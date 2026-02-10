add_executable(forest_floor_desktop
  src/main.cpp
)

target_compile_features(forest_floor_desktop PRIVATE cxx_std_20)
target_link_libraries(forest_floor_desktop PRIVATE ff_engine ff_plugin_host)

