cmake_minimum_required(VERSION 3.16)

# Set all required extra component paths (IDF, ADF, IDF-lib)
set(EXTRA_COMPONENT_DIRS
    C:/esp/v5.1.6/esp-idf/components
    C:/esp/esp-idf-lib-0.9.4/components
    C:/esp/esp-adf-v2.7/components
)

# Include the project CMake logic from IDF (must be after setting EXTRA_COMPONENT_DIRS)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set the project name
project(living_stream_m3u_aac)
