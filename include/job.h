#pragma once

#include <string>
#include <memory>

class Downloader;
class Job
{
public:
    template< typename String,
              typename = std::enable_if_t< std::is_convertible<String, std::string>::value, String> >
    Job(String&& fname_)
        : id{ generate_id() },
          fname{ std::forward<String>(fname_) },
          redirect_count{0}
    {}

    const std::size_t id;
    const std::string fname;
    std::size_t redirect_count;
    std::shared_ptr<Downloader> downloader;

    Job() = delete;
    Job(const Job&) = delete;
    Job& operator= (const Job&) = delete;

    Job(Job&&) = default;
    Job& operator= (Job&&) = default;
    ~Job() = default;

private:
    static std::size_t generate_id()
    {
        static std::size_t id = 1;
        return id++;
    }
};
