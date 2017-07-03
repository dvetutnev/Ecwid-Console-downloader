if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there. Thank you.")
endif()

project(Ecwid-Console-downloader CXX C)
cmake_minimum_required(VERSION 3.4)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic")
endif(NOT WIN32)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DRELEASE")

set(PROJECT_CMAKE_MODULES cmake/modules)
set(PROJECT_VENDOR_DIR vendor)
set(PROJECT_INCLUDE_DIR include)
set(PROJECT_SRC_DIR src)
set(PROJECT_TEST_DIR test)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_INCLUDE_DIR})

set(
    CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${CMAKE_SOURCE_DIR}/${PROJECT_CMAKE_MODULES}
)

find_package(UV REQUIRED)
find_package(UVW REQUIRED)
find_package(http-parser REQUIRED)
find_package(GoogleTest)
find_package(Threads)

include_directories(
    ${UV_INCLUDE_DIR}
    ${UVW_INCLUDE_DIR}
    ${http-parser_INCLUDE_DIR}
    ${GoogleTest_INCLUDE_DIRS}
    )

set(SRC_LIST
    main.cpp
    src/task_simple.cpp
    src/on_tick_simple.cpp
    src/http.cpp
    src/aio/tcp_bandwidth.cpp
    src/aio/factory_tcp.cpp
    src/aio/factory_tcp_bandwidth.cpp
)
add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} uv http-parser ${CMAKE_THREAD_LIBS_INIT})

message(STATUS "CMAKE_CURRENT_BINARY_DIR => ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "CMAKE_INSTALL_PREFIX => ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_C_COMPILER => ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER => ${CMAKE_CXX_COMPILER}")

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

