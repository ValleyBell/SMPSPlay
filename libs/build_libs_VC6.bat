echo off

@if "%MSVCDir%" == "" goto patherr

set BUILD_DIR=build_VC6
set BASE_DIR=%CD%\download
set INST_DIR=%CD%\install
rem Warning: VC6 project files require CMake 3.5 or lower, builds Debug+Release
rem set GENERATOR=Visual Studio 6
rem Unlike VC6 projects, the generated NMake Makefiles don't support multiple configurations, only Release is built.
set GENERATOR=NMake Makefiles

pushd .

rem create clean folder structure
rmdir /S /Q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"


rem ---- build/install libvgm ---
set LIBVGM_DIR=libvgm
mkdir %LIBVGM_DIR%
cd %LIBVGM_DIR%

set LIBVGM_CMAKE_OPTS=-D BUILD_TESTS=OFF -D BUILD_LIBAUDIO=ON -D BUILD_LIBEMU=ON -D SNDEMU_ALL=OFF -D SNDEMU_YM2612_GPGX=ON -D SNDEMU_SN76496_MAME=ON -D SNDEMU_UPD7759_ALL=ON
cmake -G "%GENERATOR%" -D CMAKE_INSTALL_PREFIX="%INST_DIR%" -D CMAKE_BUILD_TYPE=Release %LIBVGM_CMAKE_OPTS% "%BASE_DIR%\%LIBVGM_DIR%"
if errorlevel 1 goto builderror
rem for %%f in (*.dsw) do msdev.exe "%%f" /MAKE "INSTALL - Win32 Debug" "INSTALL - Win32 Release"
nmake /NOLOGO install
if errorlevel 1 goto builderror

cd ..\..\


popd
echo Done.

exit /b

:patherr
@echo Error: MSVC path not set!
@echo Please run VSVARS32.BAT first to set up the required directories.
pause
exit /b

:builderror
popd
@echo An error occoured!
pause
exit /b
