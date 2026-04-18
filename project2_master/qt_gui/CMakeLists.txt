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
  core/rf_utils.cpp
  rf/rf_gateway_client.cpp
  rf/rf_status_page.cpp
  log/system_log_page.cpp
  vision/vision_page.cpp
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

target_compile_features(rf_dashboard_qt5 PRIVATE cxx_std_17)
target_link_libraries(rf_dashboard_qt5 PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)

set_target_properties(
  rf_dashboard_qt5
  PROPERTIES
    OUTPUT_NAME "rf_dashboard_qt5"
)
