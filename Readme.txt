SMPS Player
===========
by Valley Bell

Version 2.20 (2018-06-24)


This program plays SMPS music files, a format commonly used in games for the Sega MegaDrive/Genesis.


Features
--------
- accurate playback of SMPS files used in MegaDrive, Sega Pico and Master System games (maybe not 100% accurate for a few special driver variations, but the differences should be inaudible)
- fully customizable SMPS commands and drums
- many SMPS driver settings, incl. FM/PSG frequencies and modulation/volume envelope commands
- separate SMPS settings for every file extention
- support for a wide range of SMPS effects and commands
- support for FM, PSG and DAC on the drum channel and mixed drums (FM/PSG is common)
- global FM instrument tables
- clean DAC playback, with frequencies based on Z80 cycle calculations
- multichannel DAC playback for games like Ristar, with adjustable DAC mixing volume
- supports compressed (DPCM) and uncompressed (PCM) DAC sounds and DAC banks
- VGM logging (v1.60) incl. automatic looping


Keys
----
Cursor Up/Down - change song
Enter - play
Space - pause/resume
N - next song
A - automatic progessing (plays the next song when a song is finished or played 2 full loops)
P - PAL mode on/off (affects only games that use VInt for timing)
S - stop and mute music (calls the StopAllSound SMPS routine)
F - fade music out (calls FadeOutMusic SMPS routine)
V - enable/disable VGM logging
J - toggle Conditional Jump variable
ESC / Q - quit

Channel Muting
--------------
1-6 - mute/unmute YM2612 channel 1-6
 *  - mute/unmute YM2612 DAC channel
7-9 - mute/unmute PSG channel 1-3
 0  - mute/unmute PSG channel 4/noise


In order to play the SMPS files of a certain game, you need to load that game's config.ini.
This is done by editing the config.ini in the folder where the SMPSPlay executable lies.


A note about offsets for SMPS Z80-based songs:
In some cases, the autodetection for the in-ROM start offset of the song fails. Then you need to specify the original Z80 offset of the song. This can be done with the following file name format:
Title.oooo.ext
oooo is a 4-digit hexadecimal number and represents the original Z80 offset of the song. (It MUST be 4-digits long or it will be ignored.)
This technique can also be used to specify the original start address of instrument tables.



Credits
-------
This program was written by Valley Bell.

The sound emulation uses sound cores from MAME.
The wave playback code was taken from VGMPlay.
The SMPS engine code is based on disassemblies of various SMPS sound drivers.

A huge thanks to the developers of IDA, The Interactive Disassembler. Disassembling the SMPS sound drivers would've been a lot harder without this great tool.
Thanks to Xeeynamo for contributing.



History
-------
2018-06-24 - 2.20
	fixed bug where a "Communication Variable" event prevented automatically going to the next song until a key is pressed
	replaced sound output and emulation system with libvgm's Audio Output and Emulation libraries
	added option to select audio API (previously only WinMM was supported)
	fixed auto-advance after stopping by pressing 'S'
	added .ini option to configure output volume
	fixed various bugs regarding 1-up save states
	added preSMPS support
	made "CompressVGM" option work
	added DAC volume control for logged VGMs
	added support for separate instrument formats for instrument libraries
	fixed Quackshot and S3K volume commands
2015-07-07 - 2.11
	improved loop detection
	fixed delay during first tick of SMPS Z80 Type 1 songs
	fixed bug with cross-referenced instruments
	added conditional jumps used by Columns
	fixed FadeIn to use parameters defined in DefDrv.txt
	fixed missing DAC drums in VGM logs of some SMPS Z80 Type 2 songs
	fixed FM Volume Envelopes
2015-04-05 - 2.10
	added "Tempo1Tick" setting, allowing for more accurate jitter emulation
	added full support for the SMPS variant used in Sonic 2 Recreation
	added support for music save states used by the Sonic series 1-up tunes
	added SFX commands from Sonic 3 & Knuckles
	added full support for Mickey Mouse: Castle of Illusion (including FM -> PSG ADSR conversion)
	added "FMDAC" drum type for Mercs
	fixed missing DAC sounds in VGM logs
	added per-driver fading settings
	added Fade In support used by Sonic games
	added support for Super Monaco GP II's melody DAC channel
	added NEC ADPCM support for Sega Pico SMPS
	added configurable wave output settings and channel muting (thanks Xeeynamo)
	added multiple "algorithm" settings for DAC driver (to simulate separate DPCM and PCM loops)
	added emulation of various bugs/oddities present in SMPS Z80
2014-08-17 - 2.01
	added proper support for Master System SMPS (incl. PSG drums, additional commands)
	added preSMPS instrument format (register-data pairs)
	added support for 2x 2op drums (used in early preSMPS Z80)
2014-07-01 - 2.00
	First real public release.
2014-05-04
	MainMemory releases SMPSOUT.DLL, a music DLL for Sonic & Knuckles PC Collection.
	I lent him much of the code I had written for SMPSPlay at that time.
2014-02-20
	Begin of the project - a complete rewrite of SMPSPlay.
