add_library(ff_plugin_host STATIC
  src/host.cpp
)

target_include_directories(ff_plugin_host PUBLIC
  include
  "${CMAKE_SOURCE_DIR}/packages/abi/include"
)
target_compile_features(ff_plugin_host PUBLIC cxx_std_20)
if(UNIX AND NOT APPLE)
  target_link_libraries(ff_plugin_host PUBLIC dl)
endif()

add_library(ff_test_plugin_valid MODULE
  tests/fixtures/test_plugin_valid.cpp
)
target_compile_features(ff_test_plugin_valid PRIVATE cxx_std_20)
target_include_directories(ff_test_plugin_valid PRIVATE
  include
  "${CMAKE_SOURCE_DIR}/packages/abi/include"
)
set_target_properties(ff_test_plugin_valid PROPERTIES PREFIX "")

add_library(ff_test_plugin_isolated MODULE
  tests/fixtures/test_plugin_isolated.cpp
)
target_compile_features(ff_test_plugin_isolated PRIVATE cxx_std_20)
target_include_directories(ff_test_plugin_isolated PRIVATE
  include
  "${CMAKE_SOURCE_DIR}/packages/abi/include"
)
set_target_properties(ff_test_plugin_isolated PROPERTIES PREFIX "")

add_library(ff_test_plugin_missing_symbols MODULE
  tests/fixtures/test_plugin_missing_symbols.cpp
)
target_compile_features(ff_test_plugin_missing_symbols PRIVATE cxx_std_20)
target_include_directories(ff_test_plugin_missing_symbols PRIVATE
  include
  "${CMAKE_SOURCE_DIR}/packages/abi/include"
)
set_target_properties(ff_test_plugin_missing_symbols PROPERTIES PREFIX "")

add_executable(ff_plugin_host_tests
  tests/host_tests.cpp
)

target_compile_features(ff_plugin_host_tests PRIVATE cxx_std_20)
target_link_libraries(ff_plugin_host_tests PRIVATE ff_plugin_host)
add_dependencies(ff_plugin_host_tests ff_test_plugin_valid ff_test_plugin_isolated
                 ff_test_plugin_missing_symbols)

add_test(NAME ff_plugin_host_tests COMMAND ff_plugin_host_tests)
set_tests_properties(ff_plugin_host_tests PROPERTIES
  ENVIRONMENT "FF_TEST_PLUGIN_VALID=$<TARGET_FILE:ff_test_plugin_valid>;FF_TEST_PLUGIN_ISOLATED=$<TARGET_FILE:ff_test_plugin_isolated>;FF_TEST_PLUGIN_INVALID=$<TARGET_FILE:ff_test_plugin_missing_symbols>"
)
