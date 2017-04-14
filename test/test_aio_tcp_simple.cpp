#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "aio_loop_mock.h"
#include "aio_uvw.h"
#include "aio_uvw_tcp.h"

using namespace std;

using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::DoDefault;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::InSequence;
using ::testing::Sequence;

using ErrorEvent = AIO_UVW::ErrorEvent;
using ConnectEvent = AIO_UVW::ConnectEvent;
using DataEvent = AIO_UVW::DataEvent;
using EndEvent = AIO_UVW::EndEvent;
using WriteEvent = AIO_UVW::WriteEvent;

namespace TcpHandleMock_interanl {

template< typename T >
void on(TcpHandleMock&, Callback<T, TcpHandleMock>) {}
template<>
void on<ErrorEvent>(TcpHandleMock&, Callback<ErrorEvent, TcpHandleMock>);
template<>
void on<ConnectEvent>(TcpHandleMock&, Callback<ConnectEvent, TcpHandleMock>);
template<>
void on<DataEvent>(TcpHandleMock&, Callback<DataEvent, TcpHandleMock>);
template<>
void on<EndEvent>(TcpHandleMock&, Callback<EndEvent, TcpHandleMock>);
template<>
void on<WriteEvent>(TcpHandleMock&, Callback<WriteEvent, TcpHandleMock>);

template< typename T >
void connect(TcpHandleMock&, const string&, unsigned short) {}
template<>
void connect<uvw::IPv4>(TcpHandleMock&, const string&, unsigned short);
template<>
void connect<uvw::IPv6>(TcpHandleMock&, const string&, unsigned short);

}

struct TcpHandleMock
{
    template< typename T >
    void on( Callback<T, TcpHandleMock> cb ) { TcpHandleMock_interanl::on<T>(*this, cb); }

    template< typename T >
    std::enable_if_t< std::is_same<T, uvw::IPv4>::value || std::is_same<T, uvw::IPv6>::value, void>
    connect(const string& ip, unsigned short port) { TcpHandleMock_interanl::connect<T>(*this, ip, port); }

    MOCK_METHOD1( on_ErrorEvent, void( Callback<ErrorEvent, TcpHandleMock> ) );
    MOCK_METHOD1( on_ConnectEvent, void( Callback<ConnectEvent, TcpHandleMock> ) );
    MOCK_METHOD1( on_DataEvent, void( Callback<DataEvent, TcpHandleMock> ) );
    MOCK_METHOD1( on_EndEvent, void( Callback<EndEvent, TcpHandleMock> ) );
    MOCK_METHOD1( on_WriteEvent, void( Callback<WriteEvent, TcpHandleMock> ) );

    MOCK_METHOD2( connect, void(const string&, unsigned short) );
    MOCK_METHOD2( connect6, void(const string&, unsigned short) );
    MOCK_METHOD0( read, void() );
    MOCK_METHOD0( stop, void() );
    void write(std::unique_ptr<char[]>ptr, unsigned int len) { write_(ptr.get(), len); }
    MOCK_METHOD2( write_, void(char[], unsigned int) );
    MOCK_METHOD0( close, void() );
};

namespace TcpHandleMock_interanl {

template<>
void on<ErrorEvent>(TcpHandleMock& self, Callback<ErrorEvent, TcpHandleMock> cb) { self.on_ErrorEvent(cb); }
template<>
void on<ConnectEvent>(TcpHandleMock& self, Callback<ConnectEvent, TcpHandleMock> cb) { self.on_ConnectEvent(cb); }
template<>
void on<DataEvent>(TcpHandleMock& self, Callback<DataEvent, TcpHandleMock> cb) { self.on_DataEvent(cb); }
template<>
void on<EndEvent>(TcpHandleMock& self, Callback<EndEvent, TcpHandleMock> cb) { self.on_EndEvent(cb); }
template<>
void on<WriteEvent>(TcpHandleMock& self, Callback<WriteEvent, TcpHandleMock> cb) { self.on_WriteEvent(cb); }

template<>
void connect<uvw::IPv4>(TcpHandleMock& self, const string& ip, unsigned short port) { self.connect(ip, port); }
template<>
void connect<uvw::IPv6>(TcpHandleMock& self, const string& ip, unsigned short port) { self.connect6(ip, port); }

}

struct AIO_Mock
{
    using Loop = LoopMock;
    using TcpHandle = TcpHandleMock;

    using ErrorEvent = ErrorEvent;
    using ConnectEvent = ConnectEvent;
    using DataEvent = DataEvent;
    using EndEvent = EndEvent;
    using WriteEvent = WriteEvent;
};

TEST(TCPSocketWrapperSimple, create_and_close)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, close() )
            .InSequence(s1, s2, s3, s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

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

struct CallbackMock
{
    MOCK_METHOD2(invoke_ErrorEvent, void(const ErrorEvent&, uvw::TCPSocketWrapper&));
    MOCK_METHOD2(invoke_ConnectEvent, void(const ConnectEvent&, uvw::TCPSocketWrapper&));
    MOCK_METHOD2(invoke_DataEvent, void(DataEvent&, uvw::TCPSocketWrapper&));
    MOCK_METHOD2(invoke_EndEvent, void(const EndEvent&, uvw::TCPSocketWrapper&));
    MOCK_METHOD2(invoke_WriteEvent, void(const WriteEvent&, uvw::TCPSocketWrapper&));
};

TEST(TCPSocketWrapperSimple, connect_failed)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    Callback<ErrorEvent, TcpHandleMock> handler_ErrorEvent;
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( SaveArg<0>(&handler_ErrorEvent) );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    const string ip = "127.0.0.1";
    const unsigned short port = 8080;
    CallbackMock cb;
    ErrorEvent error_event{ static_cast<int>(UV_ECONNREFUSED) };
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, connect(ip, port) )
                .Times(1);
        EXPECT_CALL( cb, invoke_ErrorEvent(_,_) )
                .WillOnce( Invoke( [&error_event, &resource](const auto& err, auto& handle)
        {
            ASSERT_EQ( err.code(), error_event.code() );
            auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);
        } ) );
    }
    resource->once<ErrorEvent>( [&cb](const auto& err, auto& handle) { cb.invoke_ErrorEvent(err, handle); } );
    resource->once<ConnectEvent>( [](const auto&, const auto&) { FAIL(); } );

    resource->connect(ip, port);
    handler_ErrorEvent(error_event, *tcp_handle);
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, connect6_failed)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    Callback<ErrorEvent, TcpHandleMock> handler_ErrorEvent;
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( SaveArg<0>(&handler_ErrorEvent) );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    const string ip = "::1";
    const unsigned short port = 8080;
    CallbackMock cb;
    ErrorEvent error_event{ static_cast<int>(UV_ECONNREFUSED) };
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, connect6(ip, port) )
                .Times(1);
        EXPECT_CALL( cb, invoke_ErrorEvent(_,_) )
                .WillOnce( Invoke( [&error_event, &resource](const auto& err, auto& handle)
        {
            ASSERT_EQ( err.code(), error_event.code() );
            auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);
        } ) );
    }
    resource->once<ErrorEvent>( [&cb](const auto& err, auto& handle) { cb.invoke_ErrorEvent(err, handle); } );
    resource->once<ConnectEvent>( [](const auto&, auto&) { FAIL(); } );

    resource->connect6(ip, port);
    handler_ErrorEvent(error_event, *tcp_handle);
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, connect_normal)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    Callback<ConnectEvent, TcpHandleMock> handler_ConnectEvent;
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( SaveArg<0>(&handler_ConnectEvent) );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    CallbackMock cb;
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, connect(_,_) )
                .Times(1);
        EXPECT_CALL( cb, invoke_ConnectEvent(_,_) )
                .WillOnce( Invoke( [&resource](const auto&, auto& handle)
        {
            auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);
        } ) );
    }
    resource->once<ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<ConnectEvent>( [&cb](const auto& event, auto& handle) { cb.invoke_ConnectEvent(event, handle); } );

    resource->connect("127.0.0.1", 8080);
    ConnectEvent connect_event{};
    handler_ConnectEvent(connect_event, *tcp_handle);
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, read_failed)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    Callback<ErrorEvent, TcpHandleMock> handler_ErrorEvent;
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( SaveArg<0>(&handler_ErrorEvent) );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    ErrorEvent error_event{ static_cast<int>(UV_ECONNREFUSED) };
    CallbackMock cb;
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, read() )
                .Times(1);
        EXPECT_CALL( cb, invoke_ErrorEvent(_,_) )
                .WillOnce( Invoke( [&error_event, &resource](const auto& err, auto& handle)
        {
            ASSERT_EQ( err.code(), error_event.code() );
            auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);

        } ) );
    }
    resource->once<ErrorEvent>( [&cb](const auto& err, auto& handle) { cb.invoke_ErrorEvent(err, handle); } );
    resource->once<DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<EndEvent>( [](auto&, auto&) { FAIL(); } );

    resource->read();
    handler_ErrorEvent(error_event, *tcp_handle);
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, read_EOF)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    Callback<EndEvent, TcpHandleMock> handler_EndEvent;
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( SaveArg<0>(&handler_EndEvent) );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    CallbackMock cb;
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, read() )
                .Times(1);
        EXPECT_CALL( cb, invoke_EndEvent(_,_) )
                .WillOnce( Invoke( [&resource](const auto&, auto& handle)
        {
            auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);

        } ) );
    }
    resource->once<ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<EndEvent>( [&cb](const auto& event, auto& handle) { cb.invoke_EndEvent(event, handle); } );

    resource->read();
    EndEvent end_event{};
    handler_EndEvent(end_event, *tcp_handle);
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, read_normal)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    Callback<DataEvent, TcpHandleMock> handler_DataEvent;
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( SaveArg<0>(&handler_DataEvent) );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    const std::size_t len = 42;
    char* raw_data_ptr = new char[len];
    DataEvent data_event{std::unique_ptr<char[]>{raw_data_ptr}, len};
    CallbackMock cb;
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, read() )
                .Times(1);
        EXPECT_CALL( cb, invoke_DataEvent(_,_) )
                .WillOnce( Invoke( [raw_data_ptr, len, &resource](const auto& event, auto& handle)
        {
            ASSERT_EQ(event.data.get(), raw_data_ptr);
            ASSERT_EQ(event.length, len);

            auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);

        } ) );
    }
    resource->once<ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<DataEvent>( [&cb](auto& data_event, auto& handle) { cb.invoke_DataEvent(data_event, handle); } );
    resource->once<EndEvent>( [](const auto&, auto&) { FAIL(); } );

    resource->read();
    handler_DataEvent(data_event, *tcp_handle);
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, read_stop)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( DoDefault() );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, read() )
                .Times(1);
        EXPECT_CALL( *tcp_handle, stop() )
                .Times(1);
    }
    resource->once<ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<EndEvent>( [](const auto&, auto&) { FAIL(); } );

    resource->read();
    resource->stop();
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}

TEST(TCPSocketWrapperSimple, write_normal)
{
    auto loop = make_shared<LoopMock>();
    auto tcp_handle = make_shared<TcpHandleMock>();

    Sequence s1, s2, s3, s4, s5;
    EXPECT_CALL( *loop, resource_TcpHandleMock() )
            .InSequence(s1, s2, s3, s4, s5)
            .WillOnce( Return(tcp_handle) );
    EXPECT_CALL( *tcp_handle, on_ErrorEvent(_))
            .InSequence(s1)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_ConnectEvent(_) )
            .InSequence(s2)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_DataEvent(_) )
            .InSequence(s3)
            .WillOnce( DoDefault() );
    EXPECT_CALL( *tcp_handle, on_EndEvent(_) )
            .InSequence(s4)
            .WillOnce( DoDefault() );
    Callback<WriteEvent, TcpHandleMock> handler_WriteEvent;
    EXPECT_CALL( *tcp_handle, on_WriteEvent(_) )
            .InSequence(s5)
            .WillOnce( SaveArg<0>(&handler_WriteEvent) );

    auto resource = uvw::TCPSocketWrapperSimple<AIO_Mock>::create(loop);
    ASSERT_TRUE(resource);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(tcp_handle.get());

    const unsigned int len = 42;
    char* raw_data_ptr = new char[len];
    CallbackMock cb;
    {
        InSequence dummy;
        EXPECT_CALL( *tcp_handle, write_(raw_data_ptr, len) )
                .Times(1);
        EXPECT_CALL( cb, invoke_WriteEvent(_,_) )
                .WillOnce( Invoke( [&resource](const auto&, auto& handle)
        {
            auto raw_ptr = dynamic_cast< uvw::TCPSocketWrapperSimple<AIO_Mock>* >(&handle);
            ASSERT_NE(raw_ptr, nullptr);
            auto ptr = raw_ptr->shared_from_this();
            ASSERT_EQ(ptr, resource);

        } ) );
    }
    resource->once<ErrorEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<DataEvent>( [](auto&, auto&) { FAIL(); } );
    resource->once<EndEvent>( [](const auto&, auto&) { FAIL(); } );
    resource->once<WriteEvent>( [&cb](const auto& event, auto& handle) { cb.invoke_WriteEvent(event, handle); } );

    resource->write(std::unique_ptr<char[]>{raw_data_ptr}, len);
    WriteEvent write_event{};
    handler_WriteEvent(write_event, *tcp_handle);
    Mock::VerifyAndClearExpectations(tcp_handle.get());
}
