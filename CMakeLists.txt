cmake_minimum_required(VERSION 3.16)
project(Metroplex3K LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Platform-independent engine core: no SDL, no OS APIs beyond the C++ standard library.
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
add_library(m3k_core STATIC ${CORE_SRC})
target_include_directories(m3k_core PUBLIC src)
if(MSVC)
  target_compile_options(m3k_core PRIVATE /W4)
else()
  target_compile_options(m3k_core PRIVATE -Wall -Wextra)
endif()

# Tests: plain executables, no framework dependency.
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/test_*.cpp)
foreach(t ${TEST_SRC})
  get_filename_component(name ${t} NAME_WE)
  add_executable(${name} ${t})
  target_link_libraries(${name} PRIVATE m3k_core)
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# Developer tools.
file(GLOB TOOL_SRC CONFIGURE_DEPENDS tools/*.cpp)
foreach(t ${TOOL_SRC})
  get_filename_component(name ${t} NAME_WE)
  add_executable(${name} ${t})
  target_link_libraries(${name} PRIVATE m3k_core)
endforeach()

# Game application (needs SDL2; skipped if not found so core + tests still build).
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  file(GLOB APP_SRC CONFIGURE_DEPENDS src/app/*.cpp)
  add_executable(metroplex3k ${APP_SRC})
  target_link_libraries(metroplex3k PRIVATE m3k_core SDL2::SDL2)
else()
  message(STATUS "SDL2 not found: building core and tests only")
endif()
