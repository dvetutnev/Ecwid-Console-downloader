#pragma once

#include <memory>
#include <string>

class HttpParser
{
public:
    struct UriParseResult;
    static std::shared_ptr<UriParseResult> uri_parse(const std::string&);

    struct ResponseParseResult;
    static std::shared_ptr<ResponseParseResult> respone_parse(const std::string&);

    class BodyDecoder;
    class BodyDecoderContentLength;
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

struct HttpParser::ResponseParseResult
{
    bool is_done;
    std::size_t http_code;
    std::string http_reason;
    const char* body;
    std::size_t body_length;
    std::string redirect_uri;
    std::unique_ptr<BodyDecoder> body_decoder;
};

struct DataChunk
{
    std::shared_ptr<char> buffer;
    std::size_t length;
};

class HttpParser::BodyDecoder
{
public:

    struct Result
    {
        DataChunk data;
        bool is_done;
    };

    virtual Result operator() (const char*, std::size_t) = 0;
    virtual ~BodyDecoder() = default;
};

class HttpParser::BodyDecoderContentLength : public BodyDecoder
{
public:
    BodyDecoderContentLength() = delete;
    BodyDecoderContentLength(const BodyDecoderContentLength&) = delete;
    BodyDecoderContentLength& operator= (const BodyDecoderContentLength&) = delete;

    BodyDecoderContentLength(std::size_t len)
        : left_length{len}
    {}

    virtual Result operator() (const char*, std::size_t) override final;

private:
    std::size_t left_length;
};
