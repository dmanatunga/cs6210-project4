cmake_minimum_required(VERSION 3.5)
project(project4)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
        rvm_internal.h
        rvm.h
        rvm.cpp)

add_executable(project4 ${SOURCE_FILES})
