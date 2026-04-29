cmake_minimum_required(VERSION 3.16)

# Pull in the improv-wifi-busware library that lives one level up.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(improv_wifi_busware_idf_test)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++20" APPEND)
