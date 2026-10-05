@echo off
rem compare_backends.cmd: renders every frame dump in dumps\ on Vulkan and
rem on D3D12 with rd_replay_tool.exe and compares the two PNGs texel by
rem texel (compare_png.ps1). Double-click it; it needs no arguments.
rem
rem Put beside it: rd_replay_tool.exe, SDL3.dll, compare_png.ps1, and the
rem dumps\ folder the game writes with dump_every=N in ico-pc.ini
rem (docs/port/TESTING.md, "Renderer wave 6: D3D12"). Results go to
rem compare_backends.log and the PNGs to compare_out\.
rem
rem For each dump: DISPLAY, the displayed buffer the GS arithmetic writes
rem (integer paths: expected identical), then the Original presenter's
rem output (filtered: expected within 1 LSB). No labels or goto: the
rem repository stores this file with LF line ends.
setlocal
cd /d "%~dp0"
set "LOG=%~dp0compare_backends.log"
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -File compare_png.ps1"
echo compare_backends: Vulkan vs D3D12 > "%LOG%"
if not exist "dumps\*.rddump" echo No dumps\*.rddump beside this file: run the game with dump_every=N first. >> "%LOG%"
if not exist compare_out mkdir compare_out
for %%F in (dumps\*.rddump) do (
    echo. >> "%LOG%"
    echo == %%F >> "%LOG%"
    rd_replay_tool.exe "%%F" "compare_out\%%~nF-vulkan.png" --backend vulkan >> "%LOG%" 2>&1
    rd_replay_tool.exe "%%F" "compare_out\%%~nF-d3d12.png" --backend d3d12 >> "%LOG%" 2>&1
    %PS% "compare_out\%%~nF-vulkan.png" "compare_out\%%~nF-d3d12.png" >> "%LOG%" 2>&1
    rd_replay_tool.exe "%%F" "compare_out\%%~nF-present-vulkan.png" --present 640x480 --backend vulkan >> "%LOG%" 2>&1
    rd_replay_tool.exe "%%F" "compare_out\%%~nF-present-d3d12.png" --present 640x480 --backend d3d12 >> "%LOG%" 2>&1
    %PS% "compare_out\%%~nF-present-vulkan.png" "compare_out\%%~nF-present-d3d12.png" >> "%LOG%" 2>&1
)
echo. >> "%LOG%"
echo Done. >> "%LOG%"
start "" notepad "%LOG%"
