cmake_minimum_required(VERSION 3.10)

project(ddplugin-organizer)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(EXT_FILES
    ${CMAKE_SOURCE_DIR}/src/plugins/desktop/desktoputils/widgetutil.h
    )

FILE(GLOB_RECURSE CORE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.json"
)

set(BIN_NAME dd-organizer-plugin)

# Include plugin configuration
include(DFMPluginConfig)

# 指定资源文件
set(QRC_FILE
    resources/images.qrc
)

qt_add_resources(QRC_RESOURCES ${QRC_FILE})

add_library(${BIN_NAME}
    SHARED
    ${CORE_FILES}
    ${EXT_FILES}
    ${QRC_RESOURCES}
)

set_target_properties(${BIN_NAME} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${DFM_BUILD_PLUGIN_DESKTOP_DIR})

# Use default plugin configuration
dfm_apply_default_plugin_config(${BIN_NAME})

install(TARGETS ${BIN_NAME}
    LIBRARY DESTINATION ${DFM_PLUGIN_DESKTOP_CORE_DIR}
)

# install dconfig files.
INSTALL_DCONFIG("org.deepin.dde.file-manager.desktop.organizer.json")
