add_library(ff_engine STATIC
  src/engine.cpp
)

target_include_directories(ff_engine PUBLIC include)
target_compile_features(ff_engine PUBLIC cxx_std_20)
target_link_libraries(ff_engine PUBLIC ff_dsp)

