cmake_minimum_required(VERSION 3.15)
project(rf_gateway_project C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_SIMULATOR "Build RF pulse simulator" ON)
option(BUILD_LINUX_APP "Build Linux gateway app" ON)

if(BUILD_LINUX_APP)
  add_subdirectory(linux_app)
endif()

if(BUILD_SIMULATOR)
  add_subdirectory(simulator)
endif()

