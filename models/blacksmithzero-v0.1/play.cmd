@echo off
chcp 65001 >nul
cd /d "%~dp0"
set PYTHONUTF8=1
set OMP_WAIT_POLICY=PASSIVE
python -c "import torch, numpy" >nul 2>&1
if errorlevel 1 (
  echo Please install Python dependencies: python -m pip install -r requirements.txt
  pause
  exit /b 1
)
python build.py
if errorlevel 1 (
  pause
  exit /b 1
)
python play.py
if errorlevel 1 pause
