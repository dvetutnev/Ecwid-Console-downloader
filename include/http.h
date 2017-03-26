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
    struct OnHeadersComplete_Args
    {
        std::size_t http_code = 0;
        std::string http_reason;
        std::size_t content_length = 0;
        std::map< std::string, std::string > headers;
    };
    using OnHeadersComplete = std::function<bool( std::unique_ptr<OnHeadersComplete_Args> )>;
    using OnBody = std::function<void( const char*, std::size_t )>;
    using OnComplete = std::function<void()>;

    template< typename T1, typename T2, typename T3 >
    static
    typename std::enable_if_t<
        std::is_convertible<T1, OnHeadersComplete>::value &&
        std::is_convertible<T2, OnBody>::value &&
        std::is_convertible<T3, OnComplete>::value,
    std::unique_ptr<HttpParser> >
    create( T1&& on_headers_complete, T2&& on_body, T3&& on_complete )
    {
        return std::unique_ptr<HttpParser>{ new HttpParser(
                        std::forward<T1>(on_headers_complete),
                        std::forward<T2>(on_body),
                        std::forward<T3>(on_complete) )
        };
    }

    class Error
    {
    public:
        explicit Error(enum http_errno = HPE_OK) noexcept;
        Error& operator= (const Error&) noexcept;
        explicit operator bool () const noexcept;
        enum http_errno code() const noexcept;
        const char* str() const noexcept;
    private:
        enum http_errno m_code;
    };

    Error response_parse(const char*, std::size_t);

    struct UriParseResult;
    static std::shared_ptr<UriParseResult> uri_parse(const std::string&);

private:
    template< typename T1, typename T2, typename T3 >
    HttpParser(T1&& on_headers_complete, T2&& on_body, T3&& on_complete )
        : cb_on_headers_complete{ std::forward<T1>(on_headers_complete) },
          cb_on_body{ std::forward<T2>(on_body) },
          cb_on_complete{ std::forward<T3>(on_complete) }
    {
        http_parser_init(&parser, HTTP_RESPONSE);
        parser.data = this;

        http_parser_settings_init(&settings);
        settings.on_status = &HttpParser::on_status;
        settings.on_header_field = &HttpParser::on_header_field;
        settings.on_header_value = &HttpParser::on_header_value;
        settings.on_headers_complete = &HttpParser::on_headers_complete;
        settings.on_body = &HttpParser::on_body;
        settings.on_message_complete = &HttpParser::on_complete;
    }

    OnHeadersComplete cb_on_headers_complete;
    OnBody cb_on_body;
    OnComplete cb_on_complete;

    http_parser parser;
    http_parser_settings settings;

    std::unique_ptr<OnHeadersComplete_Args> headers_complete_args;
    std::string field_header, value_header;
    enum class ModeHeader { Filed, Value };
    ModeHeader mode_header = ModeHeader::Filed;

    static int on_status(http_parser*, const char*, std::size_t);
    static int on_header_field(http_parser*, const char*, std::size_t);
    static int on_header_value(http_parser*, const char*, std::size_t);
    static int on_headers_complete(http_parser*);
    static int on_body(http_parser*, const char*, std::size_t);
    static int on_complete(http_parser*);

public:
    HttpParser() = delete;
    HttpParser(const HttpParser&) = delete;
    HttpParser& operator= (const HttpParser&) = delete;
};

struct HttpParser::UriParseResult
{
    std::string proto;
    std::string host;
    std::size_t port;
    std::string query;
    std::string username;
    std::string password;
};
