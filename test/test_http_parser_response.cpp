#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "http.h"

extern "C" {
#include <http_parser.h> // remove ???
}

using ::testing::_;
using ::testing::SaveArg;

using namespace std;

/*
TEST(http_parser, incomplete_body)
{
    const string buf ="\
HTTP/1.0 200 OK\r\n\
Server: nginx/1.6.2\r\n\
Date: Fri, 10 Mar 2017 18:11:04 GMT\r\n\
Content-Type: text/pain\r\n\
Last-Modified: Fri, 10 Mar 2017 18:09:54 GMT\r\n\
Connection: keep-alive\r\n\
ETag: \"58c2fb69-c\"\r\n\
Accept-Ranges: bytes\r\n\
\r\n\
Hello world\
";

//
    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    struct http_parser_data
    {
        size_t content_length = 0;
        size_t body_length = 0;
        size_t http_code = 0;
        string http_status;
    };
    http_parser_data data;
    parser.data = &data;

    http_parser_settings settings;
    http_parser_settings_init(&settings);

    auto on_status = [](http_parser* parser, const char* at, size_t len) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser->data);
        data_ptr->http_code = parser->status_code;
        data_ptr->http_status.append(at, len);
        return 0;
    };
    settings.on_status = on_status;

    auto on_headers_complete = [](http_parser* parser) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser->data);
        data_ptr->content_length = parser->content_length;
        return 0;
    };
    settings.on_headers_complete = on_headers_complete;

    auto on_boby = [](http_parser* parser, const char* at, size_t len) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser->data);
        data_ptr->body_length = len;
        cout << "on_body! len => " << len << endl;
        return 0;
    };
    settings.on_body = on_boby;

    auto on_message_complete = [](http_parser*) -> int
    {
        cout << "on_message_complete!" << endl;
        return 0;
    };
    settings.on_message_complete = on_message_complete;

    const size_t nparsed = http_parser_execute(&parser, &settings, buf.data(), buf.size());
    cout << "send next data in parser... " << endl;
    http_parser_execute(&parser, &settings, "next_data", 5);
    cout << "send EOF in parser... " << endl;
    http_parser_execute(&parser, &settings, nullptr, 0);

    const char* str = "11"
            "22";
    std::cout << str << std::endl;

    cout << "http_status => " << data.http_status << endl;

    ASSERT_EQ(nparsed, buf.size());
    ASSERT_EQ(parser.status_code, 200u);

    //ASSERT_EQ(data.content_length, 12u);
    ASSERT_EQ(data.body_length, 11u);
    ASSERT_EQ(data.http_code, 200u);
}
*/

TEST(http_parser, on_headers_complete_return_1)
{
    const string buf = ""
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Server: nginx/1.6.2\r\n"
            "Date: Fri, 10 Mar 2017 18:11:04 GMT\r\n"
            "Content-Type: text/pain\r\n"
            "Contenet-Length: 12\r\n"
            "Last-Modified: Fri, 10 Mar 2017 18:09:54 GMT\r\n"
            "Location: http://www.example.org/index.asp\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello world!";
//
    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    struct http_parser_data
    {
        bool on_headers_complete = false;
        bool on_body = false;
        bool on_message_complete = false;
        size_t nread = 0;
    };
    http_parser_data data;
    parser.data = &data;

    http_parser_settings settings;
    http_parser_settings_init(&settings);
    settings.on_headers_complete = [](http_parser* parser_ptr) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser_ptr->data);
        data_ptr->on_headers_complete = true;
        data_ptr->nread = parser_ptr->nread;
        return 0;
    };
    settings.on_body = [](http_parser* parser_ptr, const char* at, size_t len) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser_ptr->data);
        data_ptr->on_body = true;
        return 0;
    };
    settings.on_message_complete = [](http_parser* parser_ptr) -> int
    {
        auto data_ptr = reinterpret_cast<http_parser_data*>(parser_ptr->data);
        data_ptr->on_message_complete = true;
        return 0;
    };

    const size_t nparsed = http_parser_execute(&parser, &settings, buf.data(), buf.size());
    cout << "http_errno => " << http_errno_description( static_cast<http_errno>(parser.http_errno) ) << endl;
    cout << "parser.nread (on_headers_complete) => " << "\r" << data.nread << " left buf => " << buf.substr(data.nread) << endl;

    ASSERT_TRUE(data.on_headers_complete);
    //ASSERT_FALSE(data.on_body);
    //ASSERT_TRUE(data.on_message_complete);

    ASSERT_EQ(parser.http_errno, HPE_OK);

    ASSERT_EQ(parser.status_code, 301u);
    //ASSERT_EQ(nparsed, buf.size());
}

TEST(HttpParser__Response, normal)
{
    HttpParser::OnHeadersComplete on_headers_complete;
    HttpParser::OnBody on_body;
    HttpParser::OnComplete on_complete;

    auto parser = HttpParser::create(on_headers_complete, on_body, on_complete);
    ASSERT_TRUE(parser);
}

TEST(HttpParser__Response, rvalue_callbacks)
{
    HttpParser::OnHeadersComplete on_headers_complete;
    HttpParser::OnBody on_body;
    HttpParser::OnComplete on_complete;

    auto parser = HttpParser::create(
                std::move(on_headers_complete),
                std::move(on_body),
                std::move(on_complete)
    );
    ASSERT_TRUE(parser);
}


class HttpParserMock
{
public:
    static shared_ptr<HttpParserMock> instance;
    static shared_ptr<HttpParserMock> create( function<void()> f)
    {
        instance->create_(f);
        return instance;
    }

    MOCK_METHOD1(create_, void(function<void()>));
};

shared_ptr<HttpParserMock> HttpParserMock::instance;

TEST(HttpParserMock, create)
{
    auto parser_ptr = make_shared<HttpParserMock>();
    HttpParserMock::instance = parser_ptr;

    function<void()> handler;
    EXPECT_CALL(*parser_ptr, create_(_))
            .WillOnce( SaveArg<0>(&handler) );

    size_t i = 0;
    auto cb = [&i]()
    {
        i++;
    };

    auto result_instance = HttpParserMock::create( std::move(cb) );

    ASSERT_EQ(result_instance, parser_ptr);

    handler();
    ASSERT_EQ(i, 1u);
}
