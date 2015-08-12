# Microsoft Developer Studio Project File - Name="SMPSPlay" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** NICHT BEARBEITEN **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=SMPSPlay - Win32 Debug
!MESSAGE Dies ist kein gültiges Makefile. Zum Erstellen dieses Projekts mit NMAKE
!MESSAGE verwenden Sie den Befehl "Makefile exportieren" und führen Sie den Befehl
!MESSAGE 
!MESSAGE NMAKE /f "SMPSPlay.mak".
!MESSAGE 
!MESSAGE Sie können beim Ausführen von NMAKE eine Konfiguration angeben
!MESSAGE durch Definieren des Makros CFG in der Befehlszeile. Zum Beispiel:
!MESSAGE 
!MESSAGE NMAKE /f "SMPSPlay.mak" CFG="SMPSPlay - Win32 Debug"
!MESSAGE 
!MESSAGE Für die Konfiguration stehen zur Auswahl:
!MESSAGE 
!MESSAGE "SMPSPlay - Win32 Release" (basierend auf  "Win32 (x86) Console Application")
!MESSAGE "SMPSPlay - Win32 Debug" (basierend auf  "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "SMPSPlay - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release_VC6"
# PROP BASE Intermediate_Dir "Release_VC6"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release_VC6"
# PROP Intermediate_Dir "Release_VC6"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "libs\include" /D "AUDDRV_WAVEWRITE" /D "AUDDRV_WINMM" /D "AUDDRV_DSOUND" /D "AUDDRV_XAUD2" /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /FD /c
# SUBTRACT CPP /YX /Yc /Yu
# ADD BASE RSC /l 0x407 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib ole32.lib winmm.lib dsound.lib libAudio_VC6.lib /nologo /subsystem:console /machine:I386 /libpath:"libs\lib"
# SUBTRACT LINK32 /nodefaultlib

!ELSEIF  "$(CFG)" == "SMPSPlay - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug_VC6"
# PROP BASE Intermediate_Dir "Debug_VC6"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug_VC6"
# PROP Intermediate_Dir "Debug_VC6"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "libs\include" /D "AUDDRV_WAVEWRITE" /D "AUDDRV_WINMM" /D "AUDDRV_DSOUND" /D "AUDDRV_XAUD2" /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /FD /GZ /c
# SUBTRACT CPP /YX /Yc /Yu
# ADD BASE RSC /l 0x407 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib ole32.lib winmm.lib dsound.lib libAudio_VC6d.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept /libpath:"libs\lib"
# SUBTRACT LINK32 /nodefaultlib

!ENDIF 

# Begin Target

# Name "SMPSPlay - Win32 Release"
# Name "SMPSPlay - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\src\Engine\dac.c
# End Source File
# Begin Source File

SOURCE=.\src\loader_data.c
# End Source File
# Begin Source File

SOURCE=.\src\loader_def.c
# End Source File
# Begin Source File

SOURCE=.\src\loader_ini.c
# End Source File
# Begin Source File

SOURCE=.\src\loader_smps.c
# End Source File
# Begin Source File

SOURCE=.\src\main.c
# End Source File
# Begin Source File

SOURCE=.\src\Engine\necpcm.c
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps.c
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps_commands.c
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps_drums.c
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps_extra.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\src\Engine\dac.h
# End Source File
# Begin Source File

SOURCE=.\src\loader.h
# End Source File
# Begin Source File

SOURCE=.\src\Engine\necpcm.h
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps.h
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps_commands.h
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps_int.h
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps_structs.h
# End Source File
# Begin Source File

SOURCE=.\src\Engine\smps_structs_int.h
# End Source File
# Begin Source File

SOURCE=.\src\stdbool.h
# End Source File
# Begin Source File

SOURCE=.\src\stdtype.h
# End Source File
# End Group
# Begin Group "Ressourcendateien"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Group "Libraries"

# PROP Default_Filter "c;h"
# Begin Source File

SOURCE=.\src\dirent.c
# End Source File
# Begin Source File

SOURCE=.\src\dirent.h
# End Source File
# Begin Source File

SOURCE=.\src\ini_lib.c
# End Source File
# Begin Source File

SOURCE=.\src\ini_lib.h
# End Source File
# End Group
# Begin Group "Sound Emulation"

# PROP Default_Filter "c;h"
# Begin Source File

SOURCE=.\src\chips\2612intf.c
# End Source File
# Begin Source File

SOURCE=.\src\chips\2612intf.h
# End Source File
# Begin Source File

SOURCE=.\src\chips\fm.h
# End Source File
# Begin Source File

SOURCE=.\src\chips\fm2612.c
# End Source File
# Begin Source File

SOURCE=.\src\chips\mamedef.h
# End Source File
# Begin Source File

SOURCE=.\src\chips\sn76496.c
# End Source File
# Begin Source File

SOURCE=.\src\chips\sn76496.h
# End Source File
# Begin Source File

SOURCE=.\src\chips\sn764intf.c
# End Source File
# Begin Source File

SOURCE=.\src\chips\sn764intf.h
# End Source File
# Begin Source File

SOURCE=.\src\Sound.c
# End Source File
# Begin Source File

SOURCE=.\src\sound.h
# End Source File
# Begin Source File

SOURCE=.\src\chips\upd7759.c
# End Source File
# Begin Source File

SOURCE=.\src\chips\upd7759.h
# End Source File
# Begin Source File

SOURCE=.\src\vgmwrite.c
# End Source File
# Begin Source File

SOURCE=.\src\vgmwrite.h
# End Source File
# End Group
# End Target
# End Project
