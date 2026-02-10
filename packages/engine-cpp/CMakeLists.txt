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

add_test(NAME ff_engine_tests COMMAND ff_engine_tests)
