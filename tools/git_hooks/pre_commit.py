"""Pre-commit check: the generated reflection tables have to match the staged headers.

The tables are checked in, and their contents are the on-disk key format, so a header
edit committed without a build leaves a table that says one thing and a level file that
says another - which the loader answers by silently skipping the key.

The check runs the generator against the *staged* content rather than the working tree,
so "built, but only staged the header" is caught too. Bypass one commit with --no-verify.
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "type_info"))
import generate_type_info as gen  # noqa: E402  (needs the path above)

GENERATOR = "tools/type_info/generate_type_info.py"

# The trees the generator walks, and the files it writes, as git spells paths.
ROOTS = sorted({r.replace("\\", "/") for cfg in gen.PROJECTS.values() for r in cfg["scan"] + cfg["context"]})
OUTPUTS = {cfg[k].replace("\\", "/") for cfg in gen.PROJECTS.values() for k in ("tables", "instances")}


def git(*args, **kwargs):
	run = subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8", check=True, **kwargs)
	return run.stdout


def nul_list(text):
	return [p for p in text.split("\0") if p]


def is_input(path):
	"""Does this path feed the generated tables?"""
	if path in OUTPUTS:
		return True
	if path.startswith("tools/type_info/") and path.endswith(".py"):
		return True
	if not path.endswith(".h") or not any(path.startswith(root + "/") for root in ROOTS):
		return False
	return not any(part in gen.SKIP_DIRS for part in path.split("/")[:-1])


def main():
	repo = git("rev-parse", "--show-toplevel").strip()
	staged = nul_list(git("diff", "--cached", "--name-only", "-z", cwd=repo))
	if not any(is_input(p) for p in staged):
		return 0

	# Lay the staged content out in a scratch tree and run the generator there: it finds
	# its inputs and outputs relative to its own file, so it sees only what's committed.
	files = [p for p in nul_list(git("ls-files", "-z", "--", *ROOTS, "tools/type_info", cwd=repo)) if is_input(p)]
	tmp = tempfile.mkdtemp(prefix="blk_type_info_")
	try:
		subprocess.run(
			["git", "checkout-index", "-z", "--stdin", "--prefix=" + tmp.replace("\\", "/") + "/"],
			input="\0".join(files) + "\0",
			text=True,
			encoding="utf-8",
			check=True,
			cwd=repo,
		)
		check = subprocess.run(
			[sys.executable, os.path.join(tmp, *GENERATOR.split("/")), "--check"],
			capture_output=True,
			text=True,
			encoding="utf-8",
			cwd=repo,
		)
		if check.returncode == 0:
			return 0
		# The generator names files by absolute path; make them repo-relative again.
		out = (check.stdout + check.stderr).replace(tmp + os.sep, "").replace(tmp.replace("\\", "/") + "/", "")
		print("type_info: the staged reflection tables don't match the staged headers.", file=sys.stderr)
		print(out.strip(), file=sys.stderr)
		print(f"Run: python {GENERATOR}, then stage what it rewrites.", file=sys.stderr)
		print("To commit anyway: git commit --no-verify", file=sys.stderr)
		return 1
	finally:
		shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
	sys.exit(main())
