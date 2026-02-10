add_library(ff_diagnostics STATIC
  src/reporter.cpp
)

target_include_directories(ff_diagnostics PUBLIC include)
target_compile_features(ff_diagnostics PUBLIC cxx_std_20)

add_executable(ff_diagnostics_tests
  tests/reporter_tests.cpp
)

target_compile_features(ff_diagnostics_tests PRIVATE cxx_std_20)
target_link_libraries(ff_diagnostics_tests PRIVATE ff_diagnostics)
add_test(NAME ff_diagnostics_tests COMMAND ff_diagnostics_tests)
