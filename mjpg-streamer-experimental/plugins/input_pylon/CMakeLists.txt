
find_file(HAVE_PYLONC_H PylonC.h PATHS "/opt/pylon5/include/pylonc")

MJPG_STREAMER_PLUGIN_OPTION(input_pylon "Basler Pylon input plugin"
                            ONLYIF HAVE_PYLONC_H)

if (PLUGIN_INPUT_PYLON)

    add_definitions(-DLINUX -D_GNU_SOURCE)

    include_directories("/opt/pylon5/include")

    find_library(PYLON_LIB
        NAMES pylonc
        HINTS "/opt/pylon5/lib")

    MJPG_STREAMER_PLUGIN_COMPILE(input_pylon input_pylon.c
                                             jpeg_utils.c)

    if (JPEG_LIB)
        target_link_libraries(input_pylon ${JPEG_LIB} ${PYLON_LIB})
    endif (JPEG_LIB)

endif()
