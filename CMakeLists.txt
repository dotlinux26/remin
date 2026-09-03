cmake_minimum_required(VERSION 3.24)

project(remin
  VERSION 0.1.0
  DESCRIPTION "Remin - Remember your work. Linux-native CLI workspace application."
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(REMIN_BUILD_GUI "Build GUI frontend (requires GTK/VTE)" ON)
option(REMIN_BUILD_TESTS "Build tests" ON)
option(REMIN_ENABLE_SANITIZERS "Enable ASan/UBSan" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Dependencies)
include(Sanitizers)
include(Install)

add_subdirectory(src/core)
add_subdirectory(src/storage)
add_subdirectory(src/terminal)
add_subdirectory(src/ipc)
add_subdirectory(src/cli)

if(REMIN_BUILD_GUI)
  add_subdirectory(src/gui)
endif()

add_subdirectory(src/app)

if(REMIN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
