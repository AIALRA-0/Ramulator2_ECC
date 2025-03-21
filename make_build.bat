@echo off
REM 获取当前目录并转换为 WSL 路径格式
for /f "delims=" %%i in ('wsl wslpath "%cd%"') do set "wsl_path=%%i"

REM 构建并编译项目
wsl -e bash -c "mkdir -p '%wsl_path%/build' && cd '%wsl_path%/build' && cmake .. && make -j4 && cp ./ramulator2 ../ramulator2"

pause
