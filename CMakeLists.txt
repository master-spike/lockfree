cmake_minimum_required(VERSION 3.11)

project(masterspike_lockfree)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)

include_directories(PUBLIC ./include)

add_subdirectory(test)

