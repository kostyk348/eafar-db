cmake_minimum_required(VERSION 3.20)
project(eafardb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Core dependency (EAFAR paradigm substrate) ---
# Prefer a local checkout for development; fall back to cloning from GitHub.
include(FetchContent)
if(NOT DEFINED EAFAR_SOURCE_DIR)
  set(EAFAR_SOURCE_DIR "C:/Users/Lab/Documents/eafar")
endif()
if(EXISTS "${EAFAR_SOURCE_DIR}/CMakeLists.txt")
  message(STATUS "eafardb: using local eafar core at ${EAFAR_SOURCE_DIR}")
  set(FETCHCONTENT_SOURCE_DIR_EAFAR "${EAFAR_SOURCE_DIR}")
endif()
FetchContent_Declare(eafar
  GIT_REPOSITORY https://github.com/kostyk348/eafar.git
  GIT_TAG main
)
set(EAFAR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EAFAR_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(eafar)

# --- EAFAR-DB: field-oriented event-driven database ---
add_library(eafardb STATIC
    src/table.cpp
)
target_include_directories(eafardb PUBLIC include)
target_link_libraries(eafardb PUBLIC eafar)
target_compile_options(eafardb PRIVATE -Wall -Wextra)

# --- Tests ---
add_executable(eafardb_tests
    tests/main.cpp
    tests/test_table.cpp
    tests/test_pages.cpp
)
target_link_libraries(eafardb_tests PRIVATE eafardb)
target_compile_options(eafardb_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME eafardb_tests COMMAND eafardb_tests)
