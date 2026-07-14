if(UNIX)
  add_executable(
    rf_decode_contract_test
    rf_decode_contract_test.c
    ../linux_app/rf_decode.c
    ../linux_app/rf_decode_c.c
    ../common/rf_protocol.c
  )
  target_compile_definitions(rf_decode_contract_test PRIVATE _POSIX_C_SOURCE=200809L)
  target_include_directories(rf_decode_contract_test PRIVATE ../linux_app ../common)
  target_link_libraries(rf_decode_contract_test PRIVATE m)
  add_test(NAME rf_decode_contract COMMAND rf_decode_contract_test)
endif()

enable_language(CXX)
find_package(Threads REQUIRED)
add_executable(
  postprocess_labels_lifetime_test
  postprocess_labels_lifetime_test.cc
  ../third_party/rknn_yolov5_rk3568/src/postprocess.cc
)
target_compile_features(postprocess_labels_lifetime_test PRIVATE cxx_std_11)
target_include_directories(
  postprocess_labels_lifetime_test
  PRIVATE
  ../third_party/rknn_yolov5_rk3568/include
)
if(UNIX)
  target_link_libraries(postprocess_labels_lifetime_test PRIVATE m)
endif()
target_link_libraries(postprocess_labels_lifetime_test PRIVATE Threads::Threads)
add_test(
  NAME postprocess_labels_lifetime
  COMMAND
    postprocess_labels_lifetime_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rknn_yolov5_rk3568/model/coco_80_labels_list.txt
)
