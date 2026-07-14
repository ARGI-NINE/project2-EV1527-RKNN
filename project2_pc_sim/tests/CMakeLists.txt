add_executable(
  rf_protocol_contract_test
  rf_protocol_contract_test.c
  ../common/rf_protocol.c
)
target_include_directories(rf_protocol_contract_test PRIVATE ../common)
add_test(NAME rf_protocol_contract COMMAND rf_protocol_contract_test)
