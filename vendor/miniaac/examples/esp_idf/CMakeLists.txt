cmake_minimum_required(VERSION 3.16)
# Component root, relative to this example project.
get_filename_component(MINIAAC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${MINIAAC_ROOT}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(miniaac_sd_example)
