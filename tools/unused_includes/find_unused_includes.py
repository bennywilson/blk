"""Find (and optionally remove) unused #include directives, by brute force.

Rather than guessing from symbol usage the way IWYU / clang-tidy's
misc-include-cleaner do, this asks the compiler: comment out one #include,
recompile, and keep the finding only if it still builds. That is slower but has
no false positives from macros, transitive template instantiation, or headers
included deliberately as a facade -- which matters in a codebase MSVC-specific
enough that clang-based tooling needs a compile_commands.json it does not have.

Two rules drive the design, both learned the hard way:

  * A .cpp is included by nothing, so a single-TU verdict is final.
  * A .h is not. Removals must be validated against every TU that pulls it in,
    AND accepted cumulatively -- removing an include from one header deletes a
    transitive path another header was silently relying on, so a set of
    individually-safe header removals is not necessarily jointly safe. Header
    probing therefore runs on a throwaway copy of the tree and re-verifies the
    whole world after every acceptance.

Usage:
    # what changed on this branch
    python find_unused_includes.py --changed-vs main

    # specific files
    python find_unused_includes.py --files blk_engine/renderer/renderer.cpp

    # everything the projects compile
    python find_unused_includes.py --all

    # ...then remove what it found
    python find_unused_includes.py --changed-vs main --apply

Requires: the projects to have been built at least once (flags are read from
the MSBuild .tlog), and Python 3.8+.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# Every project whose TUs must keep compiling. Headers are shared between them,
# so a removal is only safe if it holds for all of them, under each one's own
# flags and include path.
PROJECTS = [
    os.path.join(REPO, "blk_engine", "blk_engine.vcxproj"),
    os.path.join(REPO, "blaise", "src", "blaise.vcxproj"),
]

PROGRAM_FILES_X86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
VSWHERE = os.path.join(PROGRAM_FILES_X86, "Microsoft Visual Studio", "Installer",
                       "vswhere.exe")

INC_RE = re.compile(rb'^\s*#\s*include\s*([<"])([^>"]*)[>"]')
IF_RE = re.compile(rb'^\s*#\s*(if|ifdef|ifndef|endif)\b')

# Flags that are per-file state or serialize the build; harmless to drop and
# they get in the way of running many probe compiles at once.
DROP_FLAGS = ("/ZI", "/JMC", "/Gm-", "/MP", "/Zs", "/c")
DROP_PREFIXES = ("/Fo", "/Fd", "/Fp", "/Yu", "/Yc", "/FS")


# --------------------------------------------------------------------------
# toolchain discovery
# --------------------------------------------------------------------------

def _vswhere(*args):
    out = subprocess.run([VSWHERE] + list(args), capture_output=True, text=True)
    return [l for l in out.stdout.splitlines() if l.strip()]


def find_msbuild():
    hits = _vswhere("-latest", "-products", "*", "-requires",
                    "Microsoft.Component.MSBuild", "-find",
                    r"MSBuild\**\Bin\MSBuild.exe")
    if not hits:
        sys.exit("MSBuild not found via vswhere")
    return hits[0]


def find_cl(env):
    """cl.exe matching the toolset the project actually builds with."""
    for d in env.get("PATH", "").split(os.pathsep):
        cand = os.path.join(d, "cl.exe")
        if os.path.exists(cand):
            return cand
    hits = _vswhere("-latest", "-products", "*", "-find",
                    r"VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe")
    if not hits:
        sys.exit("cl.exe not found")
    return hits[-1]


def msb_property(msbuild, vcxproj, config, name):
    out = subprocess.run(
        [msbuild, vcxproj, "-p:Configuration=" + config, "-p:Platform=x64",
         "-getProperty:" + name, "-nologo"],
        capture_output=True, text=True, cwd=os.path.dirname(vcxproj))
    return out.stdout.strip()


def capture_env(msbuild, vcxproj, config, scratch):
    """The exact environment MSBuild hands cl.exe.

    Rebuilding INCLUDE by hand is a trap: a project can override IncludePath
    without re-appending the default, and MSBuild *prepends* it to the toolchain
    default rather than replacing it. So ask MSBuild instead, by swapping in a
    batch file that dumps its environment in place of the compiler.
    """
    dump = os.path.join(scratch, "dumpenv.bat")
    out = os.path.join(scratch, "env_%s.txt" % os.path.basename(vcxproj))
    with open(dump, "w") as fh:
        fh.write("@echo off\nset > \"%s\"\nexit /b 0\n" % out)

    tus = list_tus(vcxproj)
    if not tus:
        sys.exit("no ClCompile items in " + vcxproj)
    subprocess.run(
        [msbuild, vcxproj, "-p:Configuration=" + config, "-p:Platform=x64",
         "-t:ClCompile", "-p:CLToolExe=dumpenv.bat", "-p:CLToolPath=" + scratch,
         "-p:TrackFileAccess=false",
         "-p:SelectedFiles=" + os.path.join(os.path.dirname(vcxproj), tus[0]),
         "-nologo", "-v:q"],
        capture_output=True, text=True, cwd=os.path.dirname(vcxproj))
    if not os.path.exists(out):
        sys.exit("failed to capture compiler environment for " + vcxproj)

    env = dict(os.environ)
    with open(out, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                env[k] = v
    # MSBuild sets this so cl.exe pipes diagnostics to the build logger instead
    # of stdout. Exit codes still work, but we want to read the errors.
    env.pop("VS_UNICODE_OUTPUT", None)
    return env


def capture_flags(msbuild, vcxproj, config):
    """Read the real compile flags out of the build's .tlog."""
    intdir = msb_property(msbuild, vcxproj, config, "IntDir")
    name = msb_property(msbuild, vcxproj, config, "ProjectName")
    tlog = os.path.join(os.path.dirname(vcxproj), intdir,
                        name + ".tlog", "CL.command.1.tlog")
    if not os.path.exists(tlog):
        sys.exit("no build log at %s\nBuild %s (%s) once first."
                 % (tlog, os.path.basename(vcxproj), config))
    with open(tlog, encoding="utf-16-le", errors="replace") as fh:
        text = fh.read()
    if "\x00" in text or not text.strip():
        with open(tlog, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    cmd = next((l for l in text.splitlines()
                if l.lstrip().startswith("/") and "/D" in l), None)
    if not cmd:
        sys.exit("could not parse flags from " + tlog)

    flags, skip = [], False
    for tok in cmd.split():
        if skip:
            skip = False
            continue
        if tok in DROP_FLAGS or tok.startswith(DROP_PREFIXES):
            continue
        if tok.upper().endswith((".CPP", ".C", ".CXX")):
            continue
        flags.append(tok)
    return flags


# --------------------------------------------------------------------------
# project / source inspection
# --------------------------------------------------------------------------

def list_tus(vcxproj):
    with open(vcxproj, encoding="utf-8-sig", errors="replace") as fh:
        return re.findall(r'<ClCompile\s+Include="([^"]+)"', fh.read())


def read_lines(path):
    with open(path, "rb") as fh:
        return fh.read().split(b"\n")


def find_includes(lines):
    """[(index, text, conditional)] for each #include.

    `conditional` marks includes inside #if/#ifdef, where a passing probe may
    only mean the branch is inactive in this configuration. A header's own
    include guard is not counted as nesting.
    """
    out, depth, guard = [], 0, False
    for i, raw in enumerate(lines):
        line = raw.rstrip(b"\r")
        m = IF_RE.match(line)
        if m:
            kind = m.group(1)
            if kind == b"endif":
                depth = max(0, depth - 1)
            elif (kind == b"ifndef" and depth == 0 and not guard and not out
                  and i + 1 < len(lines)
                  and lines[i + 1].strip().startswith(b"#define")):
                guard = True
            else:
                depth += 1
            continue
        m = INC_RE.match(line)
        if m:
            out.append((i, line.decode("utf-8", "replace").strip(), depth > 0))
    return out


def blank(lines, idxs):
    """Comment out the given lines, preserving numbering."""
    out = list(lines)
    for i in idxs:
        out[i] = b"//" + out[i]
    return b"\n".join(out)


def is_own_header(src_rel, inc_text):
    """A .cpp including its own header is what proves that header is
    self-contained. The probe will call it removable; it should stay."""
    m = INC_RE.match(inc_text.encode())
    if not m:
        return False
    inc = os.path.basename(m.group(2).decode()).lower()
    stem = os.path.splitext(os.path.basename(src_rel))[0].lower()
    return os.path.splitext(inc)[0] == stem


# --------------------------------------------------------------------------
# compiling
# --------------------------------------------------------------------------

class Toolchain:
    def __init__(self, msbuild, vcxproj, config, scratch):
        self.proj_dir = os.path.dirname(vcxproj)
        self.env = capture_env(msbuild, vcxproj, config, scratch)
        self.flags = capture_flags(msbuild, vcxproj, config)
        self.cl = find_cl(self.env)
        self.tus = list_tus(vcxproj)
        self.name = os.path.basename(vcxproj)

    def check(self, path):
        """Syntax-check one TU (no codegen, so no .obj collisions)."""
        p = subprocess.run(
            [self.cl] + self.flags + ["/Zs", path],
            cwd=self.proj_dir, env=self.env, capture_output=True, timeout=1800)
        return p.returncode == 0, (p.stdout + p.stderr).decode("utf-8", "replace")

    def compile_obj(self, path, objdir):
        """Full compile of one TU, including codegen."""
        p = subprocess.run(
            [self.cl] + self.flags + ["/c", "/Fo" + objdir + os.sep, path],
            cwd=self.proj_dir, env=self.env, capture_output=True, timeout=1800)
        return p.returncode == 0, (p.stdout + p.stderr).decode("utf-8", "replace")


def build_world(chains, workers, root=None):
    """Compile every TU of every project. True only if all succeed."""
    jobs = []
    for tc in chains:
        base = tc.proj_dir if root is None else \
            os.path.join(root, os.path.relpath(tc.proj_dir, REPO))
        for tu in tc.tus:
            jobs.append((tc, os.path.join(base, tu)))
    with ThreadPoolExecutor(max_workers=workers) as ex:
        out = list(ex.map(lambda j: (j[1],) + j[0].check(j[1]), jobs))
    return [(p, log) for p, ok, log in out if not ok]


# --------------------------------------------------------------------------
# probing
# --------------------------------------------------------------------------

PREFIX = "__inc_probe_"


def sweep(root):
    for dirpath, _d, files in os.walk(root):
        for f in files:
            if f.startswith(PREFIX):
                try:
                    os.remove(os.path.join(dirpath, f))
                except OSError:
                    pass


def probe_source(rel, tc, workers, objdir):
    """A .cpp is included by nothing, so a single-TU verdict is final."""
    path = os.path.join(REPO, rel)
    src_dir = os.path.dirname(path)
    stem = PREFIX + os.path.splitext(os.path.basename(path))[0]
    lines = read_lines(path)
    incs = find_includes(lines)
    res = {"file": rel, "kind": "source", "includes": len(incs),
           "unused": [], "skipped": [], "baseline": True}

    def run(name, body):
        # The probe lives beside the original so quote-include resolution and
        # the include path behave exactly as they do for the real file.
        p = os.path.join(src_dir, name + ".cpp")
        with open(p, "wb") as fh:
            fh.write(body)
        try:
            return tc.compile_obj(p, objdir)[0]
        finally:
            for f in (p, os.path.join(objdir, name + ".obj")):
                if os.path.exists(f):
                    os.remove(f)

    if not run(stem + "_base", blank(lines, [])):
        res["baseline"] = False
        return res

    def one(job):
        n, (i, text, cond) = job
        return i, text, cond, run("%s_%d" % (stem, n), blank(lines, [i]))

    with ThreadPoolExecutor(max_workers=workers) as ex:
        for i, text, cond, ok in ex.map(one, list(enumerate(incs))):
            if not ok:
                continue
            entry = {"line": i + 1, "text": text, "conditional": cond}
            if is_own_header(rel, text):
                entry["reason"] = "own header (proves self-containedness)"
                res["skipped"].append(entry)
            elif cond:
                entry["reason"] = "inside #if -- branch may be inactive here"
                res["skipped"].append(entry)
            else:
                res["unused"].append(entry)

    # Individually removable does not imply jointly removable: two headers can
    # each supply the same declaration.
    drop = [u["line"] - 1 for u in res["unused"]]
    if drop and not run(stem + "_all", blank(lines, drop)):
        res["unused"], res["combined_failed"] = [], True
    return res


def probe_headers(rels, chains, workers, scratch):
    """Headers are probed cumulatively against every TU in every project.

    Each acceptance is left in place for subsequent candidates, so the accepted
    set is jointly consistent by construction. Runs on a copy of the tree.
    """
    tree = os.path.join(scratch, "tree")
    if os.path.exists(tree):
        shutil.rmtree(tree, ignore_errors=True)
    print("  copying source tree...", flush=True)
    subprocess.run(
        ["robocopy", REPO, tree, "*.h", "*.hpp", "*.inl", "*.cpp", "*.c",
         "*.hxx", "*.vcxproj", "*.props", "/E", "/XD", ".git", "x64", "lib",
         "assets", ".vs", "/NFL", "/NDL", "/NJH", "/NJS", "/NP"],
        capture_output=True)

    bad = build_world(chains, workers, root=tree)
    if bad:
        sys.exit("copied tree does not build; cannot probe headers:\n" +
                 "\n".join(p for p, _ in bad[:5]))

    results = []
    for rel in rels:
        path = os.path.join(tree, rel)
        if not os.path.exists(path):
            continue
        lines = read_lines(path)
        incs = find_includes(lines)
        res = {"file": rel, "kind": "header", "includes": len(incs),
               "unused": [], "skipped": [], "baseline": True}
        keep = []
        for i, text, cond in incs:
            with open(path, "wb") as fh:
                fh.write(blank(lines, keep + [i]))
            if build_world(chains, workers, root=tree):
                continue
            keep.append(i)
            entry = {"line": i + 1, "text": text, "conditional": cond}
            if cond:
                entry["reason"] = "inside #if -- branch may be inactive here"
                res["skipped"].append(entry)
                keep.pop()
            else:
                res["unused"].append(entry)
        with open(path, "wb") as fh:
            fh.write(blank(lines, keep))
        results.append(res)
        print("  %-46s %2d/%2d" % (rel, len(keep), len(incs)), flush=True)

    if build_world(chains, workers, root=tree):
        sys.exit("joint verification failed -- refusing to report header results")
    return results


# --------------------------------------------------------------------------

def changed_files(base):
    out = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=d", base + "...HEAD"],
        capture_output=True, text=True, cwd=REPO).stdout.split()
    out += subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=d"],
        capture_output=True, text=True, cwd=REPO).stdout.split()
    keep = []
    for f in sorted(set(out)):
        if not f.endswith((".cpp", ".h", ".hpp", ".inl", ".c")):
            continue
        # Vendored code is not ours to tidy.
        if "/External/" in f or "/external/" in f:
            continue
        if os.path.exists(os.path.join(REPO, f)):
            keep.append(f)
    return keep


def apply_removals(results):
    total = 0
    for r in results:
        if not r["unused"]:
            continue
        path = os.path.join(REPO, r["file"])
        lines = read_lines(path)
        drop = []
        for u in r["unused"]:
            idx = u["line"] - 1
            actual = lines[idx].strip().decode("utf-8", "replace")
            if actual != u["text"].strip():
                sys.exit("MISMATCH %s:%d\n  expected %r\n  actual   %r"
                         % (r["file"], u["line"], u["text"].strip(), actual))
            drop.append(idx)
        for idx in sorted(drop, reverse=True):
            del lines[idx]
        with open(path, "wb") as fh:
            fh.write(b"\n".join(lines))
        total += len(drop)
        print("  %-52s -%d" % (r["file"], len(drop)))
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--changed-vs", metavar="BASE",
                   help="probe files changed vs BASE plus uncommitted changes")
    g.add_argument("--files", nargs="+", help="explicit repo-relative paths")
    g.add_argument("--all", action="store_true", help="every TU in the projects")
    ap.add_argument("--config", default="Debug")
    ap.add_argument("--apply", action="store_true",
                    help="delete the confirmed-unused includes")
    ap.add_argument("--skip-headers", action="store_true",
                    help="sources only (fast; headers need a whole-tree pass)")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 8)
    ap.add_argument("--scratch", default=os.path.join(HERE, ".probe"))
    a = ap.parse_args()

    os.makedirs(a.scratch, exist_ok=True)
    objdir = os.path.join(a.scratch, "obj")
    os.makedirs(objdir, exist_ok=True)

    msbuild = find_msbuild()
    print("configuring toolchain...")
    chains = [Toolchain(msbuild, p, a.config, a.scratch) for p in PROJECTS]
    primary = chains[0]

    if a.changed_vs:
        files = changed_files(a.changed_vs)
    elif a.files:
        files = [f.replace("\\", "/") for f in a.files]
    else:
        files = []
        for tc in chains:
            rel = os.path.relpath(tc.proj_dir, REPO).replace("\\", "/")
            files += [rel + "/" + t.replace("\\", "/") for t in tc.tus]

    srcs = [f for f in files if not f.endswith((".h", ".hpp", ".inl"))]
    hdrs = [f for f in files if f.endswith((".h", ".hpp", ".inl"))]
    print("%d source(s), %d header(s)\n" % (len(srcs), len(hdrs)))

    results = []
    sweep(REPO)
    try:
        for rel in srcs:
            # Use whichever project actually compiles this file. Compare
            # normalised paths -- `rel` uses forward slashes, proj_dir does not.
            full = os.path.normcase(os.path.normpath(os.path.join(REPO, rel)))
            tc = next((c for c in chains
                       if full.startswith(
                           os.path.normcase(os.path.normpath(c.proj_dir)))),
                      primary)
            r = probe_source(rel, tc, a.j, objdir)
            results.append(r)
            print("  %-52s %2d/%2d%s" % (rel, len(r["unused"]), r["includes"],
                                         "" if r["baseline"] else "  [BASELINE FAILED]"),
                  flush=True)
    finally:
        sweep(REPO)

    if hdrs and not a.skip_headers:
        print("\nheaders (cumulative, whole-codebase):")
        results += probe_headers(hdrs, chains, a.j, a.scratch)

    out = os.path.join(a.scratch, "findings.json")
    with open(out, "w") as fh:
        json.dump(results, fh, indent=1)

    n = sum(len(r["unused"]) for r in results)
    tot = sum(r["includes"] for r in results)
    skipped = [s for r in results for s in r["skipped"]]
    print("\n%d of %d includes removable  (%d left alone deliberately)"
          % (n, tot, len(skipped)))
    for r in results:
        for s in r["skipped"]:
            print("  keep %s:%d  %s -- %s"
                  % (r["file"], s["line"], s["text"], s["reason"]))
    print("details: " + out)

    if a.apply:
        print("\napplying:")
        print("\nremoved %d lines. Now run a real build of both projects in "
              "both configurations." % apply_removals(results))


if __name__ == "__main__":
    main()
