# @OPT_LEVEL=0
# @OPT_TAGS=
# @TYPE_TAGS=audio,image

include_directories(chango ${CMAKE_CURRENT_BINARY_DIR})

add_eff_app(chango
    TYPE exe
    ARCHS all
    TARGETS fabric sim scalar native
    SOURCE
        main.c chango.c camera_swil.c audio_out_swil.c imu_swil.c
        chango_data.h.inc
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/chango_data.h.inc
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen_data.py
        -o ${CMAKE_CURRENT_BINARY_DIR}/chango_data.h.inc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_data.py
)
