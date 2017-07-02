#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mock/uvw/loop_mock.h"
#include "mock/uvw/tcp_handle_mock.h"
#include <uvw/stream.hpp>

#include "aio/tcp_simple.h"

using namespace std;

using ::testing::_;
using ::testing::Return;
using ::testing::Mock;
using ::testing::InSequence;
using ::testing::Mock;

struct AIO_Mock
{
    using Loop = LoopMock;
    using TcpHandle = TcpHandleMock;
};

TEST(TCPSocketWrapperSimple, TcpHandle_is_null)
{
    auto loop = make_shared<LoopMock>();
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .WillOnce( Return(nullptr) );
    auto resource = ::aio::TCPSocketSimple<AIO_Mock>::create(loop);
    ASSERT_FALSE(resource);
    ASSERT_TRUE( loop.unique() );
}

struct TCPSocketSimpleF : public ::testing::Test
{
    TCPSocketSimpleF()
        : loop{ make_shared<LoopMock>() },
          tcp_handle{ make_shared<TcpHandleMock>() }
    {
        EXPECT_CALL( *loop, resource_TcpHandleMock() )
                .WillOnce( Return(tcp_handle) );

        resource = ::aio::TCPSocketSimple<AIO_Mock>::create(loop);

        EXPECT_TRUE(resource);
        Mock::VerifyAndClearExpectations(loop.get());
    }

    void resource_close()
    {
        EXPECT_CALL( *tcp_handle, close() )
                .Times(1);

        bool cb_called = false;
        resource->clear<::uvw::CloseEvent>();
        resource->once<::uvw::CloseEvent>( [&cb_called, this](const auto&, auto& handle)
        {
            cb_called = true;

            auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);
        } );

        resource->close();
        tcp_handle->publish( ::uvw::CloseEvent{} );

        EXPECT_TRUE(cb_called);
        Mock::VerifyAndClearExpectations(tcp_handle.get());
    }

    virtual ~TCPSocketSimpleF()
    {
        EXPECT_TRUE( resource.unique() );
        EXPECT_LE(loop.use_count(), 2);
        EXPECT_LE(tcp_handle.use_count(), 2);
    }

    shared_ptr<LoopMock> loop;
    shared_ptr<TcpHandleMock> tcp_handle;

    shared_ptr< ::aio::TCPSocketSimple<AIO_Mock> > resource;
};

TEST_F(TCPSocketSimpleF, create_and_close)
{
    resource_close();
}

TEST_F(TCPSocketSimpleF, connect_failed)
{
    bool cb_called = false;
    ::uvw::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    resource->once<::uvw::ErrorEvent>( [&cb_called, &event, this](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    const string ip = "127.0.0.1";
    const unsigned short port = 8080;
    EXPECT_CALL( *tcp_handle, connect(ip, port) )
            .Times(1);

    resource->connect(ip, port);
    tcp_handle->publish(event);
    EXPECT_TRUE(cb_called);

    Mock::VerifyAndClearExpectations(tcp_handle.get());

    resource_close();
}

TEST_F(TCPSocketSimpleF, connect6_failed)
{
    bool cb_called = false;
    ::uvw::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    resource->once<::uvw::ErrorEvent>( [&cb_called, &event, this](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    const string ip = "::1";
    const unsigned short port = 8080;
    EXPECT_CALL( *tcp_handle, connect(ip, port) )
            .Times(1);

    resource->connect(ip, port);
    tcp_handle->publish(event);
    EXPECT_TRUE(cb_called);

    Mock::VerifyAndClearExpectations(tcp_handle.get());

    resource_close();
}

TEST_F(TCPSocketSimpleF, connect_shutdown_close_normal)
{
    bool cb_connect_called = false;
    resource->once<::uvw::ConnectEvent>( [&cb_connect_called, this](const auto&, auto& handle)
    {
        cb_connect_called = true;
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    bool cb_shutdown_called = false;
    resource->once<::uvw::ShutdownEvent>( [&cb_shutdown_called, this](const auto&, auto& handle)
    {
        cb_shutdown_called = true;
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    bool cb_close_called = false;
    resource->once<::uvw::CloseEvent>( [&cb_close_called, this](const auto&, auto& handle)
    {
        cb_close_called = true;
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<::uvw::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    const string ip = "127.0.0.1";
    const size_t port = 8080;
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, connect(ip, port) )
                .Times(1);
        EXPECT_CALL( *tcp_handle, shutdown() )
                .Times(1);
    }
    resource->connect(ip, port);
    tcp_handle->publish( ::uvw::ConnectEvent{} );
    EXPECT_TRUE(cb_connect_called);

    resource->shutdown();
    tcp_handle->publish( ::uvw::ShutdownEvent{} );
    EXPECT_TRUE(cb_shutdown_called);

    Mock::VerifyAndClearExpectations(tcp_handle.get());

    resource_close();
}

TEST_F(TCPSocketSimpleF, read_failed)
{
    ::uvw::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    bool cb_called = false;
    resource->once<::uvw::ErrorEvent>( [&cb_called, &event, this](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<::uvw::ConnectEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](auto&, auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, read() )
            .Times(1);

    resource->read();
    tcp_handle->publish(event);

    EXPECT_TRUE(cb_called);
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    resource_close();
}

TEST_F(TCPSocketSimpleF, read_EOF)
{
    bool cb_called = false;
    resource->once<::uvw::EndEvent>( [&cb_called, this](const auto&, auto& handle)
    {
        cb_called = true;
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<::uvw::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, read() )
            .Times(1);
    EXPECT_CALL( *tcp_handle, active() )
            .WillOnce( Return(true) );

    resource->read();
    EXPECT_TRUE( resource->active() );
    tcp_handle->publish( ::uvw::EndEvent{} );

    EXPECT_TRUE(cb_called);
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    resource_close();
}

TEST_F(TCPSocketSimpleF, read_normal)
{
    bool cb_called = false;
    const std::size_t len = 42;
    char* raw_data_ptr = new char[len];
    resource->once<::uvw::DataEvent>( [&cb_called, &raw_data_ptr, &len, this](auto& event, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ(event.data.get(), raw_data_ptr);
        ASSERT_EQ(event.length, len);

        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);

    } );
    resource->once<::uvw::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, read() )
            .Times(1);

    resource->read();
    tcp_handle->publish( ::uvw::DataEvent{std::unique_ptr<char[]>{raw_data_ptr}, len} );

    EXPECT_TRUE(cb_called);
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    resource_close();
}

TEST_F(TCPSocketSimpleF, read_stop)
{
    resource->once<::uvw::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, auto&) { FAIL(); } );

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

    resource_close();
}

TEST_F(TCPSocketSimpleF, write_failed_and_close_on_event)
{
    bool cb_called = false;
    ::uvw::ErrorEvent event{ static_cast<int>(UV_ECONNREFUSED) };
    resource->once<::uvw::ErrorEvent>( [&cb_called, &event, this](const auto& err, auto& handle)
    {
        cb_called = true;
        ASSERT_EQ( err.code(), event.code() );
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);

        this->resource_close();
    } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, auto&) { FAIL(); } );

    const unsigned int len = 42;
    char* raw_data_ptr = new char[len];
    EXPECT_CALL( *tcp_handle, write_(raw_data_ptr, len) )
            .Times(1);

    resource->write( std::unique_ptr<char[]>{raw_data_ptr}, len );
    tcp_handle->publish(event);
    EXPECT_TRUE(cb_called);
    tcp_handle->publish( ::uvw::CloseEvent{} );

    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST_F(TCPSocketSimpleF, write_normal)
{
    bool cb_called = false;
    resource->once<::uvw::WriteEvent>( [&cb_called, this](const auto&, auto& handle)
    {
        cb_called = true;
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);
    } );
    resource->once<::uvw::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<::uvw::EndEvent>( [](const auto&, auto&) { FAIL(); } );

    const unsigned int len = 42;
    char* raw_data_ptr = new char[len];
    EXPECT_CALL( *tcp_handle, write_(raw_data_ptr, len) )
            .Times(1);

    resource->write( std::unique_ptr<char[]>{raw_data_ptr}, len );
    tcp_handle->publish( ::uvw::WriteEvent{} );
    EXPECT_TRUE(cb_called);

    Mock::VerifyAndClearExpectations(tcp_handle.get());

    resource_close();
}

TEST_F(TCPSocketSimpleF, read_EOF_and_close_on_event)
{
    bool cb_called = false;
    resource->once<::uvw::EndEvent>( [&cb_called, this](const auto&, auto& handle)
    {
        cb_called = true;
        auto raw_ptr = dynamic_cast< ::aio::TCPSocketSimple<AIO_Mock>* >(&handle);
        ASSERT_NE(raw_ptr, nullptr);
        auto ptr = raw_ptr->shared_from_this();
        ASSERT_EQ(ptr, resource);

        this->resource_close();
    } );
    resource->once<::uvw::ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<::uvw::ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::DataEvent>( [](const auto&, const auto&) { FAIL(); } );
    resource->once<::uvw::WriteEvent>( [](const auto&, const auto&) { FAIL(); } );

    EXPECT_CALL( *tcp_handle, read() )
            .Times(1);

    resource->read();
    tcp_handle->publish( ::uvw::EndEvent{} );
    EXPECT_TRUE(cb_called);

    tcp_handle->publish( ::uvw::CloseEvent{} );

    Mock::VerifyAndClearExpectations(tcp_handle.get());
}
