#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

struct ZSTD_DCtx_s;  // libzstd's ZSTD_DStream

// Sequential line reader over a zstd-compressed text file.
class ZSTDReader {
    std::string filepath_;
    std::FILE* file_ = nullptr;
    ZSTD_DCtx_s* dstream_ = nullptr;

    char* in_buf_;      // raw compressed bytes read from file_
    char* out_buf_;     // decompressed bytes not yet handed to the caller
    std::size_t in_pos_ = 0, in_size_ = 0;
    std::size_t out_pos_ = 0, out_size_ = 0;

    bool input_exhausted_ = false;
    uint64_t line_number_ = 0;

    // Decompresses the next non-empty chunk into out_buf_
    // Returns false when EOF is reached
    bool decode_more();

public:
    ZSTDReader(const char* filepath);
    ~ZSTDReader();

    bool reached_file_end();

    // Reads next line into *line. Returns false when EOF is reached.
    bool get_next_line(std::string* line);
    uint64_t get_line_number() {return line_number_;};
    uint64_t goto_line(uint64_t target);    // Returns the actual line reached, not always the same as target.
    void rewind();
};
