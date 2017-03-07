if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there. Thank you.")
endif()

project(Ecwid-Console-downloader CXX C)
cmake_minimum_required(VERSION 3.4)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif(NOT WIN32)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DRELEASE")

set(PROJECT_CMAKE_MODULES cmake/modules)
set(PROJECT_VENDOR_DIR vendor)
set(PROJECT_INCLUDE_DIR include)
set(PROJECT_SRC_DIR src)
set(PROJECT_TEST_DIR test)

#set(PROJECT_RUNTIME_OUTPUT_DIRECTORY bin)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_RUNTIME_OUTPUT_DIRECTORY})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_INCLUDE_DIR})

set(
    CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${CMAKE_SOURCE_DIR}/${PROJECT_CMAKE_MODULES}
)

find_package(UV REQUIRED)
find_package(UVPP REQUIRED)
find_package(http-parser REQUIRED)
find_package(GoogleTest)

include_directories(${UVPP_INCLUDE_DIR})

set(SRC_LIST
    main.cpp
)
add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} uv)

message(STATUS "CMAKE_CURRENT_BINARY_DIR => ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "CMAKE_INSTALL_PREFIX => ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_C_COMPILER => ${CMAKE_C_COMPILER}")

if(${GoogleTest_FOUND})
    message(STATUS "Enable test")
    include(CTest)
    add_subdirectory(test)
endif()

file(
    GLOB_RECURSE PROJECT_FILES FOLLOW_SYMLINKS
    *.txt *.c *.cpp *.hpp *.h *.in *.cmake *.sh *.md AUTHORS
)

add_custom_target(
    QTCREATOR_FALLBACK ALL
    COMMENT "Feel free to ignore this target, but please do not remove it."
    SOURCES ${PROJECT_FILES}
)
