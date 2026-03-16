add_executable(rf_simulator rf_simulator.c ../common/rf_protocol.c)
target_include_directories(rf_simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ../common)

