#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <type_traits>

extern "C" {
    #include <http_parser.h>
}

class HttpParser
{
public:
    using OnData = std::function<void(std::unique_ptr<char[]>, std::size_t)>;

    template< typename T >
    static
    typename std::enable_if_t< std::is_convertible<T, OnData>::value, std::unique_ptr<HttpParser> >
    create(T&& on_data)
    {
        return std::unique_ptr<HttpParser>{ new HttpParser( std::forward<T>(on_data) ) };
    }

    struct ResponseParseResult
    {
        enum class State { InProgress, Done, Redirect, Error };
        State state;
        std::string redirect_uri;
        std::string err_str;
        std::size_t content_length;
    };

    const ResponseParseResult response_parse(std::unique_ptr<char[]>, std::size_t);

    struct UriParseResult;
    static std::unique_ptr<UriParseResult> uri_parse(const std::string&);

private:

    template < typename T >
    explicit HttpParser(T&& on_data)
        : cb_on_data{ std::forward<T>(on_data) }
    {
        http_parser_init(&parser, HTTP_RESPONSE);
        parser.data = this;

        http_parser_settings_init(&parser_settings);
        parser_settings.on_status = &HttpParser::on_status;
        parser_settings.on_header_field = &HttpParser::on_header_field;
        parser_settings.on_header_value = &HttpParser::on_header_value;
        parser_settings.on_headers_complete = &HttpParser::on_headers_complete;
        parser_settings.on_body = &HttpParser::on_body;
        parser_settings.on_message_complete = &HttpParser::on_message_complete;

        result.state = ResponseParseResult::State::InProgress;
        result.content_length = 0;
    }

    OnData cb_on_data;

    http_parser parser;
    http_parser_settings parser_settings;

    ResponseParseResult result;

    std::map<std::string, std::string> headers;
    std::string field_header, value_header;
    enum class ModeHeader { Field, Value };
    ModeHeader mode_header = ModeHeader::Field;

    bool redirect = false;

    static int on_status(http_parser*, const char*, std::size_t);
    static int on_header_field(http_parser*, const char*, std::size_t);
    static int on_header_value(http_parser*, const char*, std::size_t);
    static int on_headers_complete(http_parser*);
    static int on_body(http_parser*, const char*, std::size_t);
    static int on_message_complete(http_parser*);
    void stop(ResponseParseResult::State);

public:
    HttpParser() = delete;
    HttpParser(const HttpParser&) = delete;
    HttpParser& operator= (const HttpParser&) = delete;
};

struct HttpParser::UriParseResult
{
    std::string proto;
    std::string host;
    unsigned short port;
    std::string query;
    std::string username;
    std::string password;
};
