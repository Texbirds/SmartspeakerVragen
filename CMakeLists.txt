# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include(C:/esp/esp-adf-v2.7/CMakeLists.txt)
include(C:/esp/v5.1.6/esp-idf/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS C:/esp/v5.1.6/esp-idf/components 
                         C:/esp/esp-idf-lib-0.9.4/components 
                         C:/esp/esp-adf-v2.7/components)



project(living_stream_m3u_aac)