add_library(ff_plugin_host STATIC
  src/host.cpp
)

target_include_directories(ff_plugin_host PUBLIC include)
target_compile_features(ff_plugin_host PUBLIC cxx_std_20)

