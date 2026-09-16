cmake_minimum_required(VERSION 3.10)
project(HeatSimulation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SQLite3 REQUIRED)

# OpenMP is optional. Apple's clang does not ship it, so the build must work
# without it - the pragmas are simply ignored and everything runs serially.
# On macOS: brew install libomp
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found - parallel step loops enabled")
else()
    message(STATUS "OpenMP not found - building serial (this is fine)")
endif()

function(link_openmp target)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${target} PRIVATE OpenMP::OpenMP_CXX)
    endif()
endfunction()

# The web application: solver + persistence + CLI.
add_executable(heat_sim
    main.cpp
    database.cpp
    database.hpp
    simulation.hpp
    reaction.hpp
)

# Not every CMake installation's FindSQLite3 defines the imported target -
# some only populate the variables - so link whichever form is available.
if(TARGET SQLite3::SQLite3)
    target_link_libraries(heat_sim PRIVATE SQLite3::SQLite3)
else()
    target_include_directories(heat_sim PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(heat_sim PRIVATE ${SQLite3_LIBRARIES})
endif()
link_openmp(heat_sim)

# Convergence / stability study. Shares the same solver headers as the app.
add_executable(heat_study study.cpp simulation.hpp reaction.hpp)
link_openmp(heat_study)

# Physics regression tests. Asserts the exact analytical values, convergence
# orders, stability threshold and conservation laws.
add_executable(heat_tests tests.cpp simulation.hpp reaction.hpp)
link_openmp(heat_tests)

enable_testing()
add_test(NAME physics COMMAND heat_tests)
