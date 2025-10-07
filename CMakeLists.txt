cmake_minimum_required(VERSION 3.14)
project(MyProject LANGUAGES CXX)

function(enable_san t)
  target_compile_options(${t} PRIVATE -g3 -O1 -fno-omit-frame-pointer -fsanitize=address,undefined)
  target_link_options(${t} PRIVATE -fsanitize=address,undefined)
endfunction()

set(CMAKE_REMOVE_ON_FAILURE OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use glob to get all the source files
file(GLOB_RECURSE SOURCE_FILES "echion/*.cc")

# Add source files
add_library(echion SHARED ${SOURCE_FILES})
target_include_directories(echion PUBLIC include)
target_include_directories(echion PUBLIC ${CMAKE_HOME_DIRECTORY})
target_compile_definitions(echion PUBLIC PL_LINUX)
target_include_directories(echion PUBLIC /usr/local/python3/include/python3.11)
target_link_directories(echion PUBLIC /usr/local/python3/lib)
target_link_libraries(echion PUBLIC python3.11 unwind lzma)

# Enable testing
include(CTest)
enable_testing()

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
# For Windows: Prevent overriding parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing and GTest helpers
enable_testing()
include(GoogleTest)

# Collect all test_*.cpp files (auto-updates on reconfigure)
set(TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test")
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${TEST_DIR}/test_*.cpp")

# One target per file
foreach(test_src IN LISTS TEST_SOURCES)
  get_filename_component(test_name "${test_src}" NAME_WE)  # e.g., test_thread_info
  add_executable("${test_name}" "${test_src}" test/test.cpp)

  target_include_directories("${test_name}" PRIVATE "${CMAKE_HOME_DIRECTORY}")
  target_link_libraries("${test_name}" PRIVATE GTest::gtest_main echion)
  target_link_options("${test_name}" PRIVATE -rdynamic)

  # Register with CTest (uses gtest to enumerate cases)
  gtest_discover_tests("${test_name}" DISCOVERY_MODE PRE_TEST)
endforeach()
