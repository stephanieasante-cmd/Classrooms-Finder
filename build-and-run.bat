@echo off
setlocal
cd /d "%~dp0"
set PATH=C:\msys64\ucrt64\bin;%PATH%
if not exist build mkdir build
echo Building the C++ server...
g++ -std=c++17 -Wall -O2 backend\classroom_store.cpp backend\main.cpp -o build\server.exe -static -lws2_32
if errorlevel 1 (
  echo Build failed. Check the messages above.
  pause
  exit /b 1
)
start "" cmd /c "timeout /t 2 >nul & start http://localhost:8080"
build\server.exe
pause
