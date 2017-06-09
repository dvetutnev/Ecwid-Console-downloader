#pragma once

#include <memory>

struct DataChunk
{
    DataChunk(std::unique_ptr<char[]> data_, std::size_t length_, std::size_t offset_ = 0) noexcept
        : data{ std::move(data_) },
          length{length_},
          offset{offset_}
    {}

    DataChunk() = delete;
    DataChunk(const DataChunk&) = delete;
    DataChunk& operator= (const DataChunk&) = delete;
    DataChunk(DataChunk&& other) = delete;
    DataChunk& operator= (DataChunk&& other) = delete;
    ~DataChunk() = default;

    std::unique_ptr<char[]> data;
    const std::size_t length;
    std::size_t offset;
};
