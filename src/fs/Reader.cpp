#include "fs/Reader.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace bubblegun::fs {

namespace {

std::string_view maybe_strip_cr(std::string_view line, bool strip_cr) {
    if (strip_cr && !line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

Reader::Reader() {
    ensure_buffer_allocated();
}

Reader::Reader(const Options& options)
    : options_(options) {
    ensure_buffer_allocated();
}

Reader::~Reader() {
    close();
}

void Reader::ensure_buffer_allocated() {
    if (!buffer_.empty()) {
        return;
    }
    if (options_.read_size == 0) {
        options_.read_size = 64 * 1024;
    }
    buffer_.resize(options_.read_size * 2 + 1);
}

bool Reader::open(const std::string& path) {
    close();
    ensure_buffer_allocated();

    last_errno_ = 0;
    cursor_ = 0;
    end_ = 0;
    eof_ = false;
    line_number_ = 0;
    long_line_.clear();
    long_ready_ = false;
    is_gzip_ = false;
    gzip_eof_ = false;
    zlib_initialized_ = false;
    zstream_ = {};

    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) {
        last_errno_ = errno;
        return false;
    }

    unsigned char magic[2] = {0, 0};
    const ssize_t read_count = ::read(fd_, magic, sizeof(magic));
    if (read_count < 0) {
        last_errno_ = errno;
        close();
        return false;
    }

    if (read_count == static_cast<ssize_t>(sizeof(magic)) && magic[0] == 0x1f && magic[1] == 0x8b) {
        is_gzip_ = true;
    }

    if (::lseek(fd_, 0, SEEK_SET) < 0) {
        last_errno_ = errno;
        close();
        return false;
    }

    if (is_gzip_) {
        if (inflateInit2(&zstream_, 15 + 16) != Z_OK) {
            last_errno_ = EINVAL;
            close();
            return false;
        }
        zlib_initialized_ = true;
        gzip_input_.resize(options_.read_size);
    }

    return true;
}

void Reader::close() {
    if (zlib_initialized_) {
        inflateEnd(&zstream_);
        zlib_initialized_ = false;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }

    is_gzip_ = false;
    gzip_eof_ = false;
    cursor_ = 0;
    end_ = 0;
    eof_ = false;
    long_line_.clear();
    long_ready_ = false;
}

void Reader::report_progress() const {
    if (options_.progress_every == 0) {
        return;
    }
    if ((line_number_ % options_.progress_every) == 0) {
        std::cerr << "Read " << line_number_ << " lines" << std::endl;
    }
}

bool Reader::refill() {
    return is_gzip_ ? refill_gzip() : refill_plain();
}

bool Reader::refill_plain() {
    if (eof_) {
        return true;
    }

    const std::size_t remainder = (end_ > cursor_) ? (end_ - cursor_) : 0;
    if (remainder != 0 && cursor_ != 0) {
        std::memmove(buffer_.data(), buffer_.data() + cursor_, remainder);
    }
    cursor_ = 0;
    end_ = remainder;

    const std::size_t capacity = buffer_.size() - end_;
    const std::size_t requested = std::min(options_.read_size, capacity);

    const ssize_t read_count = ::read(fd_, buffer_.data() + end_, requested);
    if (read_count < 0) {
        last_errno_ = errno;
        return false;
    }
    if (read_count == 0) {
        eof_ = true;
        return true;
    }

    end_ += static_cast<std::size_t>(read_count);
    return true;
}

bool Reader::refill_gzip() {
    if (eof_) {
        return true;
    }

    const std::size_t remainder = (end_ > cursor_) ? (end_ - cursor_) : 0;
    if (remainder != 0 && cursor_ != 0) {
        std::memmove(buffer_.data(), buffer_.data() + cursor_, remainder);
    }
    cursor_ = 0;
    end_ = remainder;

    while (end_ < buffer_.size()) {
        if (zstream_.avail_in == 0 && !gzip_eof_) {
            const ssize_t read_count = ::read(fd_, gzip_input_.data(), gzip_input_.size());
            if (read_count < 0) {
                last_errno_ = errno;
                return false;
            }
            if (read_count == 0) {
                gzip_eof_ = true;
            } else {
                zstream_.next_in = gzip_input_.data();
                zstream_.avail_in = static_cast<uInt>(read_count);
            }
        }

        if (zstream_.avail_in == 0 && gzip_eof_) {
            eof_ = true;
            return true;
        }

        zstream_.next_out = reinterpret_cast<Bytef*>(buffer_.data() + end_);
        zstream_.avail_out = static_cast<uInt>(buffer_.size() - end_);

        const int inflate_code = inflate(&zstream_, Z_NO_FLUSH);
        const std::size_t produced = (buffer_.size() - end_) - zstream_.avail_out;
        end_ += produced;

        if (inflate_code == Z_STREAM_END) {
            uInt remaining = zstream_.avail_in;
            Bytef* next_in = zstream_.next_in;

            inflateEnd(&zstream_);
            zlib_initialized_ = false;

            if (remaining == 0) {
                const ssize_t read_count = ::read(fd_, gzip_input_.data(), gzip_input_.size());
                if (read_count < 0) {
                    last_errno_ = errno;
                    return false;
                }
                if (read_count == 0) {
                    gzip_eof_ = true;
                } else {
                    remaining = static_cast<uInt>(read_count);
                    next_in = gzip_input_.data();
                }
            }

            if (!gzip_eof_) {
                zstream_ = {};
                if (inflateInit2(&zstream_, 15 + 16) != Z_OK) {
                    last_errno_ = EINVAL;
                    return false;
                }
                zlib_initialized_ = true;
                zstream_.next_in = next_in;
                zstream_.avail_in = remaining;
                continue;
            }

            eof_ = true;
            return true;
        }

        if (inflate_code != Z_OK) {
            last_errno_ = EIO;
            return false;
        }

        if (produced != 0) {
            return true;
        }
    }

    return true;
}

bool Reader::ensure_eol_or_eof() {
    while (true) {
        if (cursor_ < end_) {
            const void* newline = std::memchr(buffer_.data() + cursor_, '\n', end_ - cursor_);
            if (newline != nullptr) {
                return true;
            }
        }

        if (eof_) {
            return true;
        }

        if (cursor_ == 0 && end_ == buffer_.size()) {
            long_line_.append(buffer_.data(), end_);
            end_ = 0;
        } else if (end_ == buffer_.size() && cursor_ < end_) {
            long_line_.append(buffer_.data() + cursor_, end_ - cursor_);
            cursor_ = 0;
            end_ = 0;
        }

        if (!refill()) {
            return false;
        }
    }
}

bool Reader::read_line(std::string_view& out) {
    out = {};

    if (!is_open()) {
        last_errno_ = EBADF;
        return false;
    }

    if (long_ready_) {
        long_line_.clear();
        long_ready_ = false;
    }

    if (!ensure_eol_or_eof()) {
        return false;
    }

    if (cursor_ == end_ && eof_) {
        return false;
    }

    const char* start = buffer_.data() + cursor_;
    const void* newline = std::memchr(start, '\n', end_ - cursor_);
    if (newline == nullptr) {
        if (!long_line_.empty()) {
            long_line_.append(start, end_ - cursor_);
            out = maybe_strip_cr(std::string_view(long_line_), options_.strip_cr);
            long_ready_ = true;
        } else {
            out = maybe_strip_cr(std::string_view(start, end_ - cursor_), options_.strip_cr);
        }
        cursor_ = end_;
    } else {
        const char* end_of_line = static_cast<const char*>(newline);
        const std::size_t line_length = static_cast<std::size_t>(end_of_line - start);
        if (!long_line_.empty()) {
            long_line_.append(start, line_length);
            out = maybe_strip_cr(std::string_view(long_line_), options_.strip_cr);
            long_ready_ = true;
        } else {
            out = maybe_strip_cr(std::string_view(start, line_length), options_.strip_cr);
        }
        cursor_ = static_cast<std::size_t>(end_of_line - buffer_.data()) + 1;
    }

    ++line_number_;
    report_progress();
    return true;
}

}  // namespace bubblegun::fs
