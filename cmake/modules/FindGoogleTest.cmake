find_path(
    GoogleTest_DIR
    NAMES CMakeLists.txt
    PATHS ${CMAKE_SOURCE_DIR}/${PROJECT_VENDOR_DIR}/googletest
    NO_DEFAULT_PATH
)

if(NOT IS_DIRECTORY ${GoogleTest_DIR})
    return()
endif()

add_subdirectory(${GoogleTest_DIR})
target_include_directories(gmock_main
  INTERFACE $<TARGET_PROPERTY:gtest_main,INTERFACE_INCLUDE_DIRECTORIES>)
set(GoogleTest_INCLUDE_DIRS
    $<TARGET_PROPERTY:gtest_main,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:gmock_main,INTERFACE_INCLUDE_DIRECTORIES>
    )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    GoogleTest
    REQUIRED_VARS
        GoogleTest_DIR
)
