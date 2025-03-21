@echo off
REM 获取当前目录并转换为 WSL 路径格式
for /f "delims=" %%i in ('wsl wslpath "%cd%"') do set "wsl_path=%%i"

REM 进入 build 文件夹并编译
wsl -e bash -c "cd '%wsl_path%/build' && make -j4 && cp ./ramulator2 ../ramulator2"

pause
