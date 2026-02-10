add_library(ff_engine STATIC
  src/engine.cpp
)

target_include_directories(ff_engine PUBLIC include "${CMAKE_SOURCE_DIR}/packages/abi/include")
target_compile_features(ff_engine PUBLIC cxx_std_20)
target_link_libraries(ff_engine PUBLIC ff_dsp)

add_executable(ff_engine_tests
  tests/engine_tests.cpp
)

target_compile_features(ff_engine_tests PRIVATE cxx_std_20)
target_link_libraries(ff_engine_tests PRIVATE ff_engine)
target_compile_definitions(ff_engine_tests PRIVATE FF_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

add_test(NAME ff_engine_tests COMMAND ff_engine_tests)

add_executable(ff_engine_nonunit_tests
  tests/engine_nonunit_tests.cpp
)

target_compile_features(ff_engine_nonunit_tests PRIVATE cxx_std_20)
target_link_libraries(ff_engine_nonunit_tests PRIVATE ff_engine)
add_test(NAME ff_engine_nonunit_tests COMMAND ff_engine_nonunit_tests)
set_tests_properties(ff_engine_nonunit_tests PROPERTIES
  LABELS "nonunit;stress;golden"
)

add_executable(ff_engine_profile
  tools/engine_profile.cpp
)

target_compile_features(ff_engine_profile PRIVATE cxx_std_20)
target_link_libraries(ff_engine_profile PRIVATE ff_engine)
add_test(NAME ff_engine_profile_smoke
  COMMAND ff_engine_profile --blocks 128 --frames 128 --output "${CMAKE_CURRENT_BINARY_DIR}/engine_profile_smoke.json"
)
set_tests_properties(ff_engine_profile_smoke PROPERTIES
  LABELS "integration;profiling"
)
