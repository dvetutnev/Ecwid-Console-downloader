find_path(
    http-parser_DIR
    NAMES http_parser.h http_parser.c
    PATHS ${CMAKE_SOURCE_DIR}/${PROJECT_VENDOR_DIR}/http-parser
    NO_DEFAULT_PATH
)

if (NOT http-parser_DIR)
    message(FATAL_ERROR "http-parser not found!")
endif()

add_library(http-parser STATIC ${http-parser_DIR}/http_parser.c)
target_include_directories(http-parser PUBLIC ${http-parser_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    http-parser
    REQUIRED_VARS
        http-parser_DIR
)
