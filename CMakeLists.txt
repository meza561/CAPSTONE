cmake_minimum_required(VERSION 3.10)
project(HeatSimulation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# The web application: solver + persistence + CLI.
add_executable(heat_sim
    main.cpp
    database.cpp
    database.hpp
    simulation.hpp
)

# Not every CMake installation's FindSQLite3 defines the imported target -
# some only populate the variables - so link whichever form is available.
# CLion's bundled CMake provides the target; several others do not.
if(TARGET SQLite3::SQLite3)
    target_link_libraries(heat_sim PRIVATE SQLite3::SQLite3)
else()
    target_include_directories(heat_sim PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(heat_sim PRIVATE ${SQLite3_LIBRARIES})
endif()

# The convergence / stability study. Shares the same solver header as the
# application, so the study measures exactly what the app runs. No database
# dependency: it writes JSON and CSV directly.
add_executable(heat_study
    study.cpp
    simulation.hpp
)
