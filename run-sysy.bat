@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "IMAGE=maxxing/compiler-dev"

if "%~1"=="" (
  echo Drag a .sy file onto this script, or paste the .sy path below.
  set /p "SRC=SysY file: "
) else (
  set "SRC=%~1"
)

set "SRC=%SRC:"=%"

if not exist "%SRC%" (
  echo File not found: %SRC%
  pause
  exit /b 1
)

for %%I in ("%SRC%") do (
  set "SRC_DIR=%%~dpI"
  set "SRC_NAME=%%~nxI"
)
for %%I in ("%SRC_DIR%.") do set "SRC_DIR=%%~fI"

if not "%~2"=="" (
  if not exist "%~2" (
    echo Input file not found: %~2
    pause
    exit /b 1
  )
  for %%I in ("%~2") do (
    set "INPUT_DIR=%%~dpI"
    set "INPUT_NAME=%%~nxI"
  )
  for %%I in ("%INPUT_DIR%.") do set "INPUT_DIR=%%~fI"
)

docker info >nul 2>nul
if errorlevel 1 (
  echo Docker Desktop is not running, or Docker is not available.
  echo Start Docker Desktop first, then run this script again.
  pause
  exit /b 1
)

if "%~2"=="" (
  docker run --rm -it ^
    -v "%CD%:/root/compiler" ^
    -v "%SRC_DIR%:/root/sysy-case:ro" ^
    -w /root/compiler ^
    %IMAGE% bash scripts/run_case.sh "/root/sysy-case/%SRC_NAME%"
) else (
  if /I "%SRC_DIR%"=="%INPUT_DIR%" (
    docker run --rm -it ^
      -v "%CD%:/root/compiler" ^
      -v "%SRC_DIR%:/root/sysy-case:ro" ^
      -w /root/compiler ^
      %IMAGE% bash scripts/run_case.sh "/root/sysy-case/%SRC_NAME%" "/root/sysy-case/%INPUT_NAME%"
  ) else (
    docker run --rm -it ^
      -v "%CD%:/root/compiler" ^
      -v "%SRC_DIR%:/root/sysy-case:ro" ^
      -v "%INPUT_DIR%:/root/sysy-input:ro" ^
      -w /root/compiler ^
      %IMAGE% bash scripts/run_case.sh "/root/sysy-case/%SRC_NAME%" "/root/sysy-input/%INPUT_NAME%"
  )
)

set "STATUS=%ERRORLEVEL%"
echo.
if not "%STATUS%"=="0" echo Script failed with exit code %STATUS%.
pause
exit /b %STATUS%
