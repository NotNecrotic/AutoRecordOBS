cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

if(NOT OS_WINDOWS)
  message(FATAL_ERROR "${_name} currently supports Windows only.")
endif()

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "${_name} requires ENABLE_FRONTEND_API and ENABLE_QT.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Svg)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets Qt6::Svg winhttp
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTORCC ON)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/config.cpp
    src/config.hpp
    src/process-scanner.cpp
    src/process-scanner.hpp
    src/recording-path.cpp
    src/recording-path.hpp
    src/auto-recorder.cpp
    src/auto-recorder.hpp
    src/status-dock.cpp
    src/status-dock.hpp
    src/status-bar-item.cpp
    src/status-bar-item.hpp
    src/capture-target.cpp
    src/capture-target.hpp
    src/settings-dialog.cpp
    src/settings-dialog.hpp
    src/game-picker.cpp
    src/game-picker.hpp
    src/theme.cpp
    src/theme.hpp
    src/update-checker.cpp
    src/update-checker.hpp
    src/resources.qrc
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
