cmake_minimum_required(VERSION 3.20)
project(file-system)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.h tests/*.cpp tests/*.h)

add_executable(file-system ${SRC_FILES})