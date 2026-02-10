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
add_subdirectory(packages/plugin-host)

if(FF_BUILD_DESKTOP)
  add_subdirectory(apps/desktop)
endif()
