#pragma once

#include <memory>

struct DataChunk
{
    DataChunk(std::unique_ptr<char[]> data_ = nullptr, std::size_t length_ = 0, std::size_t offset_ = 0) noexcept
        : data{ std::move(data_) },
          length{length_},
          offset{offset_}
    {}

    DataChunk(DataChunk&& other) noexcept
        : data{ std::move(other.data) },
          length{other.length},
          offset{other.offset}
    {}

    DataChunk& operator= (DataChunk&& other) noexcept
    {
        data = std::move(other.data);
        length = other.length;
        offset = other.offset;
        return *this;
    }

    DataChunk(const DataChunk&) = delete;
    DataChunk& operator= (const DataChunk&) = delete;
    ~DataChunk() = default;

    operator bool() const noexcept { return data != nullptr; }
    void reset() noexcept { data.reset(); }
    const char* get() const noexcept { return data.get(); }

    std::unique_ptr<char[]> data;
    std::size_t length;
    std::size_t offset;
};
