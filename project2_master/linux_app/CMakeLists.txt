add_library(
  rf_gateway_core
  STATIC
  rf_epoll.c
  rf_decode.c
  rf_decode_c.c
  rf_source.c
)

target_include_directories(rf_gateway_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../common ../linux_driver)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(MOSQUITTO QUIET libmosquitto)
endif()

find_path(
  MOSQUITTO_INCLUDE_DIR
  NAMES mosquitto.h
  HINTS ${MOSQUITTO_INCLUDE_DIRS}
)
find_library(
  MOSQUITTO_LIBRARY
  NAMES mosquitto
  HINTS ${MOSQUITTO_LIBRARY_DIRS}
)

if(NOT MOSQUITTO_INCLUDE_DIR OR NOT MOSQUITTO_LIBRARY)
  message(FATAL_ERROR "libmosquitto is required for rf_gateway MQTT publishing")
endif()

add_executable(
  rf_gateway
  main.c
  mqtt_publisher.c
)

target_include_directories(rf_gateway PRIVATE ${MOSQUITTO_INCLUDE_DIR})
target_link_libraries(rf_gateway PRIVATE rf_gateway_core ${MOSQUITTO_LIBRARY})

if(UNIX)
  target_link_libraries(rf_gateway PRIVATE m)
endif()
