cmake_minimum_required(VERSION 3.0)
project(HTTPExample)

add_executable(HTTPExample example.c httplibrary.c)
target_include_directories(HTTPExample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
