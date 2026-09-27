@echo off
rem build.bat
rem
rem 2026 blk
rem
rem Thin wrapper: build.py is the single source of truth.
rem
rem   build.bat list
rem   build.bat run native build_run Release
rem   build.bat run web build_serve --level gs_test
rem   build.bat stop [port]

python "%~dp0build.py" %*
