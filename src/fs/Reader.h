#ifndef BUBBLEGUN_CPP_FS_READER_H
#define BUBBLEGUN_CPP_FS_READER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace bubblegun::fs {

class Reader {
public:
    struct Options {
        std::size_t read_size = 64 * 1024;
        bool strip_cr = false;
        std::uint64_t progress_every = 0;
    };

    Reader();
    explicit Reader(const Options& options);

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    ~Reader();

    bool open(const std::string& path);
    void close();

    bool read_line(std::string_view& out);

    [[nodiscard]] bool is_open() const {
        return fd_ >= 0;
    }

    [[nodiscard]] int last_error_number() const {
        return last_errno_;
    }

    [[nodiscard]] std::uint64_t line_number() const {
        return line_number_;
    }

private:
    void ensure_buffer_allocated();
    void report_progress() const;
    bool refill();
    bool refill_plain();
    bool refill_gzip();
    bool ensure_eol_or_eof();

    Options options_{};

    int fd_ = -1;
    int last_errno_ = 0;

    std::vector<char> buffer_;
    std::size_t cursor_ = 0;
    std::size_t end_ = 0;
    bool eof_ = false;

    std::uint64_t line_number_ = 0;

    std::string long_line_;
    bool long_ready_ = false;

    bool is_gzip_ = false;
    bool gzip_eof_ = false;
    bool zlib_initialized_ = false;
    z_stream zstream_{};
    std::vector<unsigned char> gzip_input_;
};

}  // namespace bubblegun::fs

#endif  // BUBBLEGUN_CPP_FS_READER_H
