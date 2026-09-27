#!/usr/bin/env python3
"""Single source of truth for building/running/serving blk.

Used two ways:
  * As a library, imported by server.py (the web dashboard).
  * As a CLI, so the dashboard isn't required:
        python build.py list
        python build.py run native <build|run|build_run> [Debug|Release]
        python build.py run web <build|build_serve|serve|tunnel> [--level L] [--symbols] [--asan]
        python build.py stop [port]

Two targets:
  * native -> MSBuild on blaise/src/blaise.sln, then blaise/src/x64/<config>/blaise.exe
  * web    -> tools/wasm/build_wasm.ps1 (Emscripten), then serve build_wasm/
"""
import argparse
import glob as globmod
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BLAISE_SRC = os.path.join(ROOT, "blaise", "src")
BLAISE_SLN = os.path.join(BLAISE_SRC, "blaise.sln")
LEVELS_DIR = os.path.join(ROOT, "blaise", "assets", "levels")
WASM_SCRIPT = os.path.join(ROOT, "tools", "wasm", "build_wasm.ps1")
WASM_OUT = os.path.join(ROOT, "build_wasm")

DEFAULT_DASHBOARD_PORT = 8090
WEB_PORT = 8080  # matches tools/wasm/serve.ps1 and build_wasm.ps1 -Serve

CONFIGS = ["Debug", "Release"]
BACKENDS = ["webgpu", "null"]

TARGETS = {
    "native": ["build", "run", "build_run"],
    "web": ["build", "build_serve", "serve", "tunnel"],
}
DEFAULT_LEVEL = "the_sheep_and_fox_show"

URL_RE = re.compile(r"https://[-a-z0-9]+\.trycloudflare\.com")
PY = sys.executable or "python"

# Every server we start stamps this header on its responses, so before we
# reclaim a busy port we can tell one of our own stale instances (safe to kill)
# apart from an unrelated app that merely happens to use the same port.
IDENT_HEADER = "X-Blk"


def discover_levels():
    """Level names (file stem, the form viewer.html?level= takes) under
    blaise/assets/levels."""
    found = globmod.glob(os.path.join(LEVELS_DIR, "**", "*.blklevel"), recursive=True)
    return sorted(os.path.splitext(os.path.basename(p))[0] for p in found)


def find_msbuild():
    """MSBuild.exe from the newest Visual Studio, via vswhere. None if missing."""
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return shutil.which("MSBuild")
    out = subprocess.run(
        [vswhere, "-latest", "-requires", "Microsoft.Component.MSBuild",
         "-find", r"MSBuild\**\Bin\MSBuild.exe"],
        capture_output=True, text=True,
    ).stdout.strip().splitlines()
    return out[0] if out else shutil.which("MSBuild")


def native_exe(config):
    return os.path.join(BLAISE_SRC, "x64", config, "blaise.exe")


def _popen(cmd, cwd, env=None):
    """subprocess.Popen preconfigured for line-streamed UTF-8 text, started in
    its own process group/job so kill_tree can take out the whole subtree --
    e.g. cloudflared is a grandchild of the tunnel job."""
    kwargs = {}
    if sys.platform == "win32":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        kwargs["start_new_session"] = True
    return subprocess.Popen(
        cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace", bufsize=1,
        env=env, **kwargs,
    )


def kill_tree(proc):
    if proc is None or proc.poll() is not None:
        return
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(proc.pid)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    else:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass


def _stream(proc, on_line):
    """Stream a process's merged stdout to on_line(text, transient).

    Reads raw bytes (not text mode's universal-newline translation, which would
    fold '\\r' into '\\n' and erase the distinction) and splits on both:
      * '\\n'  -> a committed line            -> on_line(text, transient=False)
      * '\\r'  -> an in-place redraw (a progress bar repainting with a bare
                 carriage return and no newline) -> transient=True
    A '\\r\\n' pair counts as one committed line. Splitting only ever happens on
    the ASCII bytes 0x0A/0x0D, which never occur inside a UTF-8 multibyte
    sequence, so decoding each segment on its own is safe."""
    stream = getattr(proc.stdout, "buffer", proc.stdout)
    buf = bytearray()
    seg = None  # bytes ended by a lone '\r', held one byte to disambiguate \r\n
    while True:
        chunk = stream.read1(4096) if hasattr(stream, "read1") else stream.read(4096)
        if not chunk:
            break
        for b in chunk:
            if seg is not None:
                if b == 0x0A:  # the held '\r' was really a '\r\n' line ending
                    on_line(seg.decode("utf-8", "replace"), False)
                    seg = None
                    continue
                on_line(seg.decode("utf-8", "replace"), True)  # lone '\r': a redraw
                seg = None
            if b == 0x0A:
                on_line(buf.decode("utf-8", "replace"), False)
                buf.clear()
            elif b == 0x0D:
                seg = bytes(buf)
                buf.clear()
            else:
                buf.append(b)
    if seg is not None:
        on_line(seg.decode("utf-8", "replace"), False)
    if buf:
        on_line(buf.decode("utf-8", "replace"), False)


def stream_step(cmd, cwd, log, env=None, on_proc=None):
    """Run a subprocess to completion, streaming merged stdout/stderr to
    log(line[, transient]). Returns the exit code (or 1 if the executable is
    missing)."""
    log(f"$ {' '.join(cmd)}")
    try:
        proc = _popen(cmd, cwd, env=env)
    except FileNotFoundError:
        log(f"error: '{cmd[0]}' not found")
        return 1
    if on_proc:
        on_proc(proc)
    _stream(proc, log)
    proc.wait()
    return proc.returncode


def build_native(config, log, on_proc=None):
    """MSBuild blaise.sln (which pulls in blk_engine). Returns the exit code."""
    msbuild = find_msbuild()
    if not msbuild:
        log("error: MSBuild not found (install Visual Studio with the C++ workload)")
        return 1
    cmd = [msbuild, BLAISE_SLN, f"/p:Configuration={config}", "/p:Platform=x64",
           "/m", "/nologo", "/v:m"]
    return stream_step(cmd, BLAISE_SRC, log, on_proc=on_proc)


def run_native(config, log, on_proc=None):
    """Launch blaise.exe from blaise/src (it chdir("../")s to find its assets).
    Blocks until it exits."""
    exe = native_exe(config)
    if not os.path.isfile(exe):
        log(f"error: {exe} not found - build {config} first")
        return 1
    return stream_step([exe], BLAISE_SRC, log, on_proc=on_proc)


def build_web(log, symbols=False, asan=False, on_proc=None):
    """tools/wasm/build_wasm.ps1 (build only; serving is a separate step)."""
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", WASM_SCRIPT]
    if symbols:
        cmd.append("-Symbols")
    if asan:
        cmd.append("-Asan")
    return stream_step(cmd, ROOT, log, on_proc=on_proc)


def web_url(level, backend, host="localhost", port=WEB_PORT):
    return f"http://{host}:{port}/viewer.html?level={level}&backend={backend}"


def run_serve(tunneled, log, on_proc=None, on_url=None, suppress_terminal_qr=False,
              level=DEFAULT_LEVEL, backend="webgpu"):
    """Launch serve.py / serve_tunnel.py (this launcher/ dir) as a long-lived
    process over build_wasm/ and stream it until it exits or is killed.
    on_url(label, url) fires once for the local URL immediately, and again
    for the public tunnel URL once cloudflared reports it."""
    if not os.path.isfile(os.path.join(WASM_OUT, "viewer.html")):
        log(f"error: no viewer.html in {WASM_OUT} - build the web viewer first")
        return 1
    # On Windows a listener bound to 127.0.0.1:PORT (e.g. a leftover
    # `http.server` from serve.ps1 / build_wasm.ps1 -Serve) does NOT block our
    # 0.0.0.0 bind, and then shadows it: localhost:PORT would show a stale,
    # cached copy. Refuse rather than serve something the browser won't reach.
    if _port_listening(WEB_PORT) and _served_by_us(WEB_PORT) is not True:
        log(f"error: port {WEB_PORT} is already in use by another process, which would "
            f"shadow this server. Free it with: python build.py stop {WEB_PORT}")
        return 1
    script = os.path.join(HERE, "serve_tunnel.py" if tunneled else "serve.py")
    cmd = [PY, script, WASM_OUT, str(WEB_PORT)]
    log(f"$ {' '.join(cmd)}")
    # PYTHONUNBUFFERED matters most: with stdout piped (not a real terminal),
    # Python block-buffers instead of line-buffering, so a slow-but-fine run
    # looks identical to a silently-dead one until unbuffered.
    env = {**os.environ, "PYTHONIOENCODING": "utf-8", "PYTHONUTF8": "1",
           "PYTHONUNBUFFERED": "1"}
    if suppress_terminal_qr:
        # The web dashboard renders its own scannable SVG QR on the card
        # instead (see qr_svg); the terminal QR is ANSI colour, which can't
        # render in a browser <pre>. CLI users still get the terminal QR.
        env["LAUNCHER_QR"] = "0"
    proc = _popen(cmd, HERE, env=env)
    if on_proc:
        on_proc(proc)
    if on_url:
        on_url("Local", web_url(level, backend))
    public_seen = False
    for line in proc.stdout:
        log(line.rstrip("\n"))
        if tunneled and on_url and not public_seen:
            m = URL_RE.search(line)
            if m:
                public_seen = True
                url = m.group(0)
                host = url.removeprefix("https://")
                log(f"waiting for {host} to resolve publicly (fresh tunnel hostnames take a few seconds) ...")
                if wait_for_dns(host):
                    log("DNS is live; URL is safe to open/scan.")
                else:
                    log("warning: hostname still not resolving after 60s -- the URL may not work yet.")
                query = web_url(level, backend).split("/viewer.html", 1)[1]
                on_url("Public (HTTPS)", f"{url}/viewer.html{query.lstrip('/')}")
    proc.wait()
    return proc.returncode


def run_action(target, action, opts, log, on_proc=None, on_url=None,
               suppress_terminal_qr=False):
    """Runs one dashboard/CLI action to completion. Returns an exit code:
    0 = every step succeeded (or the long-lived process ended), else failure.
    opts: config, level, backend, symbols, asan."""
    config = opts.get("config", "Release")
    if target == "native":
        if action in ("build", "build_run"):
            code = build_native(config, log, on_proc)
            if code:
                log("build failed")
                return code
            log("build succeeded")
        if action in ("run", "build_run"):
            return run_native(config, log, on_proc)
        return 0
    if target == "web":
        if action in ("build", "build_serve"):
            code = build_web(log, bool(opts.get("symbols")), bool(opts.get("asan")), on_proc)
            if code:
                log("build failed")
                return code
            log("build succeeded")
        if action in ("build_serve", "serve", "tunnel"):
            return run_serve(
                tunneled=(action == "tunnel"), log=log, on_proc=on_proc, on_url=on_url,
                suppress_terminal_qr=suppress_terminal_qr,
                level=opts.get("level") or DEFAULT_LEVEL,
                backend=opts.get("backend") or "webgpu",
            )
        return 0
    log(f"unknown target: {target}")
    return 1


def _dns_query_direct(hostname, server="1.1.1.1"):
    """One A-record lookup sent straight to `server` over UDP:53, bypassing
    the local resolver entirely. Returns True (resolves), False (NXDOMAIN /
    no records yet), or None (couldn't ask -- network blocked the query).
    Hand-rolled wire format because the stdlib's getaddrinfo can only use the
    system resolver, and DoH via urllib trips over missing intermediate certs
    on the Windows Store Python."""
    qid = os.urandom(2)
    # header: id, flags=0x0100 (recursion desired), 1 question, 0 answers/etc.
    packet = qid + b"\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00"
    for label in hostname.encode("ascii").split(b"."):
        packet += bytes([len(label)]) + label
    packet += b"\x00\x00\x01\x00\x01"  # root, qtype=A, qclass=IN
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.settimeout(4)
            s.sendto(packet, (server, 53))
            resp = s.recv(512)
    except OSError:
        return None
    if len(resp) < 12 or resp[:2] != qid:
        return None
    rcode = resp[3] & 0x0F
    ancount = int.from_bytes(resp[6:8], "big")
    return rcode == 0 and ancount > 0


def wait_for_dns(hostname, timeout=60):
    """Wait until `hostname` resolves publicly. Quick-tunnel hostnames are
    minted seconds before use, and a resolver that looks one up too early
    caches the miss (some routers hold NXDOMAIN for a 30-minute negative TTL,
    killing the URL for every device on that router). So don't advertise a
    URL/QR until the name is actually live. Queries 1.1.1.1 directly, which
    neither consults nor primes the local resolver."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = _dns_query_direct(hostname)
        if result:
            return True
        if result is None:
            # Can't reach an outside resolver (e.g. UDP/53 egress blocked):
            # no signal either way, so hand the URL over unverified.
            return True
        time.sleep(2)
    return False


def qr_svg(url):
    """Render `url` as an SVG QR (white bg, black modules) via node-qrcode.
    Returns the SVG markup, or None if Node/npx isn't available."""
    if shutil.which("npx") is None:
        return None
    out = os.path.join(tempfile.gettempdir(), f"blk_qr_{os.getpid()}.svg")
    try:
        cmd = f'npx --yes qrcode -t svg -o "{out}" "{url}"'
        subprocess.run(cmd if sys.platform == "win32" else
                       ["npx", "--yes", "qrcode", "-t", "svg", "-o", out, url],
                       shell=(sys.platform == "win32"),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=False, timeout=60)
        with open(out, encoding="utf-8") as f:
            return f.read()
    except Exception:
        return None
    finally:
        try:
            os.remove(out)
        except OSError:
            pass


def stop_port(port):
    """Kill whatever process is listening on `port`. Escape hatch for a
    launcher/job that outlived its terminal -- e.g. the dashboard itself, if
    closing its window didn't take it down cleanly."""
    if sys.platform == "win32":
        out = subprocess.run(["netstat", "-ano"], capture_output=True, text=True).stdout
        pids = set()
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP" and parts[1].endswith(f":{port}") and "LISTENING" in line:
                pids.add(parts[-1])
    else:
        out = subprocess.run(["lsof", "-ti", f"tcp:{port}"], capture_output=True, text=True).stdout
        pids = {p for p in out.split() if p}

    if not pids:
        print(f"Nothing was listening on port {port}.")
        return False
    for pid in pids:
        print(f"Stopping PID {pid} on port {port} ...")
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/F", "/PID", pid],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            subprocess.run(["kill", "-9", pid])
    return True


def _port_listening(port, host="127.0.0.1"):
    """True if something is accepting TCP connections on host:port right now.
    (A port merely lingering in TIME_WAIT after a restart is NOT listening, so
    this reads as free -- the bind retry rides that window out.)"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.3)
        return s.connect_ex((host, port)) == 0


def _served_by_us(port):
    """Whether the HTTP server on `port` is one of ours, told by the
    IDENT_HEADER it stamps on every response (present even on its 404/501).
    True = ours, False = answered but not ours, None = didn't answer as HTTP."""
    req = urllib.request.Request(f"http://127.0.0.1:{port}/", method="HEAD")
    try:
        with urllib.request.urlopen(req, timeout=1.0) as r:
            headers = r.headers
    except urllib.error.HTTPError as e:
        headers = e.headers  # a 404/501 body still carries our header
    except (urllib.error.URLError, OSError, ValueError):
        return None
    return bool(headers.get(IDENT_HEADER))


def free_port_if_ours(port, log=print):
    """Make `port` bindable, reclaiming it *only* from a stale blk server (one
    that answers with IDENT_HEADER). Returns True if the port is now free (or
    was), False if it's held by a process that isn't ours -- which is left
    untouched. Never kills an unidentified process."""
    if not _port_listening(port):
        return True  # free, or a TIME_WAIT remnant the bind retry will outlast
    if _served_by_us(port) is not True:
        log(f"port {port} is in use by another process (not this launcher) -- "
            f"leaving it alone.")
        return False
    log(f"port {port} held by a stale launcher/server -- reclaiming it.")
    stop_port(port)
    for _ in range(20):  # wait for the OS to actually drop the listener
        if not _port_listening(port):
            return True
        time.sleep(0.15)
    return not _port_listening(port)


def bind_or_reclaim(make_server, port, log=print):
    """Build a server that binds `port`, first reclaiming the port from a stale
    blk instance and riding out the brief TIME_WAIT window left by a
    just-stopped server. `make_server` is a no-arg callable that binds and
    returns the server. Raises the final OSError if the port stays unavailable
    (e.g. it's held by an unrelated app -- reported, not fought over)."""
    last = None
    for attempt in range(12):  # ~2s of retries across a TIME_WAIT window
        try:
            return make_server()
        except OSError as e:
            last = e
            if attempt == 0 and not free_port_if_ours(port, log):
                raise  # not ours -- don't fight over it
            time.sleep(0.15)
    raise last


def _cli():
    # When stdout is redirected/piped Python block-buffers by default; force
    # line buffering so `build.py run ... | tee log` etc. shows output live.
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except (AttributeError, ValueError):
        pass

    parser = argparse.ArgumentParser(
        prog="build.py", description="Build/run blk without the web dashboard.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="list targets, actions and levels")

    run_p = sub.add_parser("run", help="run one action in the foreground (Ctrl+C to stop)")
    run_p.add_argument("target", choices=sorted(TARGETS))
    run_p.add_argument("action", choices=sorted({a for v in TARGETS.values() for a in v}))
    run_p.add_argument("config", nargs="?", default="Release", choices=CONFIGS,
                       help="native only (default: Release)")
    run_p.add_argument("--level", default=DEFAULT_LEVEL)
    run_p.add_argument("--backend", default="webgpu", choices=BACKENDS)
    run_p.add_argument("--symbols", action="store_true", help="web build: names in stack traces")
    run_p.add_argument("--asan", action="store_true", help="web build: AddressSanitizer")

    stop_p = sub.add_parser("stop", help="kill whatever is listening on a port")
    stop_p.add_argument("port", nargs="?", type=int, default=DEFAULT_DASHBOARD_PORT,
                        help=f"default: {DEFAULT_DASHBOARD_PORT} (the dashboard)")

    args = parser.parse_args()

    if args.cmd == "list":
        for target, actions in TARGETS.items():
            print(f"{target:8} {', '.join(actions)}")
        print("levels:  " + ", ".join(discover_levels()))
        return

    if args.cmd == "stop":
        stop_port(args.port)
        return

    if args.action not in TARGETS[args.target]:
        parser.error(f"{args.target} has no action '{args.action}' "
                     f"(choose from {', '.join(TARGETS[args.target])})")

    # Render a transient (\r) redraw in place; move off it with a newline before
    # the next committed line.
    pending_cr = [False]

    def clog(line, transient=False):
        if transient:
            sys.stdout.write("\r" + line)
            sys.stdout.flush()
            pending_cr[0] = True
        else:
            if pending_cr[0]:
                sys.stdout.write("\n")
                pending_cr[0] = False
            print(line)

    proc_holder = {}
    opts = {"config": args.config, "level": args.level, "backend": args.backend,
            "symbols": args.symbols, "asan": args.asan}
    try:
        code = run_action(
            args.target, args.action, opts, clog,
            on_proc=lambda p: proc_holder.update(proc=p),
            on_url=lambda label, url: print(f"--- {label}: {url}"),
        )
        sys.exit(code or 0)
    except KeyboardInterrupt:
        kill_tree(proc_holder.get("proc"))
        print("\nstopped.")


if __name__ == "__main__":
    _cli()
