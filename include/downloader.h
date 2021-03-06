#pragma once

#include <string>

class StatusDownloader
{
public:
    StatusDownloader() :
        downloaded{0},
        size{0},
        state{State::Init},
        state_str{},
        redirect_uri{}
    {}

    std::size_t downloaded;
    std::size_t size;
    enum class State { Init, OnTheGo, Done, Failed, Redirect };
    State state;
    std::string state_str;
    std::string redirect_uri;
};

class Downloader
{
public:
    virtual bool run(const std::string& uri, const std::string& fname) = 0;
    virtual void stop() = 0;
    virtual const StatusDownloader& status() const = 0;
    virtual ~Downloader() = default;
};
