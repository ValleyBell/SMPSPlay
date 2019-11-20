@echo off

@if "%VCINSTALLDIR%" == "" goto patherr

set BUILD_DIR=build_VC2017_Win64
set BASE_DIR=%CD%\download
set INST_DIR=%CD%\install
set GENERATOR=Visual Studio 15 2017 Win64
set TOOLSET=v141_xp

pushd .

rem create clean folder structure
rmdir /S /Q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"


rem ---- build/install libvgm ---
set LIBVGM_DIR=libvgm
mkdir %LIBVGM_DIR%
cd %LIBVGM_DIR%

set LIBVGM_CMAKE_OPTS=-D BUILD_TESTS=OFF -D BUILD_PLAYER=OFF -D BUILD_LIBAUDIO=ON -D BUILD_LIBEMU=ON -D BUILD_LIBPLAYER=OFF -D SNDEMU_ALL=OFF -D SNDEMU_YM2612_GPGX=ON -D SNDEMU_SN76496_MAME=ON -D SNDEMU_UPD7759_ALL=ON
cmake -G "%GENERATOR%" -T "%TOOLSET%" -D CMAKE_INSTALL_PREFIX="%INST_DIR%" %LIBVGM_CMAKE_OPTS% "%BASE_DIR%\%LIBVGM_DIR%"
if errorlevel 1 goto builderror
msbuild /property:Configuration=Debug INSTALL.vcxproj
if errorlevel 1 goto builderror
msbuild /property:Configuration=Release INSTALL.vcxproj
if errorlevel 1 goto builderror

cd ..\..\


popd
echo Done.

exit /b

:patherr
@echo Error: MSVC path not set!
@echo Please run within a Visual Studio command line or use vcvarsall.bat.
pause
exit /b

:builderror
popd
@echo An error occoured!
pause
exit /b
