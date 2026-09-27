# launcher

Build and run blk from a web dashboard or a CLI. Ported from black_splat's
launcher; the logic is plain Python in `build.py`, the `.bat` files are thin
wrappers.

## Dashboard

Double-click **`launch.bat`**; it opens <http://localhost:8090/>.

| Card | Actions |
|------|---------|
| **Native editor** | **Build** (MSBuild on `blaise/src/blaise.sln`, which pulls in `blk_engine`), **Run** (`blaise.exe` from `blaise/src`), **Build & Run**. Debug/Release dropdown. |
| **Web viewer** | **Build** (`tools/wasm/build_wasm.ps1`), **Build & Serve**, **Serve** (existing `build_wasm/`), **Tunnel** (serve over an HTTPS Cloudflare quick tunnel; needed for WebGPU on phones; shows the public URL and a QR). Level and backend dropdowns; `symbols` / `asan` checkboxes pass through to the build. |

Build output streams into the console pane; click a card title (or its tab) to
view its log. One job per card at a time; starting a new one stops the old. The
page re-attaches to running jobs after a reload. Ports: dashboard `:8090`, web
viewer `:8080`.

## CLI

```
build.bat list
build.bat run native build_run Release
build.bat run web build_serve --level gs_test
build.bat run web tunnel
build.bat stop            # kill whatever is on :8090 (the dashboard)
build.bat stop 8080       # kill whatever is on the web viewer's port
```

`stop` is the escape hatch for a server that outlived its window.

## Notes

- Serve/Tunnel refuse to start if something else already holds `:8080`. On
  Windows a leftover `python -m http.server ... --bind 127.0.0.1` (from
  `serve.ps1` or `build_wasm.ps1 -Serve`) doesn't block the launcher's bind but
  does shadow it, so `localhost:8080` would show a stale, cached build. Run
  `build.bat stop 8080` to clear it.
- Levels come from `blaise/assets/levels/**/*.blklevel` (file stem).

## Requirements

- Python 3.8+ (stdlib only)
- Visual Studio with the C++ workload (MSBuild is found via `vswhere`)
- Emscripten at `C:\emsdk` for web builds (see `tools/wasm/build_wasm.ps1`)
- `cloudflared` on PATH for Tunnel (`scoop install cloudflared`)
- Node (`npx`) optional: only used to render the dashboard's QR image
