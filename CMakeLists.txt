cmake_minimum_required(VERSION 3.12)	# due to add_compile_definitions
if(POLICY CMP0042)
	cmake_policy(SET CMP0042 NEW)
endif()
if(POLICY CMP0048)
	cmake_policy(SET CMP0048 NEW)
endif()
project(SMPSPlay VERSION 0.5 LANGUAGES C CXX)

find_package(Threads REQUIRED)
find_package(libvgm REQUIRED)

if(MSVC)
	if(NOT MSVC_VERSION LESS 1400)
		add_compile_definitions("_CRT_SECURE_NO_WARNINGS")
	endif()
endif()

if(CMAKE_COMPILER_IS_GNUCC)
	# assume Windows 2000 and later for GetConsoleWindow API call
	if(WIN32)
		add_compile_definitions("_WIN32_WINNT=0x500")
	endif()
	
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
	
	set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
endif(CMAKE_COMPILER_IS_GNUCC)

check_include_file("stdint.h" HAVE_STDINT)
if(HAVE_STDINT)
	add_compile_definitions("HAVE_STDINT_H")
endif()

option(LOOP_DETECTION "enable loop detection" ON)
option(VGM_LOGGING "enable VGM logging feature" ON)

if(LOOP_DETECTION)
	add_compile_definitions("ENABLE_LOOP_DETECTION")
endif(LOOP_DETECTION)

if(VGM_LOGGING)
	add_compile_definitions("ENABLE_VGM_LOGGING")
endif(VGM_LOGGING)

set(ENGINE_HEADERS
	src/Engine/dac.h
	src/Engine/necpcm.h
	src/Engine/smps.h
	src/Engine/smps_commands.h
	src/Engine/smps_int.h
	src/Engine/smps_structs.h
	src/Engine/smps_structs_int.h
)
set(ENGINE_SOURCES
	src/Engine/dac.c
	src/Engine/necpcm.c
	src/Engine/smps.c
	src/Engine/smps_commands.c
	src/Engine/smps_drums.c
	src/Engine/smps_extra.c
)
set(MAIN_HEADERS
	src/ini_lib.h
	src/loader.h
	src/loader_data.h
	src/loader_def.h
	src/loader_ini.h
	src/loader_smps.h
	src/Sound.h
	src/vgmwrite.h
)
set(MAIN_SOURCES
	src/ini_lib.c
	src/loader_data.c
	src/loader_def.c
	src/loader_ini.c
	src/loader_smps.c
	src/main.c
	src/Sound.c
	src/vgmwrite.c
)

if(MSVC)
	# for POSIX directory browsing functions
	set(MAIN_HEADERS ${MAIN_HEADERS} src/dirent.h)
	set(MAIN_SOURCES ${MAIN_SOURCES} src/dirent.c)
endif()

add_executable(${PROJECT_NAME} ${ENGINE_HEADERS} ${ENGINE_SOURCES} ${MAIN_HEADERS} ${MAIN_SOURCES})
#target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBRARIES} libvgm::vgm-utils libvgm::vgm-audio libvgm::vgm-emu)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
