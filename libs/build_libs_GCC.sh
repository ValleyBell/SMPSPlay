#!/bin/sh

# set GNUMAKEFLAGS=-j4 before running the script to speed up the build on multicore systems

BUILD_DIR="build_GCC"
BASE_DIR="$PWD/download"
INST_DIR="$PWD/install"
if test -n "$MSYSTEM"; then
	GENERATOR="MSYS Makefiles"
fi

# create clean folder structure
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR


# ---- build/install libvgm ---
mkdir -p libvgm
cd libvgm

LIBVGM_CMAKE_OPTS="-D CMAKE_BUILD_TYPE=Release -D BUILD_TESTS=OFF -D BUILD_PLAYER=OFF -D BUILD_VGM2WAV=OFF -D UTIL_CHARSET_CONV=OFF -D UTIL_LOADERS=OFF -D BUILD_LIBAUDIO=ON -D AUDIODRV_PULSE=OFF -D BUILD_LIBEMU=ON -D BUILD_LIBPLAYER=OFF -D SNDEMU__ALL=OFF -D SNDEMU_YM2612_GPGX=ON -D SNDEMU_SN76496_MAME=ON -D SNDEMU_UPD7759_ALL=ON"
if test -z "$GENERATOR"; then
	cmake -D CMAKE_INSTALL_PREFIX="$INST_DIR" $LIBVGM_CMAKE_OPTS "$BASE_DIR/libvgm"
else
	cmake -G "$GENERATOR" -D CMAKE_INSTALL_PREFIX="$INST_DIR" $LIBVGM_CMAKE_OPTS "$BASE_DIR/libvgm"
fi
make install

cd ..


echo "Done."
