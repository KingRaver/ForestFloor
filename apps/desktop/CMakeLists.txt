set(FF_DESKTOP_SOURCES
  src/main.cpp
  src/runtime.cpp
  src/project_io.cpp
  src/sample_loader.cpp
  src/audio_backend_stub.cpp
  src/midi_backend_stub.cpp
)

if(APPLE)
  list(APPEND FF_DESKTOP_SOURCES
    src/audio_backend_coreaudio.mm
    src/midi_backend_coremidi.mm
    src/macos_ui.mm
  )
endif()

if(APPLE)
  list(APPEND FF_DESKTOP_SOURCES "${FF_APP_ICON_ICNS}")
  add_executable(forest_floor_desktop MACOSX_BUNDLE ${FF_DESKTOP_SOURCES})
else()
  add_executable(forest_floor_desktop ${FF_DESKTOP_SOURCES})
endif()

target_compile_features(forest_floor_desktop PRIVATE cxx_std_20)
target_link_libraries(forest_floor_desktop PRIVATE ff_diagnostics ff_engine ff_plugin_host)
target_compile_definitions(forest_floor_desktop PRIVATE FF_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

if(APPLE)
  find_library(FF_FRAMEWORK_COCOA Cocoa REQUIRED)
  find_library(FF_FRAMEWORK_AUDIOUNIT AudioUnit REQUIRED)
  find_library(FF_FRAMEWORK_AUDIOTOOLBOX AudioToolbox REQUIRED)
  find_library(FF_FRAMEWORK_COREAUDIO CoreAudio REQUIRED)
  find_library(FF_FRAMEWORK_COREMIDI CoreMIDI REQUIRED)
  find_library(FF_FRAMEWORK_COREFOUNDATION CoreFoundation REQUIRED)
  find_library(FF_FRAMEWORK_QUARTZCORE QuartzCore REQUIRED)

  set_source_files_properties("${FF_APP_ICON_ICNS}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  set_target_properties(forest_floor_desktop PROPERTIES
    OUTPUT_NAME "${FF_APP_DISPLAY_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "${FF_APP_DISPLAY_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${FF_APP_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${FF_RELEASE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${FF_RELEASE_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "forest-floor.icns"
  )

  target_link_libraries(forest_floor_desktop PRIVATE
    "${FF_FRAMEWORK_COCOA}"
    "${FF_FRAMEWORK_AUDIOUNIT}"
    "${FF_FRAMEWORK_AUDIOTOOLBOX}"
    "${FF_FRAMEWORK_COREAUDIO}"
    "${FF_FRAMEWORK_COREMIDI}"
    "${FF_FRAMEWORK_COREFOUNDATION}"
    "${FF_FRAMEWORK_QUARTZCORE}"
  )
endif()

if(WIN32)
  # RC string literals treat backslashes as escapes (\a, \b, ...), so pass
  # a CMake-style path with forward slashes.
  file(TO_CMAKE_PATH "${FF_APP_ICON_ICO}" FF_APP_ICON_ICO_RC_PATH)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/forest_floor_desktop.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/forest_floor_desktop.rc"
    @ONLY
  )
  target_sources(forest_floor_desktop PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/forest_floor_desktop.rc"
  )
endif()

install(TARGETS forest_floor_desktop
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)

install(DIRECTORY
  "${CMAKE_SOURCE_DIR}/assets/starter-kit"
  DESTINATION share/forest-floor
)

install(FILES
  "${CMAKE_SOURCE_DIR}/README.md"
  "${CMAKE_SOURCE_DIR}/docs/community/SECURITY.md"
  "${CMAKE_SOURCE_DIR}/docs/project/ROADMAP.md"
  "${CMAKE_SOURCE_DIR}/docs/project/ICON.md"
  DESTINATION share/doc
)

add_test(NAME ff_desktop_headless_smoke
  COMMAND forest_floor_desktop --headless-smoke
)
set_tests_properties(ff_desktop_headless_smoke PROPERTIES
  LABELS "integration;desktop"
)

add_test(NAME ff_desktop_headless_soak
  COMMAND forest_floor_desktop --headless-soak
)
set_tests_properties(ff_desktop_headless_soak PROPERTIES
  LABELS "nonunit;desktop;soak"
)

add_executable(ff_desktop_unit_tests
  tests/project_and_sample_tests.cpp
  src/project_io.cpp
  src/sample_loader.cpp
)
target_compile_features(ff_desktop_unit_tests PRIVATE cxx_std_20)
target_link_libraries(ff_desktop_unit_tests PRIVATE ff_engine)
target_include_directories(ff_desktop_unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(ff_desktop_unit_tests PRIVATE FF_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME ff_desktop_unit_tests
  COMMAND ff_desktop_unit_tests
)
set_tests_properties(ff_desktop_unit_tests PROPERTIES
  LABELS "unit;desktop"
)
