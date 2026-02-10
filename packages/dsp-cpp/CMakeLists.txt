add_library(ff_dsp STATIC
  src/gain.cpp
)

target_include_directories(ff_dsp PUBLIC include)
target_compile_features(ff_dsp PUBLIC cxx_std_20)

