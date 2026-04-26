enable_language(CXX)

find_package(Qt5 COMPONENTS Core Gui Widgets QUIET)
if(NOT Qt5_FOUND)
  message(WARNING "Qt5 not found. Skip target rf_dashboard_qt5. Set CMAKE_PREFIX_PATH to your Qt5 installation.")
  return()
endif()

set(QT_GUI_SOURCES
  app/main.cpp
  app/main_window.cpp
  core/app_palette.cpp
  core/dashboard_backend.cpp
  rf/rf_gateway_client.cpp
  rf/rf_status_page.cpp
  log/system_log_page.cpp
  vision/vision_page.cpp
  vision/vision_runtime.cpp
  widgets/waveform_widget.cpp
)

add_executable(rf_dashboard_qt5 ${QT_GUI_SOURCES})

target_include_directories(
  rf_dashboard_qt5
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/rf
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/vision
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
)

get_filename_component(
  PROJECT2_MASTER_VISION_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rknn_yolov5_rk3568"
  ABSOLUTE
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${PROJECT2_MASTER_VISION_ROOT}")
  string(REPLACE "\\" "\\\\" PROJECT2_MASTER_VISION_ROOT_ESCAPED "${PROJECT2_MASTER_VISION_ROOT}")

  target_sources(
    rf_dashboard_qt5
    PRIVATE
      ${PROJECT2_MASTER_VISION_ROOT}/src/rkYolov5s.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/preprocess.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/postprocess.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/v4l2_capture.cc
  )

  target_include_directories(
    rf_dashboard_qt5
    PRIVATE
      ${PROJECT2_MASTER_VISION_ROOT}/include
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rknn/include
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rga/include
  )

  target_compile_definitions(
    rf_dashboard_qt5
    PRIVATE
      DASHBOARD_HAVE_LOCAL_VISION_RUNTIME=1
      DASHBOARD_LOCAL_VISION_ROOT="${PROJECT2_MASTER_VISION_ROOT_ESCAPED}"
  )

  find_package(Threads REQUIRED)
  target_link_libraries(
    rf_dashboard_qt5
    PRIVATE
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rknn/lib/librknnrt.so
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rga/lib/librga.so
      Threads::Threads
  )
else()
  message(STATUS "Local RK3568 vision runtime assets are not available inside project2_master; rf_dashboard_qt5 will use the stub status path on this host.")
endif()

target_compile_features(rf_dashboard_qt5 PRIVATE cxx_std_17)
target_link_libraries(rf_dashboard_qt5 PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)

set_target_properties(
  rf_dashboard_qt5
  PROPERTIES
    OUTPUT_NAME "rf_dashboard_qt5"
)
