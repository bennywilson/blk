# build_wasm.ps1
#
# 2026 blk
#
# Emscripten build for the web viewer spike.
#
# Deliberately NOT a CMake conversion. blk_engine.vcxproj stays the single
# source of truth for the two native configurations; this script compiles an
# explicit, separate file list so that a wasm build can never perturb them.
# The list is small on purpose - it is the spike's actual finding, a running
# record of how much of the engine the core really drags in.
#
#   pwsh tools/wasm/build_wasm.ps1              # build
#   pwsh tools/wasm/build_wasm.ps1 -Serve       # build, then serve on :8080

param(
	[switch]$Serve,
	[int]$Port = 8080
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine = Join-Path $repo "blk_engine"
$outDir = Join-Path $repo "build_wasm"
# em++, not emcc: emcc compiles .cpp as C++ but links as C, so libc++ and
# libc++abi are left off the link line and every std:: symbol comes up undefined.
$emcc = "C:\emsdk\upstream\emscripten\em++.exe"

if (-not (Test-Path $emcc)) {
	throw "em++ not found at $emcc - run C:\emsdk\emsdk.bat install latest + activate latest"
}

New-Item -ItemType Directory -Force $outDir | Out-Null

# Source list. Grows only as link errors demand it -- see the header comment.
$sources = @(
	"core/blk_core.cpp",
	"core/blk_string.cpp",
	"game/job_manager.cpp",
	"math/blk_math.cpp",
	"math/matrix.cpp",
	"math/quaternion.cpp",
	"math/plane3d.cpp",
	"renderer/renderer.cpp",
	"renderer/render_graph.cpp",
	"renderer/render_defs.cpp",
	"renderer/renderer_factory.cpp",
	"renderer/null/renderer_null.cpp",
	"viewer/viewer_main_web.cpp"
) | ForEach-Object { Join-Path $engine $_ }

$includes = @(
	"core", "math", "renderer", "renderer/null", "game", "collision", "app", "sound", "viewer"
) | ForEach-Object { "-I", (Join-Path $engine $_) }

$flags = @(
	"-std=c++20",
	"-D_XM_NO_INTRINSICS_",
	"-fexceptions",              # blk::error() throws, and error_check() relies on it
	# Component::component<T>() dereferences GetOwner() while GameEntity is still
	# incomplete. MSVC defers parsing template bodies to instantiation, so it
	# never notices; this asks clang to do the same rather than reordering the
	# component/entity headers.
	"-fdelayed-template-parsing",
	"-Wno-microsoft-goto",
	"-Wno-invalid-offsetof",
	"-O1",
	"-sALLOW_MEMORY_GROWTH=1",
	"-sEXIT_RUNTIME=0",
	"-sNO_DISABLE_EXCEPTION_CATCHING",
	"--shell-file", (Join-Path $PSScriptRoot "shell.html")
)

$target = Join-Path $outDir "viewer.html"

Write-Host "Compiling $($sources.Count) source files -> $target"
& $emcc @sources @includes @flags -o $target
if ($LASTEXITCODE -ne 0) {
	throw "emcc failed with exit code $LASTEXITCODE"
}

Write-Host "Built $target"

if ($Serve) {
	$python = (Get-Command python).Source
	Write-Host "Serving $outDir on http://localhost:$Port/viewer.html"
	& $python -m http.server $Port --directory $outDir
}
