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
#   pwsh tools/wasm/build_wasm.ps1 -Symbols     # build with names in stack traces
#   pwsh tools/wasm/build_wasm.ps1 -Asan        # build with AddressSanitizer

param(
	[switch]$Serve,
	[switch]$Symbols,
	[switch]$Asan,
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
#
# Deliberately absent: editor/ (the viewer has no editor) and the d3d12/vk/sw
# backends. sound/ is present but silent off Windows - see sound_manager.h.
$sources = @(
	"core/blk_console.cpp",
	"core/blk_core.cpp",
	"core/blk_string.cpp",
	"collision/intersection_tests.cpp",
	"game/breakable_component.cpp",
	"game/camera.cpp",
	"game/cloth_component.cpp",
	"game/collision_manager.cpp",
	"game/component.cpp",
	"game/debug_component.cpp",
	"game/entity.cpp",
	"game/file.cpp",
	"game/game.cpp",
	"game/gaussian_splat.cpp",
	"game/input_manager.cpp",
	"game/job_manager.cpp",
	"game/level_component.cpp",
	"game/level_director.cpp",
	"game/light_component.cpp",
	"game/model_component.cpp",
	"game/particle_component.cpp",
	"game/render_component.cpp",
	"game/resource_manager.cpp",
	"game/terrain_component.cpp",
	"game/type_info.cpp",
	"game/ui_component.cpp",
	"math/blk_math.cpp",
	"math/matrix.cpp",
	"math/quaternion.cpp",
	"math/plane3d.cpp",
	"renderer/material.cpp",
	"renderer/model.cpp",
	"renderer/renderer.cpp",
	"renderer/render_graph.cpp",
	"renderer/render_defs.cpp",
	"renderer/renderer_factory.cpp",
	"renderer/null/renderer_null.cpp",
	"renderer/webgpu/renderer_webgpu.cpp",
	"sound/sound_component.h.cpp",
	"sound/sound_manager.cpp",
	"viewer/viewer_main_web.cpp",
	"editor/editor.cpp",
	"editor/editor_entity.cpp",
	"editor/editor_platform_web.cpp",
	"editor/manipulator.cpp",
	"editor/outliner_panel.cpp",
	"editor/properties_panel.cpp",
	"editor/resources_panel.cpp",
	"editor/undo_action.cpp",
	"editor/viewport_panel.cpp",
	"editor/workbench_panel.cpp",
	"External/imgui/imgui.cpp",
	"External/imgui/imgui_draw.cpp",
	"External/imgui/imgui_tables.cpp",
	"External/imgui/imgui_widgets.cpp",
	"External/imgui/imgui_demo.cpp",
	"External/imgui/backends/imgui_impl_wgpu.cpp"
) | ForEach-Object { Join-Path $engine $_ }

$includes = @(
	"core", "math", "renderer", "renderer/null", "game", "collision", "app", "sound", "viewer", "editor",
	"External/imgui", "External/imgui/backends"
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
	# The browser's WebGPU, through the same webgpu.h Renderer_WebGpu uses with
	# Dawn natively. Emscripten fetches the package itself.
	"--use-port=emdawnwebgpu",
	# Startup waits on the adapter and device callbacks, which only fire once
	# control returns to the page; ASYNCIFY is what lets that wait yield. It
	# costs size and speed, so the alternative - deferring level load until the
	# device is ready, and dropping the wait entirely - is worth doing later.
	"-sASYNCIFY",
	"-sALLOW_MEMORY_GROWTH=1",
	"-sEXIT_RUNTIME=0",
	"-sNO_DISABLE_EXCEPTION_CATCHING",
	"--shell-file", (Join-Path $PSScriptRoot "shell.html")
)

# Without these a trap reports "wasm-function[2275]" and nothing else. The name
# section and the runtime's own checks are what make a browser-only fault - an
# out-of-bounds read that native hardware never notices - findable.
if ($Symbols) {
	$flags += @("-g2", "-sASSERTIONS=1")
}

# Finds the wild write that a canary can only tell you happened. ASan needs its
# own shadow memory on top of the 100 MB asset package, hence the large initial
# heap; it makes the build slow and the run slower, so it is opt-in.
if ($Asan) {
	$flags += @("-fsanitize=address", "-g2", "-sALLOW_MEMORY_GROWTH=0", "-sINITIAL_MEMORY=2147483648", "-sSTACK_SIZE=5242880")
}

$target = Join-Path $outDir "viewer.html"

# The WGSL the viewer loads is build output, not checked in, and staging copies
# whatever is on disk - so refresh it first. No-ops when it is already current.
& (Get-Command python).Source (Join-Path $repo "tools\shaders\hlsl_to_wgsl.py")
if ($LASTEXITCODE -ne 0) {
	throw "hlsl_to_wgsl.py failed with exit code $LASTEXITCODE"
}

# Assets go in as a preloaded virtual filesystem - see stage_assets.py for the
# layout and why every path in it is lowercased.
& (Get-Command python).Source (Join-Path $PSScriptRoot "stage_assets.py")
if ($LASTEXITCODE -ne 0) {
	throw "stage_assets.py failed with exit code $LASTEXITCODE"
}
$fsRoot = (Join-Path $outDir "fs\blk") -replace '\\', '/'

Write-Host "Compiling $($sources.Count) source files -> $target"
& $emcc @sources @includes @flags --preload-file "$fsRoot@/blk" -o $target
if ($LASTEXITCODE -ne 0) {
	throw "emcc failed with exit code $LASTEXITCODE"
}

Write-Host "Built $target"

if ($Serve) {
	$python = (Get-Command python).Source
	Write-Host "Serving $outDir on http://localhost:$Port/viewer.html"
	& $python -m http.server $Port --directory $outDir
}
