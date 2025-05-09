@echo off
REM Get the current directory and convert it to WSL path format
for /f "delims=" %%i in ('wsl wslpath "%cd%"') do set "wsl_path=%%i"

REM Build and compile the project
wsl -e bash -c "mkdir -p '%wsl_path%/build' && cd '%wsl_path%/build' && cmake .. && make -j4 && cp ./ramulator2 ../ramulator2"

pause
