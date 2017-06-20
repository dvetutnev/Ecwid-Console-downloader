#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <regex>
#include <algorithm>
#include <random>

#include "downloader_simple.h"
#include "aio_loop_mock.h"
#include "aio_dns_mock.h"
#include "aio_tcp_socket_mock.h"
#include "aio_timer_mock.h"
#include "aio_file_mock.h"
#include "aio_uvw.h"
#include "on_tick_mock.h"
#include "http.h"

using namespace std;

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
using ::testing::AnyNumber;

struct AIO_Mock
{
    using Loop = LoopMock;
    using ErrorEvent = AIO_UVW::ErrorEvent;

    using AddrInfoEvent = AIO_UVW::AddrInfoEvent;
    using GetAddrInfoReq = GetAddrInfoReqMock;
    using IPAddress = AIO_UVW::IPAddress;
    static auto addrinfo2IPAddress(const addrinfo* addr) { return AIO_UVW::addrinfo2IPAddress(addr); }

    using TCPSocket = uvw::TCPSocket;
    using TCPSocketSimple = TCPSocketMock;
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
    static unique_ptr<HttpParserMock> create(OnData on_data) { return instance_response_parse->create_( std::move(on_data) ); }
    MOCK_METHOD1( create_, unique_ptr<HttpParserMock>(OnData) );
    const ResponseParseResult response_parse(unique_ptr<char[]> data, std::size_t len) { return response_parse_(data.get(), len); }
    MOCK_CONST_METHOD2( response_parse_, ResponseParseResult(const char[], std::size_t) );
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
        EXPECT_LE(loop.use_count(), 2);
        EXPECT_LE(on_tick.use_count(), 2);
    }

    shared_ptr<LoopMock> loop;
    shared_ptr<OnTickMock> on_tick;
    unique_ptr<HttpParserMock> instance_uri_parse;

    const std::size_t backlog;

    shared_ptr<Downloader> downloader;
};

TEST_F(DownloaderSimpleF, uri_parse_failed)
{
    const string bad_uri = "bad_uri";
    const Task task{bad_uri, ""};
    EXPECT_CALL( *instance_uri_parse, uri_parse_(bad_uri) )
            .WillOnce( Return( ByMove(nullptr) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    ASSERT_FALSE( downloader->run(task) );
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- host resolve -------*/

auto on_tick_handler = [](Downloader* d) { cout << "on_tick: status => " << d->status().state_str << endl; };

struct DownloaderSimpleResolve : public DownloaderSimpleF
{
    DownloaderSimpleResolve()
        : host{"www.internet.org"},
          port{8080},
          query{"/uri/fname.zip"},
          task{"www.internet.org", "fname"},
          resolver{ make_shared<GetAddrInfoReqMock>() }
    {
        auto uri_parsed = make_unique<UriParseResult>();
        uri_parsed->host = host;
        uri_parsed->port = port;
        uri_parsed->query = query;
        EXPECT_CALL( *instance_uri_parse, uri_parse_(task.uri) )
                .WillOnce( Return( ByMove( std::move(uri_parsed) ) ) );

        EXPECT_CALL( *loop, resource_GetAddrInfoReqMock() )
                .WillOnce( Return(resolver) );
    }

    virtual ~DownloaderSimpleResolve()
    {
        EXPECT_LE(resolver.use_count(), 2);
    }

    const string host;
    const unsigned short port;
    const string query;
    const Task task;

    shared_ptr<GetAddrInfoReqMock> resolver;
};

TEST_F(DownloaderSimpleResolve, dont_invoke_tick_if_resolver_failed_run)
{
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);
    EXPECT_CALL( *resolver, nodeAddrInfo(host) )
            .WillOnce( Invoke( [h = resolver.get()](string) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );

    ASSERT_FALSE( downloader->run(task) );

    const auto status = downloader->status();
    ASSERT_EQ(status.state, StatusDownloader::State::Failed);
    cout << "status.state_str => " << status.state_str << endl;
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

struct DownloaderSimpleResolve_normalRun : public DownloaderSimpleResolve
{
    DownloaderSimpleResolve_normalRun()
    {
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);
        EXPECT_CALL( *resolver, nodeAddrInfo(host) )
                .Times(1);

        EXPECT_TRUE( downloader->run(task) );

        EXPECT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
        Mock::VerifyAndClearExpectations(instance_uri_parse.get());
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(resolver.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );
    }
};

TEST_F(DownloaderSimpleResolve_normalRun, host_resolve_failed)
{
    resolver->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EAI_NONAME) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleResolve_normalRun, stop)
{
    EXPECT_CALL( *resolver, cancel() )
            .WillOnce( Return(true) );

    downloader->stop();

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
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

struct DownloaderSimpleConnect_CreateHandles : public DownloaderSimpleResolve_normalRun {};

TEST_F(DownloaderSimpleConnect_CreateHandles, socket_create_failed)
{
    EXPECT_CALL( *loop, resource_TCPSocketMock() )
            .WillRepeatedly( Return(nullptr) );
    EXPECT_CALL( *loop, resource_TimerHandleMock() )
            .WillRepeatedly( Return(nullptr) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleConnect_CreateHandles, timer_create_failed)
{
    auto socket = make_shared<TCPSocketMock>();
    EXPECT_CALL( *loop, resource_TCPSocketMock() )
            .WillOnce( Return(socket) );
    EXPECT_CALL( *loop, resource_TimerHandleMock() )
            .WillOnce( Return(nullptr) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
}

struct DownloaderSimpleConnect : DownloaderSimpleConnect_CreateHandles
{
    DownloaderSimpleConnect()
        : socket{ make_shared<TCPSocketMock>() },
          timer{ make_shared<TimerHandleMock>() }
    {
        EXPECT_CALL( *loop, resource_TCPSocketMock() )
                .WillOnce( Return(socket) );
        EXPECT_CALL( *loop, resource_TimerHandleMock() )
                .WillOnce( Return(timer) );
    }

    virtual ~DownloaderSimpleConnect()
    {
        EXPECT_LE(socket.use_count(), 2);
        EXPECT_LE(timer.use_count(), 2);
    }

    shared_ptr<TCPSocketMock> socket;
    shared_ptr<TimerHandleMock> timer;
};

TEST_F(DownloaderSimpleConnect, connect_failed)
{
    const string ip = "127.0.0.1";
    EXPECT_CALL( *socket, connect(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);

    resolver->publish( create_addr_info_event(ip) );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, connect6_failed)
{
    const string ip = "::1";
    EXPECT_CALL( *socket, connect6(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);

    resolver->publish( create_addr_info_event_ipv6(ip) );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, connect_timeout)
{
    EXPECT_CALL( *socket, connect(_,_) )
            .Times(1);
    TimerHandleMock::Time repeat{0};
    TimerHandleMock::Time timeout{5000};
    EXPECT_CALL( *timer, start(timeout, repeat) )
            .Times(1);

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, timer_failed_on_run)
{
    {
        InSequence s;
        EXPECT_CALL( *socket, connect(_,_) )
                .Times(1);
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
    {
        InSequence s;
        EXPECT_CALL( *timer, start(_,_) )
                .WillOnce( Invoke( [h = timer.get()](const auto&, const auto&) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
        EXPECT_CALL( *timer, close_() )
                .Times(1);
    }

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
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

        resolver->publish( create_addr_info_event(ip) );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }

    void prepare_close_socket_and_timer()
    {
        Mock::VerifyAndClearExpectations(on_tick.get());
        EXPECT_CALL( *socket, close_() )
                .Times(1);
        EXPECT_CALL( *timer, close_() )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );
    }

    void check_close_socket_and_timer()
    {
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }

    const string ip;
    TimerHandleMock::Time timeout;
    TimerHandleMock::Time repeat;
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

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
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

    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    prepare_close_socket_and_timer();

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();
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

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    prepare_close_socket_and_timer();

    timer->publish( AIO_UVW::TimerEvent{} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();
}

TEST_F(DownloaderSimpleHttpRequest, timer_failed_on_run)
{
    {
        InSequence s;
        EXPECT_CALL( *socket, write_(_,_) )
                .Times(1);
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .WillOnce( Invoke( [h = timer.get()](const auto&, const auto&) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
        EXPECT_CALL( *timer, close_() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();
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

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

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

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }
};

TEST_F(DownloaderSimpleReadStart, unexpectedly_EOF)
{
    prepare_close_socket_and_timer();

    socket->publish( AIO_UVW::EndEvent{} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();
}

TEST_F(DownloaderSimpleReadStart, error_read)
{
    prepare_close_socket_and_timer();

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();
}

TEST_F(DownloaderSimpleReadStart, timeout_read)
{
    prepare_close_socket_and_timer();

    timer->publish( AIO_UVW::TimerEvent{} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();
}

/*------- response parse -------*/

struct DownloaderSimpleResponseParse : public DownloaderSimpleReadStart
{
    DownloaderSimpleResponseParse()
        : http_parser{ new HttpParserMock }
    {
        HttpParserMock::instance_response_parse = http_parser;
        EXPECT_CALL( *http_parser, create_(_) )
            .WillOnce( DoAll( SaveArg<0>(&handler_on_data),
                              Return( ByMove( unique_ptr<HttpParserMock>{http_parser} ) ) ) );
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

    bool timer_stopped = false;
    EXPECT_CALL( *timer, stop() )
            .Times( AnyNumber() )
            .WillRepeatedly( Invoke( [&timer_stopped]() { timer_stopped = true; } ) );
    EXPECT_CALL( *timer, close_() )
            .Times(1);

    EXPECT_CALL( *socket, shutdown() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data}, len } );
    if (!timer_stopped)
        timer->publish( AIO_UVW::TimerEvent{} );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    EXPECT_CALL( *socket, close_() )
            .Times(1);

    socket->publish( AIO_UVW::ShutdownEvent{} );

    const auto status = downloader->status();
    EXPECT_EQ(status.state, StatusDownloader::State::Redirect);
    EXPECT_EQ(status.redirect_uri, redirect_uri);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleResponseParse, error_on_headers)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "404 Not found (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( Return(result) );

    prepare_close_socket_and_timer();

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{}, 0 } );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    Mock::VerifyAndClearExpectations(http_parser);
    check_close_socket_and_timer();
}

struct DownloaderSimpleFileOpen : public DownloaderSimpleResponseParse
{
    DownloaderSimpleFileOpen()
        : file{ make_shared<FileReqMock>() },
          file_flags{ O_CREAT | O_EXCL | O_WRONLY },
          file_mode{ S_IRUSR | S_IWUSR | S_IRGRP }
    {
        copy_data = [this](const char* input_data, size_t length)
        {
            auto output_data = make_unique<char[]>(length);
            copy_n( input_data, length, output_data.get() );
            handler_on_data(move(output_data), length);
        };
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::InProgress;
        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .Times( AtLeast(1) )
                .WillRepeatedly( DoAll( Invoke(copy_data),
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
};

TEST_F(DownloaderSimpleFileOpen, file_open_error)
{
    EXPECT_CALL( *file, open(task.fname, file_flags, file_mode) )
            .Times(1);
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ make_unique<char[]>(42), 42 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    prepare_close_socket_and_timer();
    EXPECT_CALL( *file, cancel() )
            .Times(0);

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations(file.get());
}

TEST_F(DownloaderSimpleFileOpen, file_open_error_invoke)
{
    EXPECT_CALL( *file, open(task.fname, file_flags, file_mode) )
            .WillOnce( InvokeWithoutArgs( [this]() { file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } ); } ) );

    prepare_close_socket_and_timer();
    EXPECT_CALL( *file, cancel() )
            .Times(0);

    socket->publish( AIO_UVW::DataEvent{ make_unique<char[]>(421), 421 } );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(http_parser);
    check_close_socket_and_timer();
    Mock::VerifyAndClearExpectations(file.get());
}

char* generate_data(size_t length)
{
    auto data = make_unique<char[]>(length);
    random_device rd{};
    uniform_int_distribution<unsigned int> dist{0, 255};
    generate_n( data.get(), length, [&dist, &rd]() { return static_cast<char>( dist(rd) ); } );
    return data.release();
}

struct DownloaderSimpleFileWrite : public DownloaderSimpleFileOpen
{
    DownloaderSimpleFileWrite()
         : fs{ make_shared<FsReqMock>() }
    {
        EXPECT_CALL( *file, open(task.fname, file_flags, file_mode) )
                .Times(1);
        EXPECT_CALL( *timer, again() )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        const size_t length = 421;
        char* const data = generate_data(length);

        socket_buffer.append(data, length);
        socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data}, length } );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_CALL( *file, write(_, _, 0) )
                .WillOnce( Invoke( [this](const char* data, size_t length, int64_t) { file_buffer.append(data, length); } ) );
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);

        file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_EQ( socket_buffer.size(), file_buffer.size() );
        if ( socket_buffer.size() == file_buffer.size() )
            EXPECT_EQ( socket_buffer, file_buffer );
    }

    void prepare_close_and_unlink_file()
    {
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        {
            InSequence s;
            EXPECT_CALL( *file, cancel() )
                    .WillOnce( Return(true) );
            EXPECT_CALL( *file, close() )
                    .Times(1);
            EXPECT_CALL( *fs, unlink(task.fname) )
                    .Times(1);
        }
    }

    void check_close_and_unlink_file()
    {
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(loop.get());

        file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );
        file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

        Mock::VerifyAndClearExpectations(fs.get());
    }

    virtual ~DownloaderSimpleFileWrite()
    {
        EXPECT_LE(fs.use_count(), 2);
    }

    string socket_buffer, file_buffer;

    std::shared_ptr<FsReqMock> fs;
};

TEST_F(DownloaderSimpleFileWrite, socket_read_error)
{
    Mock::VerifyAndClearExpectations(http_parser);
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .Times(0);

    prepare_close_socket_and_timer();
    prepare_close_and_unlink_file();

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);

    check_close_socket_and_timer();
    check_close_and_unlink_file();
}

TEST_F(DownloaderSimpleFileWrite, unexpected_EOF)
{
    Mock::VerifyAndClearExpectations(http_parser);
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Connection lost, unexpected EOF (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(nullptr,_) )
            .WillOnce( Return(result) );

    prepare_close_socket_and_timer();
    prepare_close_and_unlink_file();

    socket->publish( AIO_UVW::EndEvent{} );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);

    check_close_socket_and_timer();
    check_close_and_unlink_file();
}

TEST_F(DownloaderSimpleFileWrite, parser_error)
{
    std::size_t len = 42;
    char* const data = new char[len];
    Mock::VerifyAndClearExpectations(http_parser);
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Response parse failed (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(data, len) )
            .WillOnce( Return(result) );

    prepare_close_socket_and_timer();
    prepare_close_and_unlink_file();

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data}, len} );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);

    check_close_socket_and_timer();
    check_close_and_unlink_file();
}

struct DownloaderSimpleFileWrite_FileError : public DownloaderSimpleFileWrite
{
    DownloaderSimpleFileWrite_FileError()
    {
        EXPECT_CALL( *timer, again() )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        const size_t length = 1452;
        char* const data = generate_data(length);

        socket_buffer.append(data, length);
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data}, length} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        prepare_close_socket_and_timer();
        EXPECT_CALL( *loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
    }

    virtual ~DownloaderSimpleFileWrite_FileError()
    {
        EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);
        Mock::VerifyAndClearExpectations(http_parser);

        check_close_socket_and_timer();

        file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(fs.get());
    }
};

TEST_F(DownloaderSimpleFileWrite_FileError, error_event)
{

    EXPECT_CALL( *file, write( _, _, static_cast<int64_t>(file_buffer.size()) ) )
            .WillOnce( Invoke( [this](const char* data, size_t length, int64_t) { file_buffer.append(data, length); } ) );

    file->publish( AIO_UVW::FileWriteEvent{ task.fname.c_str(), file_buffer.size() } );

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    EXPECT_EQ( socket_buffer.size(), file_buffer.size() );
    if ( socket_buffer.size() == file_buffer.size() )
        EXPECT_EQ( socket_buffer, file_buffer );
    Mock::VerifyAndClearExpectations(file.get());

    {
        // no invoke cancel()
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .Times(0);
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *fs, unlink(task.fname) )
                .Times(1);
    }

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );
}

TEST_F(DownloaderSimpleFileWrite_FileError, error_on_invoke_write)
{
    {
        // no invoke cancel()
        InSequence s;
        EXPECT_CALL( *file, write(_, _, file_buffer.size() ) )
                .WillOnce( InvokeWithoutArgs( [this]() { file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } ); } ) );
        EXPECT_CALL( *file, cancel() )
                .Times(0);
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *fs, unlink(task.fname) )
                .Times(1);
    }

    file->publish( AIO_UVW::FileWriteEvent{ task.fname.c_str(), file_buffer.size() } );
}

struct DownloaderSimpleQueue : public DownloaderSimpleFileOpen
{
    DownloaderSimpleQueue()
    {
        EXPECT_CALL( *timer, again() )
                .Times( AtLeast(1) );
        EXPECT_CALL( *file, open(task.fname, file_flags, file_mode) )
                .Times(1);
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
    string input_data;
    for (size_t i = 1; i <= backlog; i++)
    {
        char* data = generate_data(chunk_size);
        input_data.append(data, chunk_size);
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data}, chunk_size} );
    }

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

    file->publish( AIO_UVW::FileOpenEvent{task.fname.c_str()} );

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} ); // first chunk write done

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // second chunk, part one
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // second chunk, part two

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} ); // 3, all write

    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // 4 chunk, part one
    Mock::VerifyAndClearExpectations(socket.get());
    {
        InSequence s;
        EXPECT_CALL( *socket, active_() )   // continue read
                .WillRepeatedly( Return(false) );
        EXPECT_CALL( *socket, read() )
                .Times(1);
    }
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size / 2} ); // 4 chunk, part two

    Mock::VerifyAndClearExpectations(socket.get());
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    EXPECT_TRUE(input_data == buff);

    // Cancel download
    Mock::VerifyAndClearExpectations(on_tick.get());
    prepare_close_socket_and_timer();

    auto fs = make_shared<FsReqMock>();
    EXPECT_CALL( *loop, resource_FsReqMock() )
            .WillOnce( Return(fs) );
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    {
        InSequence s;
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *fs, unlink( task.fname.c_str() ) )
                .Times(1);
    }

    downloader->stop();
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    check_close_socket_and_timer();

    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(fs.get());
    Mock::VerifyAndClearExpectations(loop.get());
    EXPECT_LE(fs.use_count(), 2);
}

TEST_F(DownloaderSimpleQueue, socket_stop_on_buffer_filled)
{
    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active] { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .WillRepeatedly( ReturnPointee(&socket_active) );

    const size_t chunk_size = 1042;
    for (size_t i = 1; i <= backlog; i++)
    {
        socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
        const auto status = downloader->status();
        EXPECT_EQ(status.downloaded, i * chunk_size);
    }

    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    // Cancel download
    prepare_close_socket_and_timer();

    EXPECT_CALL( *file, cancel() )
            .Times(1);
    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();

    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(file.get());
    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );
}

TEST_F(DownloaderSimpleQueue, socket_read_on_buffer_empty)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times( static_cast<int>(backlog + 2) );

    bool socket_active = true;
    EXPECT_CALL( *socket, stop() )
            .WillOnce( Invoke( [&socket_active] { socket_active = false; } ) );
    EXPECT_CALL( *socket, active_() )
            .Times( AtLeast(1) )
            .WillRepeatedly( ReturnPointee(&socket_active) );

    const size_t chunk_size = 1042;

    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );
    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} );
    for (size_t i = 1; i <= backlog; i++)
        socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );

    Mock::VerifyAndClearExpectations(socket.get());
    // continue read
    {
        InSequence s;
        EXPECT_CALL( *socket, active_() )
                .WillOnce( Return(false) );
        EXPECT_CALL( *socket, read() )
                .Times(1);
    }
    for (size_t i = 1; i <= backlog + 1; i++)
        file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} );
    Mock::VerifyAndClearExpectations(socket.get());

    // Cancel download
    prepare_close_socket_and_timer();

    auto fs = make_shared<FsReqMock>();
    EXPECT_CALL( *loop, resource_FsReqMock() )
            .WillOnce( Return(fs) );
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    {
        InSequence s;
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *fs, unlink( task.fname.c_str() ) )
                .Times(1);
    }

    downloader->stop();
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    check_close_socket_and_timer();

    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(fs.get());
    Mock::VerifyAndClearExpectations(loop.get());
    EXPECT_LE(fs.use_count(), 2);
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
    file->publish( AIO_UVW::FileOpenEvent{task.fname.c_str()} );
    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} );
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), chunk_size} );

    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(socket.get());
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    // continue write file
    EXPECT_CALL( *file, write(_,_,_) )
            .Times(1);
    socket->publish( AIO_UVW::DataEvent{make_unique<char[]>(chunk_size), chunk_size} );
    Mock::VerifyAndClearExpectations(file.get());
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    // Cancel download
    prepare_close_socket_and_timer();

    auto fs = make_shared<FsReqMock>();
    EXPECT_CALL( *loop, resource_FsReqMock() )
            .WillOnce( Return(fs) );
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .Times(1);
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *fs, unlink( task.fname.c_str() ) )
                .Times(1);
    }

    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    check_close_socket_and_timer();

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(loop.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(fs.get());
    EXPECT_LE(fs.use_count(), 2);
}

struct DownloaderSimpleComplete : public DownloaderSimpleQueue
{
    struct Chunk
    {
        Chunk(char* ptr, size_t len)
            : data{ptr},
              length{len}
        {}
        unique_ptr<char[]> data;
        size_t length;
    };

    DownloaderSimpleComplete()
    {
        const size_t chunk_size = 1042;
        const size_t chunk_count = 5;
        list<Chunk> chunks;
        string input_data;

        for (size_t i = 1; i <= chunk_count; i++)
        {
            char* data = generate_data(chunk_size);
            input_data.append(data, chunk_size);
            chunks.emplace_back(data, chunk_size);
        }

        string buff(chunk_size * backlog, '\0');
        auto buff_replace = [&buff](const char* data, size_t length, size_t offset) { buff.replace(offset, length, data, length); };
        EXPECT_CALL( *file, write(_,_,_) )
                .Times( AtLeast(1) )
                .WillRepeatedly( Invoke(buff_replace) );

        auto it = begin(chunks);
        socket->publish( AIO_UVW::DataEvent{move(it->data), it->length} );
        file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );
        size_t last_length = it->length;
        ++it;

        auto it_end = end(chunks);
        --it_end;
        while (it != it_end)
        {
            socket->publish( AIO_UVW::DataEvent{move(it->data), it->length} );
            file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), last_length} );
            last_length = it->length;
            ++it;
        }

        // shutdown socket and close timer
        bool timer_stoped = false;
        EXPECT_CALL( *timer, stop() )
                .Times( AnyNumber() )
                .WillRepeatedly( Invoke( [&timer_stoped]() { timer_stoped = true; } ) );
        EXPECT_CALL( *timer, close_() )
                .Times(1);

        EXPECT_CALL( *socket, shutdown() )
                .Times(1);
        EXPECT_CALL( *socket, active_() )
                .WillRepeatedly( Return(true) );
        EXPECT_CALL( *socket, stop() )
                .Times(0);

        Mock::VerifyAndClearExpectations(http_parser);
        HttpParser::ResponseParseResult parser_result;
        parser_result.state = HttpParser::ResponseParseResult::State::Done;
        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .WillOnce( DoAll( Invoke(copy_data),
                                  Return(parser_result) ) );

        socket->publish( AIO_UVW::DataEvent{move(it->data), it->length} );
        file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), last_length} );
        last_length = it->length;

        if (!timer_stoped)
            timer->publish( AIO_UVW::TimerEvent{} ); // timer should be cleared or stoped
        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(http_parser);

        // close socket and file
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( *loop, resource_FsReqMock() ) // dont delete file
                .Times(0);
        EXPECT_CALL( *socket, read() )
                .Times(0);

        file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), last_length} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(loop.get());
        Mock::VerifyAndClearExpectations(socket.get());

        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
};

TEST_F(DownloaderSimpleComplete, close_socket_before_file)
{
    socket->publish( AIO_UVW::ShutdownEvent{} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Done);
}

TEST_F(DownloaderSimpleComplete, close_file_before_socket)
{
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ShutdownEvent{} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Done);
}

TEST_F(DownloaderSimpleComplete, close_socket_before_file_error)
{
    socket->publish( AIO_UVW::ShutdownEvent{} );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    EXPECT_CALL( *socket, close_() )
            .Times( AnyNumber() );
    EXPECT_CALL( *timer, close_() )
            .Times( AnyNumber() );
    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);
}

TEST_F(DownloaderSimpleComplete, file_error)
{
    EXPECT_CALL( *timer, close_() )
            .Times( AnyNumber() );
    Mock::VerifyAndClearExpectations(on_tick.get());
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );
    EXPECT_EQ(downloader->status().state, StatusDownloader::State::Failed);
}
