# PlayStation Vita build, on VitaSDK's toolchain file.
#
#   cmake -S platforms/vita -B build/vita && cmake --build build/vita
#   make vita                     the same, from the repository root
#
# Makes one VPK per game. Each is its own LiveArea bubble and reads its
# data from ux0:data/aitd/<folder>.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
    else()
        message(FATAL_ERROR "VITASDK is not set")
    endif()
endif()

project(aitd C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(READ ${ROOT}/VERSION AITD_VERSION)
string(STRIP "${AITD_VERSION}" AITD_VERSION)

# APP_VER is XX.YY: 0.MINOR.PATCH becomes MINOR.PATCH.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${AITD_VERSION}")
if(CMAKE_MATCH_2)
    math(EXPR minor "${CMAKE_MATCH_2}")
    math(EXPR patch "${CMAKE_MATCH_3}")
    if(minor LESS 10)
        set(minor "0${minor}")
    endif()
    if(patch LESS 10)
        set(patch "0${patch}")
    endif()
    set(VITA_VERSION "${minor}.${patch}")
else()
    set(VITA_VERSION "01.00")
endif()

# The engine sources, from the list every platform shares.
file(READ ${ROOT}/sources.mk sources)
string(REGEX MATCH "ENGINE_SRC :=[^\n]*\n([^\n]+\n)+" block "${sources}")
string(REGEX MATCHALL "src/[A-Za-z0-9_/]+\\.c" files "${block}")
set(ENGINE_SRC "")
foreach(file ${files})
    list(APPEND ENGINE_SRC ${ROOT}/${file})
endforeach()

function(aitd_vpk folder title_id name)
    set(exe aitd_${folder})
    add_executable(${exe} ${ENGINE_SRC} ${ROOT}/src/platforms/vita.c
                   ${ROOT}/src/platforms/midi_tsf.c ${ROOT}/src/platforms/pad.c)
    target_include_directories(${exe} PRIVATE ${ROOT}/src)
    set_property(TARGET ${exe} PROPERTY C_STANDARD 99)
    target_compile_definitions(${exe} PRIVATE VITA_GAME_DIR="${folder}"
                               AITD_VERSION="${AITD_VERSION}")
    # -fsigned-char: the engine was written for x86, where char is signed.
    target_compile_options(${exe} PRIVATE -O2 -Wall -fwrapv -fsigned-char
                           -Wno-unused-parameter)
    target_link_libraries(${exe} SceDisplay_stub SceCtrl_stub SceAudio_stub
                          ScePower_stub SceSysmem_stub pthread m)
    vita_create_self(${exe}.self ${exe})
    # LiveArea art made from the player's own data by `make art`, when it
    # is there; without it the package has the system's default look.
    set(art ${ROOT}/build/art/${folder}/vita)
    if(EXISTS ${art}/icon0.png)
        vita_create_vpk(aitd-${folder}.vpk ${title_id} ${exe}.self
                        VERSION ${VITA_VERSION} NAME "${name}"
                        FILE ${art}/icon0.png sce_sys/icon0.png
                             ${art}/bg.png sce_sys/livearea/contents/bg.png
                             ${art}/startup.png sce_sys/livearea/contents/startup.png
                             ${CMAKE_CURRENT_SOURCE_DIR}/template.xml
                             sce_sys/livearea/contents/template.xml)
    else()
        vita_create_vpk(aitd-${folder}.vpk ${title_id} ${exe}.self
                        VERSION ${VITA_VERSION} NAME "${name}")
    endif()
endfunction()

aitd_vpk(INDARK   AITD00001 "Alone in the Dark")
aitd_vpk(JACK     AITD00002 "Jack in the Dark")
aitd_vpk(INDARK2  AITD00003 "Alone in the Dark 2")
aitd_vpk(INDARK3  AITD00004 "Alone in the Dark 3")
aitd_vpk(TIMEGATE AITD00005 "Time Gate: Knight's Chase")
aitd_vpk(KNIGHTS  AITD00006 "Time Gate (Windows)")
