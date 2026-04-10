#[[
多行注释！
]]
cmake_minimum_required(VERSION 3.10)

project(MyWebServer)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall)

# aux无法递归搜索
# aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)

#file GLOB GLOB_RECURSE
#file(GLOB SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

file(GLOB SOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)

set(SOURCES main.cpp ${SOURCES})

include_directories(include/http include/logger)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

add_executable (app ${SOURCES})

