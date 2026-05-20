cmake_minimum_required(VERSION 3.15)
project(rf_gateway_project C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_LINUX_APP "Build Linux gateway app" ON)
option(BUILD_QT5_GUI "Build Qt5 C++ dashboard frontend" ON)

if(BUILD_LINUX_APP)
  add_subdirectory(linux_app)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(rf_gateway_core PRIVATE _GNU_SOURCE)
    target_compile_definitions(rf_gateway PRIVATE _GNU_SOURCE)
  endif()
endif()

if(BUILD_QT5_GUI)
  add_subdirectory(qt_gui)
endif()
