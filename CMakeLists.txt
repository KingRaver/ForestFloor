cmake_minimum_required(VERSION 3.28)

project(ForestFloor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# We are not using C++20 modules yet; disabling scan avoids clang-scan-deps issues on mixed local toolchains.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

enable_testing()

option(FF_BUILD_DESKTOP "Build the standalone desktop host stub." ON)

add_subdirectory(packages/dsp-cpp)
add_subdirectory(packages/engine-cpp)
add_subdirectory(packages/diagnostics-cpp)
add_subdirectory(packages/plugin-host)

if(FF_BUILD_DESKTOP)
  add_subdirectory(apps/desktop)
endif()

set(FF_RELEASE_VERSION "${PROJECT_VERSION}" CACHE STRING
  "Release version used for packaged installers."
)

set(CPACK_PACKAGE_NAME "forest-floor")
set(CPACK_PACKAGE_VENDOR "Forest Floor")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Forest Floor standalone drum machine host")
set(CPACK_PACKAGE_VERSION "${FF_RELEASE_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
  "forest-floor-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES ON)
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
