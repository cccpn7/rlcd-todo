cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rlcd_todo)

esptool_py_flash_to_partition(flash "fonts" "${CMAKE_CURRENT_SOURCE_DIR}/assets/font.bin")
