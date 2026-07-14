if(WIN32)
  add_library(rf_common OBJECT ../common/rf_protocol.c)
  add_library(
    rf_gateway_core
    OBJECT
    rf_epoll.c
    rf_decode.c
    rf_decode_c.c
    rf_source.c
  )
else()
  add_library(rf_common STATIC ../common/rf_protocol.c)
  add_library(
    rf_gateway_core
    STATIC
    rf_epoll.c
    rf_decode.c
    rf_decode_c.c
    rf_source.c
  )
  target_link_libraries(rf_gateway_core PUBLIC rf_common)
endif()

target_include_directories(rf_common PUBLIC ../common)
target_include_directories(rf_gateway_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ../common)

if(WIN32)
  add_executable(
    rf_gateway
    main.c
    $<TARGET_OBJECTS:rf_gateway_core>
    $<TARGET_OBJECTS:rf_common>
  )
else()
  add_executable(rf_gateway main.c)
  target_link_libraries(rf_gateway PRIVATE rf_gateway_core rf_common)
endif()

if(UNIX)
  target_link_libraries(rf_gateway PRIVATE m)
endif()
