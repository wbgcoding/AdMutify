add_executable(FakeSpotify WIN32 fake_spotify.cpp)
admutify_common(FakeSpotify)
set_target_properties(FakeSpotify PROPERTIES
  OUTPUT_NAME Spotify
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rig/$<CONFIG>")
target_link_libraries(FakeSpotify PRIVATE ole32 shell32 user32)

add_executable(SessionProbe session_probe.cpp)
admutify_common(SessionProbe)
set_target_properties(SessionProbe PROPERTIES
  OUTPUT_NAME session_probe
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rig/$<CONFIG>")
target_link_libraries(SessionProbe PRIVATE ole32)
