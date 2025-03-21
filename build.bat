@echo off
wsl -e bash -c "cd '/mnt/f/AIALRA Workspace/Ramulator2_ECC/build' && make -j4 && cp ./ramulator2 ../ramulator2"
pause
