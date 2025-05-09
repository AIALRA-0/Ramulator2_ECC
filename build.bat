@echo off
REM Get the current directory and convert it to WSL path format
for /f "delims=" %%i in ('wsl wslpath "%cd%"') do set "wsl_path=%%i"

REM Enter the build folder, compile the project, and copy the output binary
wsl -e bash -c "cd '%wsl_path%/build' && make -j4 && cp ./ramulator2 ../ramulator2"

pause
