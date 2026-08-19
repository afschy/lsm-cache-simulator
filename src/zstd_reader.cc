#include "zstd_reader.h"

#include <zstd.h>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <iostream>

constexpr size_t kInBufCapacity = ZSTD_BLOCKSIZE_MAX + 3;
constexpr size_t kOutBufCapacity = ZSTD_BLOCKSIZE_MAX;

ZSTDReader::ZSTDReader(const char* filepath) : filepath_(filepath == nullptr ? "" : filepath) {
    in_buf_ = new char[kInBufCapacity];
    out_buf_ = new char[kOutBufCapacity];

    if (filepath == nullptr) {
        std::cerr << "Invalid file path\n";
        exit(1);
    }

    file_ = std::fopen(filepath_.c_str(), "rb");
    if (file_ == nullptr) {
        std::cerr << "Invalid file path\n";
        exit(1);
    }

    dstream_ = ZSTD_createDStream();
    if (dstream_ == nullptr) {
        std::fclose(file_);
        file_ = nullptr;
        std::cerr << "ZSTD_createDStream failed\n";
        exit(1);
    }

    const size_t rc = ZSTD_initDStream(dstream_);
    if (ZSTD_isError(rc)) {
        ZSTD_freeDStream(dstream_);
        std::fclose(file_);
        dstream_ = nullptr;
        file_ = nullptr;
        std::cerr << "ZSTD_initDStream failed\n";
        exit(1);
    }
}

ZSTDReader::~ZSTDReader() {
    if (dstream_ != nullptr) ZSTD_freeDStream(dstream_);
    if (file_ != nullptr) std::fclose(file_);
    delete in_buf_;
    delete out_buf_;
}

bool ZSTDReader::decode_more() {
    out_pos_ = 0;
    out_size_ = 0;

    // A decompress call can legitimately produce no output (e.g. it only consumed
    // a frame header), so keep going until there are bytes or the input runs out.
    while (out_size_ == 0) {
        if (in_pos_ == in_size_) {
            if (input_exhausted_) return false;
            in_size_ = std::fread(in_buf_, 1, kInBufCapacity, file_);
            in_pos_ = 0;
            if (in_size_ == 0) {
                if (std::ferror(file_) != 0) {
                    std::cerr << "File read error\n";
                    exit(1);
                }
                input_exhausted_ = true;
                return false;
            }
        }

        ZSTD_inBuffer in{in_buf_, in_size_, in_pos_};
        ZSTD_outBuffer out{out_buf_, kOutBufCapacity, 0};
        const size_t rc = ZSTD_decompressStream(dstream_, &out, &in);
        in_pos_ = in.pos;

        if (ZSTD_isError(rc)) {
            ZSTD_DCtx_reset(dstream_, ZSTD_reset_session_only);
            input_exhausted_ = true;
            std::cerr << "Decompression Error\n";
        }

        out_size_ = out.pos;

        // rc == 0 means the current frame is complete and flushed. The input may
        // hold further frames, and starting one requires a fresh session.
        if (rc == 0) ZSTD_initDStream(dstream_);
    }
    return true;
}

bool ZSTDReader::get_next_line(std::string* line) {
    if (line != nullptr) line->clear();
    bool consumed_any = false;

    while (true) {
        if (out_pos_ == out_size_ && !decode_more()) {
            // End of input. Anything gathered so far is a final line that was not
            // terminated by a newline; it still counts as a line.
            if (consumed_any) {
                if (line != nullptr && !line->empty() && line->back() == '\r') line->pop_back();
                ++line_number_;
                return true;
            }
            return false;
        }

        const char* base = out_buf_;
        const size_t avail = out_size_ - out_pos_;
        const char* nl = static_cast<const char*>(std::memchr(base + out_pos_, '\n', avail));

        if (nl != nullptr) {
            size_t len = static_cast<size_t>(nl - (base + out_pos_));
            if (line != nullptr) {
                // Tolerate CRLF input by dropping the carriage return.
                const bool split_crlf = len == 0 && !line->empty() && line->back() == '\r';
                if (split_crlf) line->pop_back();
                if (len > 0 && base[out_pos_ + len - 1] == '\r') --len;
                line->append(base + out_pos_, len);
            }
            out_pos_ = nl - base + 1;
            ++line_number_;
            return true;
        }

        // No newline in this chunk: the line spans a buffer refill.
        if (line != nullptr) line->append(base + out_pos_, avail);
        out_pos_ = out_size_;
        consumed_any = consumed_any || avail > 0;
    }
}

bool ZSTDReader::reached_file_end() {
    if (out_pos_ < out_size_) return false;
    return !decode_more();
}

void ZSTDReader::rewind() {
    if (std::fseek(file_, 0, SEEK_SET) != 0) throw std::runtime_error("cannot seek in " + filepath_);
    std::clearerr(file_);

    const size_t rc = ZSTD_DCtx_reset(dstream_, ZSTD_reset_session_only);
    if (ZSTD_isError(rc)) {
        std::cerr << "Decompressor reset failed\n";
        exit(1);
    }

    in_pos_ = in_size_ = 0;
    out_pos_ = out_size_ = 0;
    input_exhausted_ = false;
    line_number_ = 0;
}

uint64_t ZSTDReader::goto_line(uint64_t target) {
    if (target < line_number_) rewind();

    while (line_number_ < target) {
        if (!get_next_line(nullptr)) break;
    }
    return line_number_;
}
