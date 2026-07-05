find_package(Qt6 6.5 REQUIRED COMPONENTS Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(ava-desktop
  "desktop_controller.cpp"
  "desktop_controller.h"
  "main.cpp"
)

set_target_properties(ava-desktop PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)

qt_add_qml_module(ava-desktop
  URI Ava.Desktop
  VERSION 1.0
  QML_FILES
    "qml/Main.qml"
)

target_link_libraries(ava-desktop
  PRIVATE
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

target_include_directories(ava-desktop
  PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${AVA_GENERATED_INCLUDE_DIR}"
)

ava_apply_warnings(ava-desktop)
ava_apply_sanitizers(ava-desktop)
