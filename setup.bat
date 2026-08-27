@echo off
setlocal

set "SPLATS_URL=https://benny-wilson.com/blk_storage/gaussian_splats.zip"
set "ASSETS_DIR=%~dp0blk_engine\assets"
set "DEST_DIR=%ASSETS_DIR%\gaussian_splats"
set "ZIP_PATH=%TEMP%\gaussian_splats.zip"

echo Downloading Gaussian splat assets
echo   from: %SPLATS_URL%
echo   to:   %DEST_DIR%
echo.

curl -L --fail -o "%ZIP_PATH%" "%SPLATS_URL%"
if errorlevel 1 (
    echo ERROR: Download failed.
    exit /b 1
)

if not exist "%ASSETS_DIR%" (
    mkdir "%ASSETS_DIR%"
)

rem The zip already wraps its contents in a top-level folder, so extract
rem one level up into assets\ rather than into gaussian_splats\ itself.
echo Extracting...
tar -xf "%ZIP_PATH%" -C "%ASSETS_DIR%"
if errorlevel 1 (
    echo ERROR: Extraction failed.
    exit /b 1
)

if not exist "%DEST_DIR%" (
    if exist "%ASSETS_DIR%\gaussian_splat" (
        ren "%ASSETS_DIR%\gaussian_splat" "gaussian_splats"
    )
)

del "%ZIP_PATH%"

echo.
echo Done. Splats saved to %DEST_DIR%
endlocal
