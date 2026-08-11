if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are disabled.\n"
    "Use a preset (cmake --preset dev) or cmake -S . -B build.\n"
    "Remove CMakeCache.txt and CMakeFiles/ before configuring again.")
endif()
