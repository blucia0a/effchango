# @OPT_LEVEL=0
# @OPT_TAGS=
# @TYPE_TAGS=audio,image

include_directories(chango ${CMAKE_CURRENT_BINARY_DIR})

option(EFFCHANGO_HW_CAMERA    "Use HM0360 hardware camera instead of SWIL stub" OFF)
option(EFFCHANGO_CAMERA_DEBUG "Stream raw camera frames over UART (skips audio)" OFF)

# The hardware path replaces main.c wholesale with main_hm0360.c, which keeps the
# known-good minimal-camera bring-up verbatim and splices chango into its capture
# loop. The SWIL path keeps main.c and the camera_t abstraction. See CLAUDE.md.
if(EFFCHANGO_HW_CAMERA)
    set(EFFCHANGO_MAIN_SRC main_hm0360.c)
    set(EFFCHANGO_CAMERA_SRC hm0360.c)
    set(EFFCHANGO_CAMERA_DEFS USE_HW_CAMERA)
else()
    set(EFFCHANGO_MAIN_SRC main.c)
    set(EFFCHANGO_CAMERA_SRC camera_swil.c)
    set(EFFCHANGO_CAMERA_DEFS)
endif()

if(EFFCHANGO_CAMERA_DEBUG)
    list(APPEND EFFCHANGO_CAMERA_DEFS CAMERA_DEBUG)
endif()

add_eff_app(chango
    TYPE exe
    ARCHS all
    TARGETS fabric sim scalar native
    SOURCE
        ${EFFCHANGO_MAIN_SRC} chango.c ${EFFCHANGO_CAMERA_SRC} audio_out_swil.c imu_swil.c
        chango_data.h.inc
)

if(EFFCHANGO_CAMERA_DEFS)
    target_compile_definitions(chango PRIVATE ${EFFCHANGO_CAMERA_DEFS})
endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/chango_data.h.inc
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen_data.py
        -o ${CMAKE_CURRENT_BINARY_DIR}/chango_data.h.inc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_data.py
)
