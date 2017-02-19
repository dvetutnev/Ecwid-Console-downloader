find_path(
    UV_DIR
    NAMES autogen.sh
    PATHS ${CMAKE_SOURCE_DIR}/${PROJECT_VENDOR_DIR}/libuv
    NO_DEFAULT_PATH
)

if (NOT UV_DIR)
    message(FATAL_ERROR "libuv not found!")
endif()

message(STATUS "CMAKE_C_COMPILER => ${CMAKE_C_COMPILER}")

set(UV_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/vendor/libuv")
include(ExternalProject)
ExternalProject_Add(
    libuv

    PREFIX ${UV_DIR}
    SOURCE_DIR ${UV_DIR}
    STAMP_DIR "${UV_INSTALL_DIR}/stamp"
    TMP_DIR "${UV_INSTALL_DIR}/tmp"
    LIST_SEPARATOR " "
    BUILD_IN_SOURCE 1

    CONFIGURE_COMMAND ${UV_DIR}/autogen.sh COMMAND ${UV_DIR}/configure --prefix=${UV_INSTALL_DIR} --enable-static --disable-shared CC=${CMAKE_C_COMPILER}
    BUILD_COMMAND make
    INSTALL_COMMAND make install

    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
)

add_library(uv STATIC IMPORTED)
set_target_properties(uv PROPERTIES IMPORTED_LOCATION "${UV_INSTALL_DIR}/lib/libuv.a")
add_dependencies(uv libuv)
#target_include_directories(uv INTERFACE "${UV_DIR}/include")
set_target_properties(uv PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${UV_DIR}/include")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    UV
    REQUIRED_VARS
        UV_DIR
)

