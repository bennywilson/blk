@echo off
rem serve.bat
rem
rem 2026 blk
rem
rem Serves build_wasm\ (the web viewer) without rebuilding it. A .bat, so it
rem runs even where PowerShell scripts are disabled.
rem
rem   tools\wasm\serve.bat                # http://127.0.0.1:8080/viewer.html
rem   tools\wasm\serve.bat gs_test        # opens that level
rem   tools\wasm\serve.bat gs_test 9000   # level and port

setlocal
set "OUT=%~dp0..\..\build_wasm"
set "LEVEL=%~1"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=8080"

if not exist "%OUT%\viewer.html" (
	echo No viewer.html in %OUT% - run tools\wasm\build_wasm.ps1 first.
	exit /b 1
)

set "URL=http://127.0.0.1:%PORT%/viewer.html"
if not "%LEVEL%"=="" set "URL=%URL%?level=%LEVEL%&backend=webgpu"

start "" "%URL%"
echo Serving %OUT% on %URL% (Ctrl+C to stop)
python -m http.server %PORT% --bind 127.0.0.1 --directory "%OUT%"
