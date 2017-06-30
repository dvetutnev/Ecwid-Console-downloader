macro(add_test_simple TEST_TARGET)
    set(SRC_LIST ${ARGN})
    add_executable(${TEST_TARGET} "${TEST_TARGET}.cpp" ${SRC_LIST})
    target_link_libraries(${TEST_TARGET} gmock_main uv http-parser)
    add_test(
        NAME ${TEST_TARGET}
        COMMAND ${TEST_TARGET}
    )
endmacro()

add_test_simple(test_task_simple ${CMAKE_SOURCE_DIR}/${PROJECT_SRC_DIR}/task_simple.cpp)
add_test_simple(test_on_tick_simple ${CMAKE_SOURCE_DIR}/${PROJECT_SRC_DIR}/on_tick_simple.cpp)
add_test_simple(test_http_parser_uri ${CMAKE_SOURCE_DIR}/${PROJECT_SRC_DIR}/http.cpp)
add_test_simple(test_http_parser_response ${CMAKE_SOURCE_DIR}/${PROJECT_SRC_DIR}/http.cpp)
add_test_simple(test_uvw_dns)
add_test_simple(test_uvw_timer)
add_test_simple(test_aio_tcp_simple)
add_test_simple(test_aio_tcp_bandwidth ${CMAKE_SOURCE_DIR}/${PROJECT_SRC_DIR}/aio/tcp_bandwidth.cpp)
add_test_simple(test_bandwidth_controller)
add_test_simple(test_downloader_simple)
