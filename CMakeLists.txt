cmake_minimum_required(VERSION 3.16)

project(rkv)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=c++2a -fcoroutines")

add_executable(rkv main.cpp)

target_link_libraries(rkv pthread)
