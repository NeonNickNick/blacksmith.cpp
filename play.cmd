@echo off
setlocal
set "PYTHONUTF8=1"
pushd "%~dp0"
if not exist "python\.venv\Scripts\python.exe" (
    echo Python environment is missing. Run build.cmd first.
    popd
    exit /b 1
)
"python\.venv\Scripts\python.exe" -m master play %*
set "PLAY_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %PLAY_EXIT_CODE%
