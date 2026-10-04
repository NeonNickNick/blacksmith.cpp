@echo off
setlocal enabledelayedexpansion

set "UV_LINK_MODE=copy"
set "ROOT=%~dp0"
cd /d "%ROOT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Please make sure Visual Studio is installed.
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo [ERROR] No Visual Studio installation with C++ toolset found.
    pause
    exit /b 1
)

set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: %VCVARS%
    pause
    exit /b 1
)

echo [INFO] Initializing MSVC environment: %VCVARS%
call "%VCVARS%"
if errorlevel 1 (
    echo [ERROR] Failed to run vcvars64.bat.
    pause
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cl.exe still not in PATH.
    pause
    exit /b 1
)
echo [INFO] cl.exe is ready.

pushd "%ROOT%python\blacksmith"
echo [INFO] uv sync (first)
uv sync
if errorlevel 1 ( echo [ERROR] uv sync failed & popd & pause & exit /b 1 )
popd

echo [INFO] cmake --preset x64-release
cmake --preset x64-release
if errorlevel 1 ( echo [ERROR] CMake configure failed & pause & exit /b 1 )

echo [INFO] cmake --build
cmake --build out/build/x64-release --config Release
if errorlevel 1 ( echo [ERROR] CMake build failed & pause & exit /b 1 )

pushd "%ROOT%python\blacksmith"
echo [INFO] uv build --wheel
uv build --wheel
if errorlevel 1 ( echo [ERROR] uv build failed & popd & pause & exit /b 1 )
popd

pushd "%ROOT%python"
echo [INFO] uv sync (second)
uv sync
if errorlevel 1 ( echo [ERROR] uv sync failed & popd & pause & exit /b 1 )

set "WHEEL="
for %%f in ("%ROOT%python\blacksmith\dist\blacksmith-*.whl") do set "WHEEL=%%f"

if not defined WHEEL (
    echo [ERROR] No blacksmith-*.whl found in dist directory.
    popd & pause & exit /b 1
)

echo [INFO] Installing wheel: !WHEEL!
uv pip install "!WHEEL!"
if errorlevel 1 ( echo [ERROR] Failed to install wheel & popd & pause & exit /b 1 )

popd

echo.
echo ============================================
echo [SUCCESS] Build completed.
echo ============================================
pause
endlocal
exit /b 0