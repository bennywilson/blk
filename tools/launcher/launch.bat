@echo off
rem launch.bat
rem
rem 2026 blk
rem
rem Starts the launcher dashboard and opens it in your browser.
rem Close this window (or Ctrl+C) to stop it; `build.bat stop` is the escape
rem hatch if a server outlives its window.

setlocal
set "DIR=%~dp0"
start "" /b python -c "import time, webbrowser; time.sleep(1); webbrowser.open('http://localhost:8090/')"
python "%DIR%server.py"
