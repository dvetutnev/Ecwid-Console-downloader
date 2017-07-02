#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mock/uvw/loop_mock.h"
#include "mock/uvw/dns_mock.h"
#include "mock/uvw/timer_mock.h"
#include "mock/uvw/file_mock.h"
#include "mock/aio/tcp_socket_mock.h"
#include "mock/on_tick_mock.h"

#include "aio_uvw.h"
#include "http.h"

#include "downloader_simple.h"

#include <regex>
#include <algorithm>
#include <random>

using ::std::cout;
using ::std::endl;
using ::std::size_t;
using ::std::string;
using ::std::shared_ptr;
using ::std::make_shared;
using ::std::unique_ptr;
using ::std::make_unique;
using ::std::move;
using ::std::function;
using ::std::copy_n;

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ByMove;
using ::testing::Mock;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::InSequence;
using ::testing::DoAll;
using ::testing::SaveArg;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::AnyNumber;

struct AIO_Mock
{
    using Loop = LoopMock;
    using ErrorEvent = AIO_UVW::ErrorEvent;

    using AddrInfoEvent = AIO_UVW::AddrInfoEvent;
    using GetAddrInfoReq = GetAddrInfoReqMock;
    using IPAddress = AIO_UVW::IPAddress;
    static auto addrinfo2IPAddress(const addrinfo* addr) { return AIO_UVW::addrinfo2IPAddress(addr); }

    using TCPSocket = ::aio::TCPSocket;
    using TCPSocketSimple = ::aio::TCPSocketMock;
    using ConnectEvent = AIO_UVW::ConnectEvent;
    using WriteEvent = AIO_UVW::WriteEvent;
    using DataEvent = AIO_UVW::DataEvent;
    using EndEvent = AIO_UVW::EndEvent;
    using ShutdownEvent = AIO_UVW::ShutdownEvent;

    using TimerHandle = TimerHandleMock;
    using TimerEvent = AIO_UVW::TimerEvent;

    using FileReq = FileReqMock;
    using FileOpenEvent = AIO_UVW::FileOpenEvent;
    using FileWriteEvent = AIO_UVW::FileWriteEvent;
    using FileCloseEvent = AIO_UVW::FileCloseEvent;

    using FsReq = FsReqMock;
};

/*------- HttpParserMock -------*/

using UriParseResult = HttpParser::UriParseResult;
struct HttpParserMock
{
    using UriParseResult = HttpParser::UriParseResult;
    static HttpParserMock* instance_uri_parse;
    static unique_ptr<UriParseResult> uri_parse(const string& uri) { return instance_uri_parse->uri_parse_(uri); }
    MOCK_METHOD1( uri_parse_, unique_ptr<UriParseResult>(const string&) );

    using OnData = HttpParser::OnData;
    using ResponseParseResult = HttpParser::ResponseParseResult;
    static HttpParserMock* instance_response_parse;
    static unique_ptr<HttpParserMock> create(OnData on_data) { return instance_response_parse->create_( move(on_data) ); }
    MOCK_METHOD1( create_, unique_ptr<HttpParserMock>(OnData) );
    const ResponseParseResult response_parse(unique_ptr<char[]> data, size_t len) { return response_parse_(data.get(), len); }
    MOCK_CONST_METHOD2( response_parse_, ResponseParseResult(const char[], size_t) );
};
HttpParserMock* HttpParserMock::instance_uri_parse;
HttpParserMock* HttpParserMock::instance_response_parse;

/*------- uri parse -------*/

struct DownloaderSimpleF : public ::testing::Test
{
    DownloaderSimpleF()
        : loop{ make_shared<LoopMock>() },
          on_tick{ make_shared<OnTickMock>() },
          instance_uri_parse{ make_unique<HttpParserMock>() },

          backlog{4},
          downloader{ make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick, backlog) }
    {
        HttpParserMock::instance_uri_parse = instance_uri_parse.get();
    }

    virtual ~DownloaderSimpleF()
    {
        EXPECT_TRUE( downloader.unique() );
        EXPECT_LE( loop.use_count(), 2 );
        EXPECT_LE( on_tick.use_count(), 2 );
    }

    shared_ptr<LoopMock> loop;
    shared_ptr<OnTickMock> on_tick;
    unique_ptr<HttpParserMock> instance_uri_parse;

    const size_t backlog;

    shared_ptr<Downloader> downloader;
};

TEST_F(DownloaderSimpleF, uri_parse_failed)
{
    const string bad_uri = "bad_uri";
    EXPECT_CALL( *instance_uri_parse, uri_parse_(bad_uri) )
            .WillOnce( Return( ByMove(nullptr) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    EXPECT_FALSE( downloader->run(bad_uri, "") );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed);
    cout << "downloader status: " << status.state_str << endl;

    Mock::VerifyAndClearExpectations( instance_uri_parse.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

struct DownloaderSimpleHandlesCreate : public DownloaderSimpleF
{
    DownloaderSimpleHandlesCreate()
        : host{"www.internet.org"},
          port{8080},
          query{"/uri/fname.zip"},
          uri{host + query},
          fname{"fname"}
    {
        auto uri_parsed = make_unique<UriParseResult>();
        uri_parsed->host = host;
        uri_parsed->port = port;
        uri_parsed->query = query;
        EXPECT_CALL( *instance_uri_parse, uri_parse_(uri) )
                .WillOnce( Return( ByMove( std::move(uri_parsed) ) ) );
    }

    const string host;
    const unsigned short port;
    const string query;
    const string uri;
    const string fname;
};

TEST_F(DownloaderSimpleHandlesCreate, socket_create_failed)
{
    auto timer = make_shared<TimerHandleMock>();
    auto resolver = make_shared<GetAddrInfoReqMock>();

    EXPECT_CALL( *loop, resource_TCPSocketMock() )
            .WillOnce( Return(nullptr) );

    bool timer_closed = true;
    EXPECT_CALL( *loop, resource_TimerHandleMock() )
            .Times( AtMost(1) )
            .WillRepeatedly( DoAll(
                                 InvokeWithoutArgs( [&timer_closed]() { timer_closed = false; } ),
                                 Return(timer)
                                 ) );
    EXPECT_CALL( *timer, close_() )
            .Times( AtMost(1) )
            .WillRepeatedly( InvokeWithoutArgs( [&timer_closed]() { timer_closed = true; } ) );

    EXPECT_CALL( *loop, resource_GetAddrInfoReqMock() )
            .Times( AtMost(1) )
            .WillRepeatedly( Return(resolver) );
    EXPECT_CALL( *resolver, nodeAddrInfo(_) )
            .Times(0);
    EXPECT_CALL( *resolver, cancel() )
            .Times(0);

    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    EXPECT_FALSE( downloader->run(uri, fname) );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed);
    cout << "downloader status: " << status.state_str << endl;

    Mock::VerifyAndClearExpectations( instance_uri_parse.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
    Mock::VerifyAndClearExpectations( loop.get() );

    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( resolver.get() );
    EXPECT_TRUE(timer_closed);

    EXPECT_LE( timer.use_count(), 2 );
    EXPECT_LE( resolver.use_count(), 2 );
}

TEST_F(DownloaderSimpleHandlesCreate, timer_create_failed)
{
    auto socket = make_shared<::aio::TCPSocketMock>();
    auto resolver = make_shared<GetAddrInfoReqMock>();

    bool socket_closed = true;
    EXPECT_CALL( *loop, resource_TCPSocketMock() )
            .Times( AtMost(1) )
            .WillRepeatedly( DoAll(
                                 InvokeWithoutArgs( [&socket_closed]() { socket_closed = false; } ),
                                 Return(socket)
                                 ) );
    EXPECT_CALL( *socket, close_() )
            .Times( AtMost(1) )
            .WillRepeatedly( InvokeWithoutArgs( [&socket_closed]() { socket_closed = true; } ) );

    EXPECT_CALL( *loop, resource_TimerHandleMock() )
            .WillOnce( Return(nullptr) );

    EXPECT_CALL( *loop, resource_GetAddrInfoReqMock() )
            .Times( AtMost(1) )
            .WillRepeatedly( Return(resolver) );
    EXPECT_CALL( *resolver, nodeAddrInfo(_) )
            .Times(0);
    EXPECT_CALL( *resolver, cancel() )
            .Times(0);

    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    EXPECT_FALSE( downloader->run(uri, fname) );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed);
    cout << "downloader status: " << status.state_str << endl;

    Mock::VerifyAndClearExpectations( instance_uri_parse.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
    Mock::VerifyAndClearExpectations( loop.get() );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( resolver.get() );
    EXPECT_TRUE(socket_closed);

    EXPECT_LE( socket.use_count(), 2 );
    EXPECT_LE( resolver.use_count(), 2 );
}

TEST_F(DownloaderSimpleHandlesCreate, resolver_create_failed)
{
    auto socket = make_shared<::aio::TCPSocketMock>();
    auto timer = make_shared<TimerHandleMock>();

    bool socket_closed = true;
    EXPECT_CALL( *loop, resource_TCPSocketMock() )
            .Times( AtMost(1) )
            .WillRepeatedly( DoAll(
                                 InvokeWithoutArgs( [&socket_closed]() { socket_closed = false; } ),
                                 Return(socket)
                                 ) );
    EXPECT_CALL( *socket, close_() )
            .Times( AtMost(1) )
            .WillRepeatedly( InvokeWithoutArgs( [&socket_closed]() { socket_closed = true; } ) );

    bool timer_closed = true;
    EXPECT_CALL( *loop, resource_TimerHandleMock() )
            .Times( AtMost(1) )
            .WillRepeatedly( DoAll(
                                 InvokeWithoutArgs( [&timer_closed]() { timer_closed = false; } ),
                                 Return(timer)
                                 ) );
    EXPECT_CALL( *timer, close_() )
            .Times( AtMost(1) )
            .WillRepeatedly( InvokeWithoutArgs( [&timer_closed]() { timer_closed = true; } ) );

    EXPECT_CALL( *loop, resource_GetAddrInfoReqMock() )
            .WillOnce( Return(nullptr) );

    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    EXPECT_FALSE( downloader->run(uri, fname) );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed);
    cout << "downloader status: " << status.state_str << endl;

    Mock::VerifyAndClearExpectations( instance_uri_parse.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
    Mock::VerifyAndClearExpectations( loop.get() );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    EXPECT_TRUE(socket_closed);
    EXPECT_TRUE(timer_closed);

    EXPECT_LE( socket.use_count(), 2 );
    EXPECT_LE( timer.use_count(), 2 );
}

/*------- host resolve -------*/

struct DownloaderSimpleResolve : public DownloaderSimpleHandlesCreate
{
    DownloaderSimpleResolve()
        : socket{ make_shared<::aio::TCPSocketMock>() },
          timer{ make_shared<TimerHandleMock>() },
          resolver{ make_shared<GetAddrInfoReqMock>() }
    {
        EXPECT_CALL( *loop, resource_TCPSocketMock() )
                .WillOnce( Return(socket) );
        EXPECT_CALL( *loop, resource_TimerHandleMock() )
                .WillOnce( Return(timer) );
        EXPECT_CALL( *loop, resource_GetAddrInfoReqMock() )
                .WillOnce( Return(resolver) );
    }

    void prepare_close_socket_and_timer()
    {
        EXPECT_CALL( *socket, close_() )
                .Times(1);
        EXPECT_CALL( *timer, close_() )
                .Times(1);
    }

    void check_close_socket_and_timer()
    {
        Mock::VerifyAndClearExpectations( socket.get() );
        Mock::VerifyAndClearExpectations( timer.get() );
    }

    virtual ~DownloaderSimpleResolve()
    {
        EXPECT_LE( socket.use_count(), 2 );
        EXPECT_LE( timer.use_count(), 2 );
        EXPECT_LE( resolver.use_count(), 2 );
    }

    shared_ptr<::aio::TCPSocketMock> socket;
    shared_ptr<TimerHandleMock> timer;
    shared_ptr<GetAddrInfoReqMock> resolver;
};

TEST_F(DownloaderSimpleResolve, resolver_failed_on_run)
{
    EXPECT_CALL( *resolver, nodeAddrInfo(host) )
            .WillOnce( InvokeWithoutArgs( [h = resolver.get()]() { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
    EXPECT_CALL( *resolver, cancel() )
            .Times(0);

    prepare_close_socket_and_timer();

    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    EXPECT_FALSE( downloader->run(uri, fname) );

    const auto status = downloader->status();
    EXPECT_EQ(status.state, StatusDownloader::State::Failed);
    cout << "downloader status: " << status.state_str << endl;

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( instance_uri_parse.get() );
    Mock::VerifyAndClearExpectations( loop.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
    Mock::VerifyAndClearExpectations( resolver.get() );
}

auto on_tick_handler = [](Downloader* d) { cout << "on_tick => downloader status: " << d->status().state_str << endl; };

struct DownloaderSimpleResolve_normalRun : public DownloaderSimpleResolve
{
    DownloaderSimpleResolve_normalRun()
    {
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);
        EXPECT_CALL( *resolver, nodeAddrInfo(host) )
                .Times(1);

        EXPECT_TRUE( downloader->run(uri, fname) );

        const auto status = downloader->status();
        EXPECT_EQ( status.state, StatusDownloader::State::OnTheGo );
        cout << "downloader status: " << status.state_str << endl;

        Mock::VerifyAndClearExpectations( instance_uri_parse.get() );
        Mock::VerifyAndClearExpectations( loop.get() );
        Mock::VerifyAndClearExpectations( resolver.get() );
        Mock::VerifyAndClearExpectations( on_tick.get() );
    }
};

TEST_F(DownloaderSimpleResolve_normalRun, host_resolve_failed)
{
    EXPECT_CALL( *resolver, cancel() )
            .Times(0);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );
    prepare_close_socket_and_timer();

    resolver->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EAI_NONAME) } );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( resolver.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleResolve_normalRun, stop)
{
    EXPECT_CALL( *resolver, cancel() )
            .WillOnce( Return(true) );
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );
    prepare_close_socket_and_timer();

    downloader->stop();

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( resolver.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

/*------- connect -------*/

AIO_UVW::AddrInfoEvent create_addr_info_event(const string& ip)
{
    auto addrinfo_raw_ptr = new addrinfo;
    addrinfo_raw_ptr->ai_family = AF_INET;
    addrinfo_raw_ptr->ai_addrlen = sizeof(sockaddr_in);
    addrinfo_raw_ptr->ai_addr = reinterpret_cast<sockaddr*>(new sockaddr_in);
    uv_ip4_addr(ip.c_str(), 0, reinterpret_cast<sockaddr_in*>(addrinfo_raw_ptr->ai_addr));
    addrinfo_raw_ptr->ai_next = nullptr;
    addrinfo_raw_ptr->ai_canonname = nullptr;
    auto addrinfo_ptr = unique_ptr<addrinfo, void(*)(addrinfo*)>{addrinfo_raw_ptr, [](addrinfo* ptr) { uv_freeaddrinfo(ptr); } };
    return AIO_UVW::AddrInfoEvent{ std::move(addrinfo_ptr) };
}

AIO_UVW::AddrInfoEvent create_addr_info_event_ipv6(const string& ip)
{
    auto addrinfo_raw_ptr = new addrinfo;
    addrinfo_raw_ptr->ai_family = AF_INET6;
    addrinfo_raw_ptr->ai_addrlen = sizeof(sockaddr_in6);
    addrinfo_raw_ptr->ai_addr = reinterpret_cast<sockaddr*>(new sockaddr_in6);
    uv_ip6_addr(ip.c_str(), 0, reinterpret_cast<sockaddr_in6*>(addrinfo_raw_ptr->ai_addr));
    addrinfo_raw_ptr->ai_next = nullptr;
    addrinfo_raw_ptr->ai_canonname = nullptr;
    auto addrinfo_ptr = unique_ptr<addrinfo, void(*)(addrinfo*)>{addrinfo_raw_ptr, [](addrinfo* ptr) { uv_freeaddrinfo(ptr); } };
    return AIO_UVW::AddrInfoEvent{ std::move(addrinfo_ptr) };
}

struct DownloaderSimpleConnect : DownloaderSimpleResolve_normalRun {};

TEST_F(DownloaderSimpleConnect, connect_failed)
{
    const string ip = "127.0.0.1";
    EXPECT_CALL( *socket, connect(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event(ip) );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleConnect, connect6_failed)
{
    const string ip = "::1";
    EXPECT_CALL( *socket, connect6(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event_ipv6(ip) );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleConnect, connect_timeout)
{
    EXPECT_CALL( *socket, connect(_,_) )
            .Times(1);
    TimerHandleMock::Time repeat{0};
    TimerHandleMock::Time timeout{5000};
    EXPECT_CALL( *timer, start(timeout, repeat) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleConnect, connect_failed_on_run)
{
    EXPECT_CALL( *socket, connect(_,_) )
            .WillOnce( InvokeWithoutArgs( [h = socket.get()]() { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );

    EXPECT_CALL( *timer, start(_,_) )
            .Times(0);

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times(2)
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleConnect, timer_failed_on_run)
{
    EXPECT_CALL( *socket, connect(_,_) )
            .Times(1);

    EXPECT_CALL( *timer, start(_,_) )
            .WillOnce( InvokeWithoutArgs( [h = timer.get()]() { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times(2)
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

/*------- http request write -------*/

struct DownloaderSimpleHttpRequest : public DownloaderSimpleConnect
{
    DownloaderSimpleHttpRequest()
        : ip{"127.0.0.1"},
          timeout{5000},
          repeat{0}
    {
        EXPECT_CALL( *socket, connect(ip, port) )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        resolver->publish( create_addr_info_event(ip) );

        const auto status = downloader->status();
        EXPECT_EQ( status.state, StatusDownloader::State::OnTheGo );

        Mock::VerifyAndClearExpectations( socket.get() );
        Mock::VerifyAndClearExpectations( timer.get() );
        Mock::VerifyAndClearExpectations( on_tick.get() );
    }

    const string ip;
    const TimerHandleMock::Time timeout;
    const TimerHandleMock::Time repeat;
};

TEST_F(DownloaderSimpleHttpRequest, write_failed__and_check_request_data)
{
    string request;
    EXPECT_CALL( *socket, write_(_,_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke( [&request](const char data[], unsigned int len) { request.append(data, len); } ) );
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(_,_) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    const string pattern_request_line = "^GET\\s" + query + "\\sHTTP/1.1\\r\\n";
    std::regex re_request_line{pattern_request_line};
    if ( !std::regex_search(request, re_request_line) )
        FAIL() << "Request failed, invalid request line. Request:" << endl << request << endl;

    const string pattern_host_header = "\\r\\nHost:\\s" + host + "\\r\\n";
    std::regex re_host_header{pattern_host_header};
    if ( !std::regex_search(request, re_host_header) )
        FAIL() << "Request failed, invalid Host header. Request:" << endl << request << endl;

    const string pattern_tail = "\\r\\n\\r\\n";
    std::regex re_tail{pattern_tail};
    if ( !std::regex_search(request, re_tail) )
        FAIL() << "Request failed, invalid tail. Request:" << endl << request << endl;

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations(on_tick.get());
}


TEST_F(DownloaderSimpleHttpRequest, write_timeout)
{
    EXPECT_CALL( *socket, write_(_,_) )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleHttpRequest, stop)
{
    EXPECT_CALL( *socket, write_(_,_) )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    downloader->stop();

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleHttpRequest, write_failed_on_run)
{
    EXPECT_CALL( *socket, write_(_,_) )
            .WillOnce( InvokeWithoutArgs( [h = socket.get()]() { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } ); } ) );

    EXPECT_CALL( *timer, stop() )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(0);

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times(2)
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleHttpRequest, timer_failed_on_run)
{
    EXPECT_CALL( *socket, write_(_,_) )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .WillOnce( InvokeWithoutArgs( [h = timer.get()]() { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
    }
    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times(2)
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

/*------- read start -------*/

struct DownloaderSimpleReadStart : public DownloaderSimpleHttpRequest
{
    DownloaderSimpleReadStart()
    {
        EXPECT_CALL( *socket, write_(_,_) )
                .Times(1);
        {
            InSequence s;
            EXPECT_CALL( *timer, stop() )
                    .Times(1);
            EXPECT_CALL( *timer, start(timeout, repeat) )
                    .Times(1);
        }
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::ConnectEvent{} );

        const auto status = downloader->status();
        EXPECT_EQ( status.state, StatusDownloader::State::OnTheGo );

        Mock::VerifyAndClearExpectations( socket.get() );
        Mock::VerifyAndClearExpectations( timer.get() );
        Mock::VerifyAndClearExpectations( on_tick.get() );
    }
};

TEST_F(DownloaderSimpleReadStart, read_failed_on_run)
{
    EXPECT_CALL( *socket, read() )
            .WillOnce( InvokeWithoutArgs( [h = socket.get()]() { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } ); } ) );

    EXPECT_CALL( *timer, stop() )
            .Times( AtMost(1) );
    EXPECT_CALL( *timer, start(_,_) )
            .Times(0);

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times(2)
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::WriteEvent{} );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

struct DownloaderSimpleReadStart_normalRun : public DownloaderSimpleReadStart
{
    DownloaderSimpleReadStart_normalRun()
    {
        EXPECT_CALL( *socket, read() )
                .Times(1);
        {
            InSequence s;
            EXPECT_CALL( *timer, stop() )
                    .Times(1);
            EXPECT_CALL( *timer, start(timeout, repeat) )
                    .Times(1);
        }
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::WriteEvent{} );

        const auto status = downloader->status();
        EXPECT_EQ( status.state, StatusDownloader::State::OnTheGo );

        Mock::VerifyAndClearExpectations( socket.get() );
        Mock::VerifyAndClearExpectations( timer.get() );
        Mock::VerifyAndClearExpectations( on_tick.get() );
    }
};


TEST_F(DownloaderSimpleReadStart_normalRun, unexpectedly_EOF)
{
    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::EndEvent{} );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleReadStart, error_read)
{
    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleReadStart, timeout_read)
{
    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

/*------- response parse -------*/

struct DownloaderSimpleResponseParse : public DownloaderSimpleReadStart_normalRun
{
    DownloaderSimpleResponseParse()
        : http_parser{ new HttpParserMock }
    {
        HttpParserMock::instance_response_parse = http_parser;
        EXPECT_CALL( *http_parser, create_(_) )
            .WillOnce( DoAll( SaveArg<0>(&handler_on_data),
                              Return( ByMove( unique_ptr<HttpParserMock>{http_parser} ) )
                              ) );
    }

    HttpParserMock* http_parser;
    HttpParser::OnData handler_on_data;
};

TEST_F(DownloaderSimpleResponseParse, redirect)
{
    const size_t len = 1510;
    char* data = new char[len];
    const string redirect_uri = "www.internet.org/redirect";

    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Redirect;
    result.redirect_uri = redirect_uri;

    EXPECT_CALL( *http_parser, response_parse_(data, len) )
            .WillOnce( Return(result) );

    EXPECT_CALL( *socket, stop() )
            .Times(1);
    EXPECT_CALL( *socket, shutdown() )
            .Times(1);

    EXPECT_CALL( *timer, stop() )
            .Times(1);
    EXPECT_CALL( *timer, close_())
            .Times(1);

    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data}, len } );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    EXPECT_CALL( *socket, close_() )
            .Times(1);

    socket->publish( AIO_UVW::ShutdownEvent{} );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Redirect );
    EXPECT_EQ( status_2.redirect_uri, redirect_uri );

    Mock::VerifyAndClearExpectations( http_parser );
    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleResponseParse, error_on_headers)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "404 Not found (HttpParserMock)";

    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( Return(result) );

    EXPECT_CALL( *timer, stop() )
            .Times(1);

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ make_unique<char[]>(421), 421 } );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( http_parser );
    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations(on_tick.get());
}

struct DownloaderSimpleFileOpen : public DownloaderSimpleResponseParse
{
    DownloaderSimpleFileOpen()
        : file{ make_shared<FileReqMock>() },
          file_flags{ O_CREAT | O_EXCL | O_WRONLY },
          file_mode{ S_IRUSR | S_IWUSR | S_IRGRP }
    {
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::InProgress;

        copy_data = [this](const char* input_data, size_t length)
        {
            auto output_data = make_unique<char[]>(length);
            copy_n( input_data, length, output_data.get() );
            handler_on_data(move(output_data), length);
        };

        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .Times( AtLeast(1) )
                .WillRepeatedly( DoAll( Invoke( [this](const char* data, size_t length) { input_data.append(data, length); } ),
                                        Invoke(copy_data),
                                        Return(result) ) );

        EXPECT_CALL( *loop, resource_FileReqMock() )
                .WillOnce( Return(file) );
    }

    virtual ~DownloaderSimpleFileOpen()
    {
        EXPECT_LE(file.use_count(), 2);
    }

    shared_ptr<FileReqMock> file;

    const int file_flags;
    const int file_mode;

    function< void(const char*, size_t) > copy_data;

    string input_data;
};

TEST_F(DownloaderSimpleFileOpen, file_filed_open)
{
    EXPECT_CALL( *file, open(fname, file_flags, file_mode) )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(_,_) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ make_unique<char[]>(42), 42 } );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( http_parser );
    Mock::VerifyAndClearExpectations( loop.get() );

    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( file.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );

    EXPECT_CALL( *file, cancel() )
            .Times(0);

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( file.get() );
    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleFileOpen, file_filed_open_on_run)
{
    EXPECT_CALL( *file, open(fname, file_flags, file_mode) )
            .WillOnce( InvokeWithoutArgs( [this]() { file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } ); } ) );

    EXPECT_CALL( *timer, stop() )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(0);

    EXPECT_CALL( *file, cancel() )
            .Times(0);

    prepare_close_socket_and_timer();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ make_unique<char[]>(421), 421 } );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( http_parser );
    Mock::VerifyAndClearExpectations( loop.get() );
    Mock::VerifyAndClearExpectations( file.get() );
    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

struct FileCloseAndUnlink : public DownloaderSimpleFileOpen
{
    FileCloseAndUnlink()
        : fs{ make_shared<FsReqMock>() }
    {}

    void prepare_close_unlink_file()
    {
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, cancel() )
                .Times(0);
        {
            InSequence s;
            EXPECT_CALL( *file, close() )
                    .Times(1);
            EXPECT_CALL( *fs, unlink(fname) )
                    .Times(1);
        }
    }

    void check_close_unlink_file()
    {
        Mock::VerifyAndClearExpectations( file.get() );
        Mock::VerifyAndClearExpectations( loop.get() );

        file->publish( AIO_UVW::FileCloseEvent{fname.c_str()} );

        Mock::VerifyAndClearExpectations( fs.get() );
    }

    void prepare_cancel_close_unlink_file()
    {
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        {
            InSequence s;
            EXPECT_CALL( *file, cancel() )
                    .WillOnce( Return(true) );
            EXPECT_CALL( *file, close() )
                    .Times(1);
            EXPECT_CALL( *fs, unlink(fname) )
                    .Times(1);
        }
    }

    void check_cancel_close_unlink_file()
    {
        Mock::VerifyAndClearExpectations( file.get() );
        Mock::VerifyAndClearExpectations( loop.get() );

        file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );
        file->publish( AIO_UVW::FileCloseEvent{fname.c_str()} );

        Mock::VerifyAndClearExpectations( fs.get() );
    }

    virtual ~FileCloseAndUnlink()
    {
        EXPECT_LE(fs.use_count(), 2);
    }

    std::shared_ptr<FsReqMock> fs;
};

char* generate_data(size_t length)
{
    auto data = make_unique<char[]>(length);
    std::random_device rd{};
    std::uniform_int_distribution<unsigned int> dist{0, 255};
    std::generate_n( data.get(), length, [&dist, &rd]() { return static_cast<char>( dist(rd) ); } );
    return data.release();
}

struct DownloaderSimpleFileWrite : public FileCloseAndUnlink
{
    DownloaderSimpleFileWrite()
    {
        EXPECT_CALL( *file, open(fname, file_flags, file_mode) )
                .Times(1);
        {
            InSequence s;
            EXPECT_CALL( *timer, stop() )
                    .Times(1);
            EXPECT_CALL( *timer, start(_,_) )
                    .Times(1);
        }
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{ generate_data(421) }, 421 } );

        const auto status = downloader->status();
        EXPECT_EQ( status.state, StatusDownloader::State::OnTheGo );

        Mock::VerifyAndClearExpectations( loop.get() );
        Mock::VerifyAndClearExpectations( timer.get() );
        Mock::VerifyAndClearExpectations( file.get() );
        Mock::VerifyAndClearExpectations( on_tick.get() );
    }
};

TEST_F(DownloaderSimpleFileWrite, write_failed_on_run)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .WillOnce( InvokeWithoutArgs( [h = file.get()]() { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } ); } ) );

    prepare_close_socket_and_timer();
    prepare_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

    const auto status = downloader->status();
    EXPECT_EQ( status.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    check_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleFileWrite, write_failed)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);

    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    prepare_close_socket_and_timer();
    prepare_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    check_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleFileWrite, socket_read_error)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);

    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( http_parser );
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .Times(0);

    prepare_close_socket_and_timer();
    prepare_cancel_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( http_parser ) ;
    check_close_socket_and_timer();
    check_cancel_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleFileWrite, unexpected_EOF)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);

    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    EXPECT_CALL( *timer, stop() )
            .Times(1);

    Mock::VerifyAndClearExpectations( http_parser );
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Connection lost, unexpected EOF (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(nullptr, _) )
            .WillOnce( Return(result) );

    prepare_close_socket_and_timer();
    prepare_cancel_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::EndEvent{} );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( http_parser );
    check_close_socket_and_timer();
    check_cancel_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleFileWrite, parser_error)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);

    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    EXPECT_CALL( *timer, stop() )
            .Times(1);

    Mock::VerifyAndClearExpectations( http_parser );
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Response parse failed (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( Return(result) );

    prepare_close_socket_and_timer();
    prepare_cancel_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(43), 43} );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( http_parser );
    check_close_socket_and_timer();
    check_cancel_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

/* queue */

struct DownloaderSimpleQueue : public FileCloseAndUnlink
{
    DownloaderSimpleQueue()
    {
        EXPECT_CALL( *file, open(fname, file_flags, file_mode) )
                .Times(1);

        EXPECT_CALL( *timer, stop() )
                .Times( AtLeast(1) );
        EXPECT_CALL( *timer, start(_,_) )
                .Times( AtLeast(1) );

        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .Times( AtLeast(1) )
                .WillRepeatedly( Invoke(on_tick_handler) );
    }
};

TEST_F(DownloaderSimpleQueue, partial_file_write)
{
    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active]() { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( ReturnPointee(&socket_active) );
    EXPECT_CALL( *socket, read() )
            .Times(0);

    const size_t chunk_size = 1000;
    for (size_t i = 1; i <= backlog; i++)
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{ generate_data(chunk_size) }, chunk_size} );

    string buff(chunk_size * backlog, '\0');
    auto buff_replace = [&buff](const char* data, size_t length, size_t offset) { buff.replace(offset, length, data, length); };

    EXPECT_EQ(backlog, 4u);

    {
        InSequence s;
        EXPECT_CALL( *file, write(_, chunk_size, 0) ) // first chunk
                .WillOnce( Invoke(buff_replace) );

        EXPECT_CALL( *file, write(_, chunk_size, chunk_size) ) // second cnunk, part one
                .WillOnce( Invoke(buff_replace) );
        EXPECT_CALL( *file, write(_, chunk_size / 2, chunk_size + chunk_size / 2) ) // second chunk, part two
                .WillOnce( Invoke(buff_replace) );

        EXPECT_CALL( *file, write(_, chunk_size, chunk_size * 2) ) // 3
                .WillOnce( Invoke(buff_replace) );

        EXPECT_CALL( *file, write(_, chunk_size, chunk_size * 3) ) // second cnunk, part one
                .WillOnce( Invoke(buff_replace) );
        EXPECT_CALL( *file, write(_, chunk_size / 2, chunk_size * 3 + chunk_size / 2) ) // second chunk, part two
                .WillOnce( Invoke(buff_replace) );
    }

    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} ); // first chunk write done

    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size / 2} ); // second chunk, part one
    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size / 2} ); // second chunk, part two

    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} ); // 3, all write

    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size / 2} ); // 4 chunk, part one

    Mock::VerifyAndClearExpectations( socket.get() );
    // continue read
    {
        InSequence s;
        EXPECT_CALL( *socket, active_() )
                .WillRepeatedly( Return(false) );
        EXPECT_CALL( *socket, read() )
                .Times(1);
    }

    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size / 2} ); // 4 chunk, part two

    Mock::VerifyAndClearExpectations( socket.get() );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );
    EXPECT_TRUE( input_data == buff );

    // Cancel download
    prepare_close_socket_and_timer();
    prepare_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    downloader->stop();

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    check_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleQueue, socket_stop_on_buffer_filled)
{
    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active] { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( ReturnPointee(&socket_active) );
    EXPECT_CALL( *socket, read() )
            .Times(0);

    const size_t chunk_size = 1042;
    for (size_t i = 1; i <= backlog; i++)
    {
        socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
        const auto status = downloader->status();
        EXPECT_EQ(status.downloaded, i * chunk_size);
    }

    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);
    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( socket.get() );

    // Cancel download
    prepare_close_socket_and_timer();
    prepare_cancel_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    downloader->stop();

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    check_cancel_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleQueue, socket_read_on_buffer_empty)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times( static_cast<int>(backlog + 1) );

    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active] { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( ReturnPointee(&socket_active) );
    EXPECT_CALL( *socket, read() )
            .Times(0);

    const size_t chunk_size = 1042;

    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );
    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} );
    for (size_t i = 1; i < backlog; i++)
        socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( socket.get() );

    // continue read
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( ReturnPointee(&socket_active) );
    EXPECT_CALL( *socket, read() )
            .WillOnce( Invoke( [&socket_active]() { socket_active = true; } ) );

    for (size_t i = 1; i <= backlog; i++)
        file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} );

    Mock::VerifyAndClearExpectations( socket.get() );

    // Cancel download
    prepare_close_socket_and_timer();
    prepare_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    downloader->stop();

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    check_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleQueue, dont_call_read_if_socket_active)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times( AtLeast(1) );

    EXPECT_CALL( *socket, active_() )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return(true) );
    EXPECT_CALL( *socket, read() )
            .Times(0);

    const size_t chunk_size = 1042;

    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );
    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} );
    file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( file.get() );
    Mock::VerifyAndClearExpectations( socket.get() );

    // continue write file
    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);

    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::OnTheGo );

    Mock::VerifyAndClearExpectations( file.get() );

    // Cancel download
    prepare_close_socket_and_timer();
    prepare_cancel_close_unlink_file();
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    downloader->stop();

    const auto status_3 = downloader->status();
    EXPECT_EQ( status_3.state, StatusDownloader::State::Failed );

    check_close_socket_and_timer();
    check_cancel_close_unlink_file();
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

/* Complete download */

struct DownloaderSimpleComplete : public DownloaderSimpleQueue
{
    DownloaderSimpleComplete()
    {
        const size_t chunk_size = 1042;
        const size_t chunk_count = 7;

        string buff(chunk_size * chunk_count, '\0');
        auto buff_replace = [&buff](const char* data, size_t length, size_t offset) { buff.replace(offset, length, data, length); };
        EXPECT_CALL( *file, write(_,_,_) )
                .WillRepeatedly( Invoke(buff_replace) );

        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>( generate_data(chunk_size) ), chunk_size} );
        file->publish( AIO_UVW::FileOpenEvent{fname.c_str()} );

        for (size_t i = 1; i < chunk_count; i++)
        {
            socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>( generate_data(chunk_size) ), chunk_size} );
            file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} );
        }

        // shutdown socket and close timer
        Mock::VerifyAndClearExpectations( timer.get() );
        Mock::VerifyAndClearExpectations( socket.get() );
        Mock::VerifyAndClearExpectations( http_parser );

        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, close_() )
                .Times(1);

        EXPECT_CALL( *socket, shutdown() )
                .Times(1);
        EXPECT_CALL( *socket, stop() )
                .WillOnce( Invoke( [this] { socket_active = false;} ) );
        EXPECT_CALL( *socket, active_() )
                .WillRepeatedly( ReturnPointee(&socket_active) );

        HttpParser::ResponseParseResult parser_result;
        parser_result.state = HttpParser::ResponseParseResult::State::Done;
        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .WillOnce( DoAll( Invoke( [this](const char* data, size_t length) { input_data.append(data, length); } ),
                                  Invoke(copy_data),
                                  Return(parser_result) ) );

        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>( generate_data(chunk_size) ), chunk_size} );

        file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} );

        Mock::VerifyAndClearExpectations( timer.get() );
        Mock::VerifyAndClearExpectations( socket.get() );
        Mock::VerifyAndClearExpectations( http_parser );

        // close socket and file
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *loop, resource_FsReqMock() ) // dont delete file
                .Times(0);
        EXPECT_CALL( *socket, read() )
                .Times(0);

        file->publish( AIO_UVW::FileWriteEvent{fname.c_str(), chunk_size} );

        const auto status = downloader->status();
        EXPECT_EQ( status.state, StatusDownloader::State::OnTheGo );

        Mock::VerifyAndClearExpectations( file.get() );
        Mock::VerifyAndClearExpectations( loop.get() );
        Mock::VerifyAndClearExpectations( socket.get() );
    }

    bool socket_active = true;
};

TEST_F(DownloaderSimpleComplete, close_socket_before_file)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);

    socket->publish( AIO_UVW::ShutdownEvent{} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::FileCloseEvent{fname.c_str()} );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Done );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleComplete, close_file_before_socket)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{fname.c_str()} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ShutdownEvent{} );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Done );

    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleComplete, close_socket_before_file_error)
{
    EXPECT_CALL( *timer, close_() )
            .Times(0);

    EXPECT_CALL( *socket, close_() )
            .Times(1);

    socket->publish( AIO_UVW::ShutdownEvent{} );

    const auto status_1 = downloader->status();
    EXPECT_EQ( status_1.state, StatusDownloader::State::OnTheGo );

    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}

TEST_F(DownloaderSimpleComplete, file_error)
{
    EXPECT_CALL( *timer, close_() )
            .Times(0);

    EXPECT_CALL( *socket, close_() )
            .Times(1);

    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );

    const auto status_2 = downloader->status();
    EXPECT_EQ( status_2.state, StatusDownloader::State::Failed );

    Mock::VerifyAndClearExpectations( timer.get() );
    Mock::VerifyAndClearExpectations( socket.get() );
    Mock::VerifyAndClearExpectations( on_tick.get() );
}
