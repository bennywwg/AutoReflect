cmake_minimum_required(VERSION 3.0.0)
project(AutoReflect VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "-O3")

# Variable holding Resources folder
set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
set(AR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include")

add_executable(AutoReflect Generator.cpp Utilities.cpp Parsing.cpp Generating.cpp)

target_include_directories(AutoReflect PUBLIC ${PROJECT_SOURCE_DIR}/Source/)
target_include_directories(AutoReflect PUBLIC ${PROJECT_SOURCE_DIR}/glm/)
target_include_directories(AutoReflect PUBLIC ${PROJECT_SOURCE_DIR}/json/include/)
set_property(TARGET AutoReflect PROPERTY CXX_STANDARD 20)

# Set AR_RESOURCES_FOLDER Macro
target_compile_definitions(AutoReflect PUBLIC AR_RESOURCES_DIR="${RESOURCES_DIR}")
target_compile_definitions(AutoReflect PUBLIC AR_INCLUDE_DIR="${AR_INCLUDE_DIR}")