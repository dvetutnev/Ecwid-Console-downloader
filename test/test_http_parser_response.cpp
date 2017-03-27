#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "http.h"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::Expectation;

using namespace std;

struct OnHeadersCompleteMock
{
    MOCK_METHOD1(invoke, bool(HttpParser::OnHeadersComplete_Args*));
};

struct OnBodyMock
{
    MOCK_METHOD2(invoke, void(const char*, size_t));
};

struct OnCompleteMock
{
    MOCK_METHOD0(invoke, void());
};

TEST(HttpParser__Response, normal)
{
    const string buf_headers = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n";

    const string buf_body = ""
            "Hello world!"
            "World hello!";

    unique_ptr<HttpParser::OnHeadersComplete_Args> headers_complete_args;
    OnHeadersCompleteMock on_headers_complete_mock;
    Expectation invoke_on_headers_complete = EXPECT_CALL( on_headers_complete_mock, invoke(_) )
            .WillOnce( Invoke( [&headers_complete_args](auto arg) -> bool { headers_complete_args.reset(arg); return true; } ) );
    HttpParser::OnHeadersComplete on_headers_complete = [&on_headers_complete_mock](auto arg) ->bool { return on_headers_complete_mock.invoke( arg.release() ); };

    string body;
    OnBodyMock on_body_mock;
    Expectation invoke_on_body = EXPECT_CALL( on_body_mock, invoke(_,_) )
            .After(invoke_on_headers_complete)
            .WillOnce( Invoke( [&body](const char* buf, size_t len) { body = string{buf, len}; } ) );
    HttpParser::OnBody on_body = [&on_body_mock](const char* buf, size_t len) { on_body_mock.invoke(buf, len); };

    OnCompleteMock on_complete_mock;
    EXPECT_CALL( on_complete_mock, invoke() )
            .Times(1)
            .After(invoke_on_body);
    HttpParser::OnComplete on_complete = [&on_complete_mock]() { on_complete_mock.invoke(); };

    auto parser = HttpParser::create( on_headers_complete, on_body, on_complete );
    ASSERT_TRUE(parser);

    const string buf = buf_headers + buf_body;
    auto err = parser->response_parse( buf.data(), buf.size() );
    ASSERT_FALSE(err);

    ASSERT_NE(headers_complete_args, nullptr);
    ASSERT_EQ(headers_complete_args->http_code, 200u);
    ASSERT_EQ(headers_complete_args->http_reason, "OK");
    ASSERT_EQ(headers_complete_args->content_length, 24u);

    ASSERT_EQ(body, buf_body);
}

TEST(HttpParser__Response, rvalue_callbacks)
{
    HttpParser::OnHeadersComplete on_headers_complete = [](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { return true; };
    HttpParser::OnBody on_body = [](const char*, size_t) {};
    HttpParser::OnComplete on_complete = []() {};

    auto parser = HttpParser::create(
                std::move(on_headers_complete),
                std::move(on_body),
                std::move(on_complete)
    );
    ASSERT_TRUE(parser);
}

TEST(HttpParser__Response, increment_parsing)
{
    const string buf1 = "HTTP/1.1 301 Mo";
    const string buf2 = "ved permanently\r\n";
    const string buf3 = ""
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Leng";
    const string buf4 = "th: 8\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ran";
    const string buf5 = "ges: bytes\r\n"
            "\r\n"
            "Go ";
    const string buf6 = "next!";

    const string buf_http_reason = "Moved permanently";
    const map< string, string > headers = {
        { "Server", "nginx/1.6.2" },
        { "Content-Type", "text/pain" },
        { "Content-Length", "8" },
        { "Connection", "keep-alive" },
        { "ETag", "\"58c2fb69-c\"" },
        { "Accept-Ranges", "bytes" }
    };
    const string buf_body = "Go next!";

    unique_ptr<HttpParser::OnHeadersComplete_Args> headers_complete_args;
    HttpParser::OnHeadersComplete on_headers_complete = [&headers_complete_args](auto args) -> bool { headers_complete_args = std::move(args); return true; };

    string body;
    HttpParser::OnBody on_body = [&body](const char* buf, size_t len) { body.append(buf, len); };

    size_t invoke_on_complete = 0;
    HttpParser::OnComplete on_complete = [&invoke_on_complete]() { invoke_on_complete++; };

    auto parser = HttpParser::create( on_headers_complete, on_body, on_complete );

    ASSERT_FALSE( parser->response_parse( buf1.data(), buf1.size() ) );
    ASSERT_FALSE( parser->response_parse( buf2.data(), buf2.size() ) );
    ASSERT_FALSE( parser->response_parse( buf3.data(), buf3.size() ) );
    ASSERT_FALSE( parser->response_parse( buf4.data(), buf4.size() ) );
    ASSERT_FALSE( parser->response_parse( buf5.data(), buf5.size() ) );
    ASSERT_FALSE( parser->response_parse( buf6.data(), buf6.size() ) );

    ASSERT_NE(headers_complete_args, nullptr);

    ASSERT_EQ(headers_complete_args->http_code, 301u);
    ASSERT_EQ(headers_complete_args->http_reason, buf_http_reason);
    ASSERT_EQ(headers_complete_args->content_length, 8u);
    ASSERT_EQ(headers_complete_args->headers, headers);

    ASSERT_EQ(buf_body, body);
    ASSERT_EQ(invoke_on_complete, 1u);
}

TEST(HttpParser__Response, on_headers_complete_is_null)
{
    HttpParser::OnBody on_body = [](const char*, size_t) {};
    HttpParser::OnComplete on_complete = []() {};

    ASSERT_THROW( HttpParser::create( nullptr, on_body, on_complete ), std::invalid_argument );
}

TEST(HttpParser__Response, on_body_is_null)
{
    const string buf = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello world!"
            "World hello!";

    size_t invoke_on_headers_complete = 0;
    HttpParser::OnHeadersComplete on_headers_complete = [&invoke_on_headers_complete](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { invoke_on_headers_complete++; return true; };

    size_t invoke_on_complete = 0;
    HttpParser::OnComplete on_complete = [&invoke_on_complete]() { invoke_on_complete++; };

    auto parser = HttpParser::create( on_headers_complete, nullptr, on_complete );

    HttpParser::Error err;
    ASSERT_NO_THROW( err = parser->response_parse( buf.data(), buf.size() ) );
    ASSERT_FALSE(err);

    ASSERT_EQ(invoke_on_headers_complete, 1u);
    ASSERT_EQ(invoke_on_complete, 1u);
}

TEST(HttpParser__Response, on_complete_is_null)
{
    const string buf = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello world!"
            "World hello!";

    size_t invoke_on_headers_complete = 0;
    HttpParser::OnHeadersComplete on_headers_complete = [&invoke_on_headers_complete](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { invoke_on_headers_complete++; return true; };

    size_t invoke_on_body = 0;
    HttpParser::OnBody on_body = [&invoke_on_body](const char*, size_t) { invoke_on_body++; };

    auto parser = HttpParser::create( on_headers_complete, on_body, nullptr );

    HttpParser::Error err;
    ASSERT_NO_THROW( err = parser->response_parse( buf.data(), buf.size() ) );
    ASSERT_FALSE(err);

    ASSERT_EQ(invoke_on_headers_complete, 1u);
    ASSERT_EQ(invoke_on_body, 1u);
}

TEST(HttpParser__Response, do_not_invoke_next_CB_if_on_headers_complete_return_false)
{
    const string buf = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello world!"
            "World hello!";

    size_t invoke_on_headers_complete = 0;
    HttpParser::OnHeadersComplete on_headers_complete = [&invoke_on_headers_complete](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { invoke_on_headers_complete++; return false; };

    size_t invoke_on_body = 0;
    HttpParser::OnBody on_body = [&invoke_on_body](const char*, size_t) { invoke_on_body++; };

    size_t invoke_on_complete = 0;
    HttpParser::OnComplete on_complete = [&invoke_on_complete]() { invoke_on_complete++; };

    auto parser = HttpParser::create( on_headers_complete, on_body, on_complete );

    ASSERT_FALSE( parser->response_parse( buf.data(), buf.size() ) );

    ASSERT_EQ(invoke_on_headers_complete, 1u);
    ASSERT_EQ(invoke_on_body, 0u);
    ASSERT_EQ(invoke_on_complete, 0u);
}

TEST(HttpParser__Response, parse_only_first_message)
{
    const string buf = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello world!"
            "World hello!"
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello world!"
            "World hello!";
    const string buf_body = ""
            "Hello world!"
            "World hello!";

    size_t invoke_on_headers_complete = 0;
    HttpParser::OnHeadersComplete on_headers_complete = [&invoke_on_headers_complete](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { invoke_on_headers_complete++; return true; };

    size_t invoke_on_body = 0;
    string body;
    HttpParser::OnBody on_body = [&invoke_on_body, &body](const char* buf, size_t len) { invoke_on_body++; body.append(buf, len); };

    size_t invoke_on_complete = 0;
    HttpParser::OnComplete on_complete = [&invoke_on_complete]() { invoke_on_complete++; };

    auto parser = HttpParser::create( on_headers_complete, on_body, on_complete );

    ASSERT_FALSE( parser->response_parse( buf.data(), buf.size() ) );

    ASSERT_EQ(invoke_on_headers_complete, 1u);
    ASSERT_EQ(invoke_on_body, 1u);
    ASSERT_EQ(body, buf_body);
    ASSERT_EQ(invoke_on_complete, 1u);
}

TEST(HttpParser__Response, wait_EOF_if_no_Content_Length)
{
    const string buf = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello world!"
            "World hello!";
    const string buf_body = ""
            "Hello world!"
            "World hello!";

    size_t invoke_on_headers_complete = 0;
    HttpParser::OnHeadersComplete on_headers_complete = [&invoke_on_headers_complete](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { invoke_on_headers_complete++; return true; };

    size_t invoke_on_body = 0;
    string body;
    HttpParser::OnBody on_body = [&invoke_on_body, &body](const char* buf, size_t len) { invoke_on_body++; body.append(buf, len); };

    size_t invoke_on_complete = 0;
    HttpParser::OnComplete on_complete = [&invoke_on_complete]() { invoke_on_complete++; };

    auto parser = HttpParser::create( on_headers_complete, on_body, on_complete );

    ASSERT_FALSE( parser->response_parse( buf.data(), buf.size() ) );

    ASSERT_EQ(invoke_on_headers_complete, 1u);
    ASSERT_EQ(invoke_on_body, 1u);
    ASSERT_EQ(body, buf_body);
    ASSERT_EQ(invoke_on_complete, 0u);

    ASSERT_FALSE( parser->response_parse( nullptr, 0 ) );

    ASSERT_EQ(invoke_on_body, 1u);
    ASSERT_EQ(invoke_on_complete, 1u);
}

TEST(HttpParser__Response, Chunked_tranfer_encoding)
{
    const string buf = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "C\r\n"
            "Hello world!\r\n"
            "D\r\n"
            "World, hello!\r\n"
            "0\r\n"
            "\r\n";
    const string buf_body = ""
            "Hello world!"
            "World, hello!";

    size_t invoke_on_headers_complete = 0;
    HttpParser::OnHeadersComplete on_headers_complete = [&invoke_on_headers_complete](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { invoke_on_headers_complete++; return true; };

    string body;
    HttpParser::OnBody on_body = [&body](const char* buf, size_t len) { body.append(buf, len); };

    size_t invoke_on_complete = 0;
    HttpParser::OnComplete on_complete = [&invoke_on_complete]() { invoke_on_complete++; };

    auto parser = HttpParser::create( on_headers_complete, on_body, on_complete );

    ASSERT_FALSE( parser->response_parse( buf.data(), buf.size() ) );

    ASSERT_EQ(invoke_on_headers_complete, 1u);
    ASSERT_EQ(body, buf_body);
    ASSERT_EQ(invoke_on_complete, 1u);
}

/* mocking HttpParser */

template< typename HttpParser_t >
struct ClientHttpParser
{
    ClientHttpParser(
            HttpParser::OnHeadersComplete on_headers_complete,
            HttpParser::OnBody on_body,
            HttpParser::OnComplete on_complete
            )
        : m_on_headers_complete{on_headers_complete},
          m_on_body{on_body},
          m_on_complete{on_complete}
    {}

    void create_parser()
    {
        parser = HttpParser_t::create(
                    std::move(m_on_headers_complete),
                    std::move(m_on_body),
                    std::move(m_on_complete)
                    );
    }

    HttpParser::Error run_parser()
    {
        return parser->response_parse( buf.data(), buf.size() );
    }

    unique_ptr<HttpParser_t> parser;

    HttpParser::OnHeadersComplete m_on_headers_complete;
    HttpParser::OnBody m_on_body;
    HttpParser::OnComplete m_on_complete;

    string buf = "vooooid";
};

struct HttpParserMock
{
    static unique_ptr<HttpParserMock> instance;
    static unique_ptr<HttpParserMock> create(
            HttpParser::OnHeadersComplete on_headers_complete,
            HttpParser::OnBody on_body,
            HttpParser::OnComplete on_complete
            )
    {
        instance->create_( on_headers_complete, on_body, on_complete );
        return std::move(instance);
    }

    MOCK_METHOD3(create_, void( HttpParser::OnHeadersComplete, HttpParser::OnBody, HttpParser::OnComplete ));
    MOCK_METHOD2(response_parse, HttpParser::Error(const char*, size_t));
};

unique_ptr<HttpParserMock> HttpParserMock::instance;

TEST(HttpParserMock, normal)
{
    auto parser_ptr = new HttpParserMock();
    HttpParserMock::instance = unique_ptr<HttpParserMock>{ parser_ptr };

    HttpParser::OnHeadersComplete handler_on_headers_complete;
    HttpParser::OnBody handler_on_body;
    HttpParser::OnComplete handler_on_complete;
    EXPECT_CALL( *parser_ptr, create_(_,_,_) )
            .WillOnce( DoAll(
                           SaveArg<0>(&handler_on_headers_complete),
                           SaveArg<1>(&handler_on_body),
                           SaveArg<2>(&handler_on_complete)
                           ) );

    EXPECT_CALL( *parser_ptr, response_parse(_,_) )
            .WillOnce( Return( HttpParser::Error{HPE_OK} ) );

    size_t a = 0;
    auto on_headers_complete = [&a](unique_ptr<HttpParser::OnHeadersComplete_Args>) -> bool { a++; return true; };
    size_t b = 10;
    auto on_body = [&b](const char*, size_t) { b++; };
    size_t c = 110;
    auto on_complete = [&c]() { c++; };

    ClientHttpParser<HttpParserMock> client{ std::move(on_headers_complete), std::move(on_body), std::move(on_complete) };
    client.create_parser();

    /* check take callbacks */
    handler_on_headers_complete( unique_ptr<HttpParser::OnHeadersComplete_Args>{} );
    ASSERT_EQ(a, 1u);
    handler_on_body(nullptr, 0);
    ASSERT_EQ(b, 11u);
    handler_on_complete();
    ASSERT_EQ(c, 111u);

    /* check returnig result */
    auto result_parse = client.run_parser();
    ASSERT_FALSE(result_parse);

    client.parser.reset();
}
