find_path(
    UVPP_INCLUDE_DIR
    NAMES uvpp/uvpp.hpp
    PATHS ${CMAKE_SOURCE_DIR}/${PROJECT_VENDOR_DIR}/uvpp/include
    NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    UVPP
    REQUIRED_VARS
        UVPP_INCLUDE_DIR
)
