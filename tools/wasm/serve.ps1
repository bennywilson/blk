# serve.ps1
#
# 2026 blk
#
# Serves build_wasm/ (the web viewer) without rebuilding it.
#
#   powershell tools/wasm/serve.ps1                  # http://127.0.0.1:8080/viewer.html
#   powershell tools/wasm/serve.ps1 -Level gs_test   # opens that level
#   powershell tools/wasm/serve.ps1 -Port 9000 -NoOpen

param(
	[int]$Port = 8080,
	[string]$Level = "",
	[string]$Backend = "webgpu",
	[switch]$NoOpen
)

$ErrorActionPreference = "Stop"

$outDir = Join-Path (Split-Path (Split-Path $PSScriptRoot)) "build_wasm"
if (-not (Test-Path (Join-Path $outDir "viewer.html"))) {
	Write-Error "No viewer.html in $outDir - run tools/wasm/build_wasm.ps1 first."
}

$url = "http://127.0.0.1:$Port/viewer.html"
if ($Level -ne "") {
	$url += "?level=$Level&backend=$Backend"
}

if (-not $NoOpen) {
	Start-Process $url
}

Write-Host "Serving $outDir on $url (Ctrl+C to stop)"
python -m http.server $Port --bind 127.0.0.1 --directory $outDir
