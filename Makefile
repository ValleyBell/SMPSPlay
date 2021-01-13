########################
#
# SMPSPlay Makefile
#
########################

ifeq ($(OS),Windows_NT)
WINDOWS = 1
else
WINDOWS = 0
endif

ifeq ($(WINDOWS), 0)
USE_BSD_AUDIO = 0
USE_ALSA = 1
USE_LIBAO = 1
endif

INCPATH = libs/install/include
LIBPATH = libs/install/lib

CC = gcc
CPP = g++
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

CFLAGS := -O3 -g0 $(CFLAGS) -I libs/include -I $(INCPATH)/vgm
CCFLAGS = -std=gnu99
CPPFLAGS = -std=gnu++98
#CFLAGS += -Wall -Wextra -Wpedantic

CFLAGS += -D ENABLE_LOOP_DETECTION -D ENABLE_VGM_LOGGING
#CFLAGS += -fstack-protector
#LDFLAGS += -fstack-protector


# add Library Path, if defined
ifdef LD_LIBRARY_PATH
LDFLAGS += -L $(LD_LIBRARY_PATH)
endif


ifeq ($(WINDOWS), 1)
# for Windows, add kernel32 and winmm (Multimedia APIs)
LDFLAGS += -lkernel32 -lwinmm -ldsound -luuid -lole32
else
# for Linux add pthread support (-pthread should include -lpthread)
# add librt (clock stuff)
LDFLAGS += -lm -lrt -pthread
CFLAGS += -pthread -DSHARE_PREFIX=\"$(PREFIX)\"
endif

# --- Audio Output stuff ---
CFLAGS += -DAUDDRV_WAVEWRITE

ifeq ($(WINDOWS), 1)
CFLAGS += -D AUDDRV_WINMM
CFLAGS += -D AUDDRV_DSOUND
CFLAGS += -D AUDDRV_XAUD2
#CFLAGS += -D AUDDRV_WASAPI
endif

ifneq ($(WINDOWS), 1)
ifneq ($(USE_BSD_AUDIO), 1)
CFLAGS += -D AUDDRV_OSS
else
CFLAGS += -D AUDDRV_SADA
endif

ifeq ($(USE_ALSA), 1)
LDFLAGS += -lasound
CFLAGS += -D AUDDRV_ALSA
endif
endif

ifeq ($(USE_LIBAO), 1)
LDFLAGS += -lao
CFLAGS += -D AUDDRV_LIBAO
endif


SRC = src
OBJ = obj
LIBAUDSRC = $(SRC)/audio
LIBAUDOBJ = $(OBJ)/audio

OBJDIRS = \
	$(OBJ) \
	$(OBJ)/Engine

LIBS = \
	$(LIBPATH)/libvgm-audio.a \
	$(LIBPATH)/libvgm-utils.a \
	$(LIBPATH)/libvgm-emu.a

ENGINEOBJS = \
	$(OBJ)/Engine/dac.o \
	$(OBJ)/Engine/necpcm.o \
	$(OBJ)/Engine/smps.o \
	$(OBJ)/Engine/smps_commands.o \
	$(OBJ)/Engine/smps_drums.o \
	$(OBJ)/Engine/smps_extra.o
# Note: dirent.c is required for MS VC only.
MAINOBJS = \
	$(OBJ)/ini_lib.o \
	$(OBJ)/loader_data.o \
	$(OBJ)/loader_def.o \
	$(OBJ)/loader_ini.o \
	$(OBJ)/loader_smps.o \
	$(OBJ)/main.o \
	$(OBJ)/Sound.o \
	$(OBJ)/vgmwrite.o

ALLOBJS = $(ENGINEOBJS) $(MAINOBJS)



all:	smpsplay

smpsplay:	dirs $(ALLOBJS)
	@echo Linking smpsplay ...
	$(CC) $(ALLOBJS) $(LIBS) $(LDFLAGS) -o smpsplay
	@echo Done.

dirs:
	@mkdir -p $(OBJDIRS)

$(OBJ)/%.o: $(SRC)/%.c
	@echo Compiling $< ...
	@$(CC) $(CFLAGS) $(CCFLAGS) -c $< -o $@

$(OBJ)/%.o: $(SRC)/%.cpp
	@echo Compiling $< ...
	@$(CPP) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	@echo Deleting object files ...
	@rm -f $(AUD_MAINOBJS) $(ALL_LIBS) $(ALLOBJS)
	@echo Deleting executable files ...
	@rm -f smpsplay
	@echo Done.

#.PHONY: all clean install uninstall
