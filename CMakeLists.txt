cmake_minimum_required(VERSION 3.20)
project(eafardb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(EAFARDB_DEMOS "Build demo applications (e.g. SCADA historian)" OFF)

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
    src/journal.cpp
    src/view.cpp
    src/derived_view.cpp
)
target_include_directories(eafardb PUBLIC include)
target_link_libraries(eafardb PUBLIC eafar)
target_compile_options(eafardb PRIVATE -Wall -Wextra)

# --- Tests ---
add_executable(eafardb_tests
    tests/main.cpp
    tests/test_table.cpp
    tests/test_pages.cpp
    tests/test_journal.cpp
    tests/test_tx.cpp
    tests/test_view.cpp
    tests/test_chain.cpp
    tests/test_query.cpp
    tests/test_audit.cpp
    tests/test_time.cpp
)
target_link_libraries(eafardb_tests PRIVATE eafardb)
target_compile_options(eafardb_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME eafardb_tests COMMAND eafardb_tests)

# --- Demos ---
if(EAFARDB_DEMOS)
  add_executable(scada_time_travel
      demos/scada_time_travel/main.cpp
  )
  target_link_libraries(scada_time_travel PRIVATE eafardb)
  target_compile_options(scada_time_travel PRIVATE -Wall -Wextra)

  add_executable(galaxy_architect
      demos/galaxy_architect/main.cpp
  )
  target_link_libraries(galaxy_architect PRIVATE eafardb)
  target_compile_options(galaxy_architect PRIVATE -Wall -Wextra)
endif()
