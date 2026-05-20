enable_language(CXX)

find_package(Qt5 COMPONENTS Core Gui Widgets QUIET)
if(NOT Qt5_FOUND)
  message(FATAL_ERROR
          "Qt5 Core/Gui/Widgets are required to build rf_dashboard_qt5. Set CMAKE_PREFIX_PATH to your Qt5 installation.")
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

set(
  DASHBOARD_LOCAL_VISION_RUNTIME
  "AUTO"
  CACHE STRING "Build the RK3568 local vision runtime (AUTO, ON, OFF)"
)
set_property(
  CACHE DASHBOARD_LOCAL_VISION_RUNTIME
  PROPERTY STRINGS AUTO ON OFF
)

set(DASHBOARD_ENABLE_LOCAL_VISION_RUNTIME OFF)
set(DASHBOARD_LOCAL_VISION_RUNTIME_REASON
    "rf_dashboard_qt5 requires the RK3568 local vision runtime on Linux; this host build would otherwise fall back to an unsupported disabled-runtime configuration.")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  string(TOUPPER "${DASHBOARD_LOCAL_VISION_RUNTIME}" DASHBOARD_LOCAL_VISION_RUNTIME_MODE)
  if(NOT DASHBOARD_LOCAL_VISION_RUNTIME_MODE STREQUAL "AUTO"
     AND NOT DASHBOARD_LOCAL_VISION_RUNTIME_MODE STREQUAL "ON"
     AND NOT DASHBOARD_LOCAL_VISION_RUNTIME_MODE STREQUAL "OFF")
    message(FATAL_ERROR
            "DASHBOARD_LOCAL_VISION_RUNTIME must be one of AUTO, ON, or OFF")
  endif()

  set(DASHBOARD_LINUX_IS_BOARD OFF)
  if(NOT CMAKE_CROSSCOMPILING AND EXISTS "/proc/device-tree/compatible")
    file(READ "/proc/device-tree/compatible" DASHBOARD_DEVICE_TREE_COMPAT)
    if(DASHBOARD_DEVICE_TREE_COMPAT MATCHES "rockchip,rk356[68]")
      set(DASHBOARD_LINUX_IS_BOARD ON)
    endif()
  endif()

  if(DASHBOARD_LOCAL_VISION_RUNTIME_MODE STREQUAL "ON")
    if(NOT EXISTS "${PROJECT2_MASTER_VISION_ROOT}")
      message(FATAL_ERROR
              "DASHBOARD_LOCAL_VISION_RUNTIME=ON requires ${PROJECT2_MASTER_VISION_ROOT}")
    endif()
    set(DASHBOARD_ENABLE_LOCAL_VISION_RUNTIME ON)
  elseif(DASHBOARD_LOCAL_VISION_RUNTIME_MODE STREQUAL "AUTO")
    if(DASHBOARD_LINUX_IS_BOARD AND EXISTS "${PROJECT2_MASTER_VISION_ROOT}")
      set(DASHBOARD_ENABLE_LOCAL_VISION_RUNTIME ON)
    elseif(NOT EXISTS "${PROJECT2_MASTER_VISION_ROOT}")
      set(DASHBOARD_LOCAL_VISION_RUNTIME_REASON
          "Local RK3568 vision runtime assets are required under ${PROJECT2_MASTER_VISION_ROOT}; the unsupported fallback configuration is no longer allowed.")
    else()
      set(DASHBOARD_LOCAL_VISION_RUNTIME_REASON
          "DASHBOARD_LOCAL_VISION_RUNTIME=AUTO refused to build the unsupported fallback configuration on this non-board Linux host. Configure on the RK3568 target or provide a supported board-side build.")
    endif()
  else()
    set(DASHBOARD_LOCAL_VISION_RUNTIME_REASON
        "DASHBOARD_LOCAL_VISION_RUNTIME=OFF is not supported because it disables the RK3568 runtime and leaves rf_dashboard_qt5 on an unsupported fallback configuration.")
  endif()
endif()

if(DASHBOARD_ENABLE_LOCAL_VISION_RUNTIME)
  string(REPLACE "\\" "\\\\" PROJECT2_MASTER_VISION_ROOT_ESCAPED "${PROJECT2_MASTER_VISION_ROOT}")

  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(AVFORMAT QUIET libavformat)
    pkg_check_modules(AVCODEC QUIET libavcodec)
    pkg_check_modules(AVUTIL QUIET libavutil)
    pkg_check_modules(MOSQUITTO QUIET libmosquitto)
  endif()

  find_library(
    AVFORMAT_LIBRARY
    NAMES avformat
    HINTS ${AVFORMAT_LIBRARY_DIRS}
  )
  find_library(
    AVCODEC_LIBRARY
    NAMES avcodec
    HINTS ${AVCODEC_LIBRARY_DIRS}
  )
  find_library(
    AVUTIL_LIBRARY
    NAMES avutil
    HINTS ${AVUTIL_LIBRARY_DIRS}
  )

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
    message(FATAL_ERROR "libmosquitto is required for the board-side vision MQTT publisher")
  endif()

  if(NOT AVFORMAT_LIBRARY OR NOT AVCODEC_LIBRARY OR NOT AVUTIL_LIBRARY)
    message(FATAL_ERROR "FFmpeg libavformat/libavcodec/libavutil are required for board-side RTSP push")
  endif()

  target_sources(
    rf_dashboard_qt5
    PRIVATE
      ${PROJECT2_MASTER_VISION_ROOT}/src/rkYolov5s.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/preprocess.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/postprocess.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/v4l2_capture.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/mpp_decoder.cc
      ${PROJECT2_MASTER_VISION_ROOT}/src/mpp_encoder_rtsp.cc
  )

  target_include_directories(
    rf_dashboard_qt5
    PRIVATE
      ${PROJECT2_MASTER_VISION_ROOT}/include
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/mpp/include
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rknn/include
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/rga/include
      ${AVFORMAT_INCLUDE_DIRS}
      ${AVCODEC_INCLUDE_DIRS}
      ${AVUTIL_INCLUDE_DIRS}
      ${MOSQUITTO_INCLUDE_DIR}
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
      ${PROJECT2_MASTER_VISION_ROOT}/3rdparty/mpp/lib/librockchip_mpp.so
      ${AVFORMAT_LIBRARY}
      ${AVCODEC_LIBRARY}
      ${AVUTIL_LIBRARY}
      ${MOSQUITTO_LIBRARY}
      Threads::Threads
  )
else()
  message(FATAL_ERROR "${DASHBOARD_LOCAL_VISION_RUNTIME_REASON}")
endif()

target_compile_features(rf_dashboard_qt5 PRIVATE cxx_std_17)
target_link_libraries(rf_dashboard_qt5 PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)

set_target_properties(
  rf_dashboard_qt5
  PROPERTIES
    OUTPUT_NAME "rf_dashboard_qt5"
)
