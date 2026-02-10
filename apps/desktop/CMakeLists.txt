add_executable(forest_floor_desktop
  src/main.cpp
)

target_compile_features(forest_floor_desktop PRIVATE cxx_std_20)
target_link_libraries(forest_floor_desktop PRIVATE ff_diagnostics ff_engine ff_plugin_host)

install(TARGETS forest_floor_desktop
  RUNTIME DESTINATION bin
)

install(FILES
  "${CMAKE_SOURCE_DIR}/README.md"
  "${CMAKE_SOURCE_DIR}/docs/community/SECURITY.md"
  "${CMAKE_SOURCE_DIR}/docs/project/ROADMAP.md"
  DESTINATION share/doc
)
