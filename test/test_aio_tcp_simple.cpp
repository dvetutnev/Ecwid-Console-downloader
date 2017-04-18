#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "aio_loop_mock.h"
#include "aio_tcp_handle_mock.h"
#include "aio_uvw.h"
#include "aio_uvw_tcp.h"

using namespace std;

using ::testing::_;
using ::testing::Return;
using ::testing::Mock;
using ::testing::InSequence;

struct AIO_Mock
{
    using Loop = LoopMock;
    using TcpHandle = TcpHandleMock;

    using ErrorEvent = AIO_UVW::ErrorEvent;
    using ConnectEvent = AIO_UVW::ConnectEvent;
    using DataEvent = AIO_UVW::DataEvent;
    using EndEvent = AIO_UVW::EndEvent;
    using WriteEvent = AIO_UVW::WriteEvent;
};

TEST(TCPSocketWrapperSimple, create_and_close)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    {   InSequence dummy;
        EXPECT_CALL( *loop, resource_TcpHandleMock() )
                .WillOnce( Return(tcp_handle) );
        EXPECT_CALL( *tcp_handle, close() )
                .Times(1);
    }

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    resource->close();

    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, TcpHandle_is_null)
{
    auto loop = make_shared<LoopMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(nullptr) );
    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_FALSE(resource);
}

TEST(TCPSocketWrapperSimple, connect_failed)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    bool cb_called = false;
    AIO_UVW::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    resource->once<AIO_UVW::ErrorEvent>( [&cb_called, &event, &resource](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<AIO_UVW::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    const string ip = "127.0.0.1";
    const unsigned short port = 8080;
    EXPECT_CALL( *tcp_handle, connect(ip, port) )
            .Times(1);
    resource->connect(ip, port);
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    tcp_handle->publish(event);
    ASSERT_TRUE(cb_called);
}

TEST(TCPSocketWrapperSimple, connect6_failed)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    bool cb_called = false;
    AIO_UVW::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    resource->once<AIO_UVW::ErrorEvent>( [&cb_called, &event, &resource](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<AIO_UVW::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    const string ip = "::1";
    const unsigned short port = 8080;
    EXPECT_CALL( *tcp_handle, connect6(ip, port) )
            .Times(1);
    resource->connect6(ip, port);
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    tcp_handle->publish(event);
    ASSERT_TRUE(cb_called);
}

TEST(TCPSocketWrapperSimple, connect_normal)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    bool cb_called = false;
    resource->once<AIO_UVW::ConnectEvent>( [&cb_called, &resource](const auto&, auto& handle)
    {
        cb_called = true;
        auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<AIO_UVW::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, connect(_,_) )
            .Times(1);
    resource->connect("127.0.0.1", 8080);
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    tcp_handle->publish( AIO_UVW::ConnectEvent{} );
    ASSERT_TRUE(cb_called);
}

TEST(TCPSocketWrapperSimple, read_failed)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    AIO_UVW::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    bool cb_called = false;
    resource->once<AIO_UVW::ErrorEvent>( [&cb_called, &event, &resource](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<AIO_UVW::ConnectEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::EndEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::WriteEvent>( [](auto&, auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, read() )
            .Times(1);
    resource->read();
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    tcp_handle->publish(event);
    ASSERT_TRUE(cb_called);
}

TEST(TCPSocketWrapperSimple, read_EOF)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    bool cb_called = false;
    resource->once<AIO_UVW::EndEvent>( [&cb_called, &resource](const auto&, auto& handle)
    {
        cb_called = true;
        auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<AIO_UVW::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<AIO_UVW::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, read() )
            .Times(1);
    resource->read();
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    tcp_handle->publish( AIO_UVW::EndEvent{} );
    ASSERT_TRUE(cb_called);
}

TEST(TCPSocketWrapperSimple, read_normal)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    bool cb_called = false;
    const std::size_t len = 42;
    char* raw_data_ptr = new char[len];
    resource->once<AIO_UVW::DataEvent>( [&cb_called, &raw_data_ptr, &len, &resource](auto& event, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ(event.data.get(), raw_data_ptr);
        ASSERT_EQ(event.length, len);

        auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);

    } );
    resource->once<AIO_UVW::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::ConnectEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::EndEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::WriteEvent>( [](const auto&, auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, read() )
            .Times(1);
    resource->read();
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    tcp_handle->publish( AIO_UVW::DataEvent{std::unique_ptr<char[]>{raw_data_ptr}, len} );
    ASSERT_TRUE(cb_called);
}

TEST(TCPSocketWrapperSimple, read_stop)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    resource->once<AIO_UVW::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::ConnectEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::EndEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::WriteEvent>( [](const auto&, auto&) { FAIL(); } );

    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, read() )
                .Times(1);
        EXPECT_CALL( *tcp_handle, stop() )
                .Times(1);
    }

    resource->read();
    resource->stop();
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, write_normal)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(tcp_handle) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());

    bool cb_called = false;
    resource->once<AIO_UVW::WriteEvent>( [&cb_called, &resource](const auto&, auto& handle)
    {
        cb_called = true;
        auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<AIO_UVW::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::ConnectEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<AIO_UVW::EndEvent>( [](const auto&, auto&) { FAIL(); } );

    const unsigned int len = 42;
    char* raw_data_ptr = new char[len];
    EXPECT_CALL( *tcp_handle, write_(raw_data_ptr, len) )
            .Times(1);
    resource->write( std::unique_ptr<char[]>{raw_data_ptr}, len );
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    tcp_handle->publish( AIO_UVW::WriteEvent{} );
    ASSERT_TRUE(cb_called);
}
