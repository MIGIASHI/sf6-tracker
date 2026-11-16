cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/sf6-filter.cpp
    src/tracker.cpp
    src/tracker.hpp
    src/http.cpp
    src/http.hpp
    src/input.cpp
    src/input.hpp
    src/detector.cpp
    src/detector.hpp
    src/store.cpp
    src/store.hpp
    src/text.cpp
    src/text.hpp
    src/ocr.cpp
    src/ocr.hpp
    src/image.cpp
    src/image.hpp
    src/scene.cpp
    src/scene.hpp
)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

# Text reading: ONNX Runtime (only its headers at build time; the library is loaded by path at run
# time) and the PP-OCRv4 models that the Python tracker uses through RapidOCR 1.4.4.
set(SF6_ORT_DIR "" CACHE PATH "Unpacked ONNX Runtime release (include/ and lib/)")
set(SF6_MODELS_DIR "" CACHE PATH "Folder with ch_PP-OCRv4_det_infer.onnx and ch_PP-OCRv4_rec_infer.onnx")
if(NOT EXISTS "${SF6_ORT_DIR}/include/onnxruntime_cxx_api.h")
  message(FATAL_ERROR "Set SF6_ORT_DIR to an unpacked ONNX Runtime release (see native/README.md)")
endif()
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE third_party "${SF6_ORT_DIR}/include")
if(WIN32)
  set(WASURENE_UPDATE_PLUGIN "sf6-tracker-native")
  set(WASURENE_UPDATE_REPO "MIGIASHI/sf6-tracker")
  set(WASURENE_UPDATE_ASSET "sf6-tracker-native-windows.zip")
  set(WASURENE_UPDATE_TITLE "SF6 Tracker")
  add_subdirectory(updater)
  configure_file(updater/configuration.hpp.in updater/configuration.hpp @ONLY)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE updater/obs-updater.cpp)
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/updater")
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE wasurene_update_core comctl32)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE $<$<BOOL:${WASURENE_UPDATER_TESTING}>:WASURENE_UPDATER_TESTING>)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  add_executable(tracker_api_check EXCLUDE_FROM_ALL tests/tracker_api_check.cpp src/tracker.cpp src/http.cpp
    src/input.cpp src/detector.cpp src/store.cpp src/text.cpp src/ocr.cpp src/image.cpp src/scene.cpp)
  target_compile_features(tracker_api_check PRIVATE cxx_std_17)
  target_include_directories(tracker_api_check PRIVATE third_party "${SF6_ORT_DIR}/include")
  target_compile_definitions(tracker_api_check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(tracker_api_check PRIVATE ws2_32 xinput9_1_0 dinput8 dxguid ole32 user32)
  set_target_properties(tracker_api_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  add_executable(ocr_load_check EXCLUDE_FROM_ALL tests/ocr_load_check.cpp src/ocr.cpp src/image.cpp src/scene.cpp)
  target_compile_features(ocr_load_check PRIVATE cxx_std_17)
  target_include_directories(ocr_load_check PRIVATE third_party "${SF6_ORT_DIR}/include")
  target_compile_definitions(ocr_load_check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  set_target_properties(ocr_load_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 xinput9_1_0 dinput8 dxguid ole32 user32)
  install(
    FILES "${SF6_ORT_DIR}/lib/onnxruntime.dll"
    DESTINATION "${CMAKE_PROJECT_NAME}/data/ort"
    RENAME sf6-onnxruntime.dll
  )
endif()
install(
  FILES "${SF6_MODELS_DIR}/ch_PP-OCRv4_det_infer.onnx" "${SF6_MODELS_DIR}/ch_PP-OCRv4_rec_infer.onnx"
  DESTINATION "${CMAKE_PROJECT_NAME}/data/models"
)
# The overlay, control panel and input display pages are the Python version's, unchanged.
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../sf6tracker/web/control.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sf6tracker/web/overlay.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sf6tracker/web/input.html"
  DESTINATION "${CMAKE_PROJECT_NAME}/data/web"
)
install(
  FILES third_party/nlohmann/LICENSE.MIT
  DESTINATION "${CMAKE_PROJECT_NAME}/data/licenses"
  RENAME nlohmann-json-LICENSE.MIT
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
