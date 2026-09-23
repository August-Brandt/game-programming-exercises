cmake_minimum_required(VERSION 4.0)

project(gp26_exercises)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

set(BUILD_SHARED_LIBS OFF)

# 3rd party libraries
add_subdirectory(lib/SDL)
add_subdirectory(lib/imgui)
add_subdirectory(lib/box2d)

# repo's executables
add_subdirectory(examples)
add_subdirectory(playground)
add_subdirectory(exercises)
add_subdirectory(exercises_workplace)
add_subdirectory(exercises_solutions)

# ugly hack to avoid setting the working directory for VSCode (which works for debuggin only)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/data ${CMAKE_BINARY_DIR}/data SYMBOLIC)
if(EXISTS ${CMAKE_BINARY_DIR}/Debug)
    file(CREATE_LINK ${CMAKE_SOURCE_DIR}/data ${CMAKE_BINARY_DIR}/Debug/data SYMBOLIC)
endif()

if(EXISTS ${CMAKE_BINARY_DIR}/Release)
    file(CREATE_LINK ${CMAKE_SOURCE_DIR}/data ${CMAKE_BINARY_DIR}/Release/data SYMBOLIC)
endif()

