cmake_minimum_required(VERSION 3.10)
project(HeatSimulation)

set(CMAKE_CXX_STANDARD 17)

# Find SQLite3
find_package(SQLite3 REQUIRED)

add_executable(heat_sim 
    main.cpp 
    database.cpp 
    database.hpp
)

target_link_libraries(heat_sim SQLite3::SQLite3)
