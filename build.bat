@echo off
rem Builds the MP7 40-Round Magazine GML plugin and its in-game test harness.
rem   build.bat            build into build\
rem   build.bat install    also copy the plugin into the game (refuses while the game runs)
rem
rem The magazine's mesh and textures are encrypted (tools\pack, a new key every build) and built into
rem MP7Mag40.dll (src\MP7Mag40.rc), so Assets\ must exist first: generate it from the model's .blend
rem with tools\prepare_mag.py.
rem Needs the GML SDK 2.2+ (github.com/RemArrow/geronimo-mod-loader): the external\gml submodule,
rem or set GML_SDK to its include folder. Set GML_GAME_DIR to your ...\Geronimo\Binaries\Win64 if
rem the game is not in the default Steam library.
setlocal
set ROOT=%~dp0
set OUT=%ROOT%build
set SDK=%ROOT%external\gml\include
if defined GML_SDK set SDK=%GML_SDK%
set GAME=C:\Program Files (x86)\Steam\steamapps\common\GERONIMO\Geronimo\Binaries\Win64
if defined GML_GAME_DIR set GAME=%GML_GAME_DIR%

if not exist "%SDK%\GML\GML.hpp" (
  echo GML SDK not found at %SDK%
  echo Run: git submodule update --init   or set GML_SDK
  exit /b 1
)
if not exist "%ROOT%Assets\mp7mag40.obj" (
  echo Assets\ is missing - generate it with tools\prepare_mag.py first ^(it is built into the DLL^).
  exit /b 1
)
if not defined VCToolsInstallDir (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
for %%D in ("%OUT%\obj" "%OUT%\gen" "%OUT%\MP7Mag40" "%OUT%\tests\MP7Mag40Dev") do if not exist "%%~D" mkdir "%%~D"
set CFLAGS=/nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%"
set LFLAGS=/DEBUG /INCREMENTAL:NO

echo === pack (encrypt Assets\ with a new key)
cl /nologo /std:c++20 /O2 /MT /EHsc /Fo"%OUT%\obj\\" "%ROOT%tools\pack\pack.cpp" /Fe"%OUT%\pack.exe" || exit /b 1
"%OUT%\pack.exe" "%ROOT%Assets" "%OUT%\gen\payload.bin" "%OUT%\gen\payload_key.h" || exit /b 1
echo === plugin (encrypted assets embedded)
rc /nologo /I "%ROOT%." /fo"%OUT%\obj\MP7Mag40.res" "%ROOT%src\MP7Mag40.rc" || exit /b 1
cl %CFLAGS% /I"%OUT%\gen" /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%src\*.cpp" "%OUT%\obj\MP7Mag40.res" /Fe"%OUT%\MP7Mag40\MP7Mag40.dll" /link %LFLAGS% || exit /b 1
echo === test harness (never installed)
cl %CFLAGS% /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%tests\MP7Mag40Dev\MP7Mag40Dev.cpp" /Fe"%OUT%\tests\MP7Mag40Dev\MP7Mag40Dev.dll" /link %LFLAGS% || exit /b 1
for /r "%OUT%" %%F in (*.exp *.lib *.ilk) do del /q "%%F"

if /i "%1"=="install" (
  tasklist /FI "IMAGENAME eq Geronimo-Win64-Shipping.exe" | find /I "Geronimo-Win64" >nul && (
    echo Geronimo is running - close it first.
    exit /b 1
  )
  echo === install to "%GAME%\GML\plugins\MP7Mag40"
  if not exist "%GAME%\GML\plugins\MP7Mag40" mkdir "%GAME%\GML\plugins\MP7Mag40"
  copy /y "%OUT%\MP7Mag40\MP7Mag40.dll" "%GAME%\GML\plugins\MP7Mag40\" >nul || exit /b 1
  copy /y "%ROOT%CREDITS.txt" "%GAME%\GML\plugins\MP7Mag40\" >nul || exit /b 1
  rem symbols stay in build\ (they would map out the key handling)
  if exist "%GAME%\GML\plugins\MP7Mag40\MP7Mag40.pdb" del /q "%GAME%\GML\plugins\MP7Mag40\MP7Mag40.pdb"
)
echo === done
exit /b 0
