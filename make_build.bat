@echo off
wsl -e bash -c "mkdir -p '/mnt/f/AIALRA Workspace/Ramulator2_ECC/build' && cd '/mnt/f/AIALRA Workspace/Ramulator2_ECC/build' && cmake .. && make -j4 && cp ./ramulator2 ../ramulator2"
pause
