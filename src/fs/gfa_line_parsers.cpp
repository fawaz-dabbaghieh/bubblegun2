#include "fs/gfa_line_parsers.h"

#include <charconv>
#include <string_view>

namespace bubblegun::fs {

namespace {

constexpr std::size_t npos = std::string_view::npos;

bool parse_uint(std::string_view text, std::uint32_t& value) {
    value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    return error == std::errc{} && ptr == end;
}

std::size_t next_tab(std::string_view line, std::size_t start) {
    return line.find('\t', start);
}

}  // namespace

bool parse_segment_line(std::string_view line, SegmentFields& fields) {
    const std::size_t t1 = next_tab(line, 0);
    if (t1 == npos) {
        return false;
    }

    const std::size_t t2 = next_tab(line, t1 + 1);
    if (t2 == npos) {
        return false;
    }

    const std::size_t t3 = next_tab(line, t2 + 1);

    fields.id = line.substr(t1 + 1, t2 - t1 - 1);
    if (t3 == npos) {
        fields.sequence = line.substr(t2 + 1);
        fields.optional_fields = {};
    } else {
        fields.sequence = line.substr(t2 + 1, t3 - t2 - 1);
        fields.optional_fields = line.substr(t3 + 1);
    }

    return true;
}

bool parse_link_line(std::string_view line, LinkFields& fields) {
    const std::size_t t1 = next_tab(line, 0);
    if (t1 == npos) {
        return false;
    }
    const std::size_t t2 = next_tab(line, t1 + 1);
    if (t2 == npos) {
        return false;
    }
    const std::size_t t3 = next_tab(line, t2 + 1);
    if (t3 == npos) {
        return false;
    }
    const std::size_t t4 = next_tab(line, t3 + 1);
    if (t4 == npos) {
        return false;
    }
    const std::size_t t5 = next_tab(line, t4 + 1);
    if (t5 == npos) {
        return false;
    }
    const std::size_t t6 = next_tab(line, t5 + 1);

    const std::string_view overlap_field = (t6 == npos) ? line.substr(t5 + 1) : line.substr(t5 + 1, t6 - t5 - 1);

    fields.from_id = line.substr(t1 + 1, t2 - t1 - 1);
    fields.from_orient = line[t2 + 1];
    fields.to_id = line.substr(t3 + 1, t4 - t3 - 1);
    fields.to_orient = line[t4 + 1];

    if (overlap_field == "*") {
        fields.overlap = 0;
        return true;
    }

    std::string_view overlap_digits = overlap_field;
    if (!overlap_digits.empty() && overlap_digits.back() == 'M') {
        overlap_digits.remove_suffix(1);
    }

    return parse_uint(overlap_digits, fields.overlap);
}

std::uint32_t segment_length(std::string_view sequence, std::string_view optional_fields) {
    if (sequence != "*") {
        return static_cast<std::uint32_t>(sequence.size());
    }

    constexpr std::string_view ln_tag = "LN:i:";
    std::size_t search_from = 0;
    while (search_from < optional_fields.size()) {
        std::size_t field_end = optional_fields.find('\t', search_from);
        if (field_end == npos) {
            field_end = optional_fields.size();
        }

        const std::string_view field = optional_fields.substr(search_from, field_end - search_from);
        if (field.substr(0, ln_tag.size()) == ln_tag) {
            std::uint32_t length = 0;
            if (parse_uint(field.substr(ln_tag.size()), length)) {
                return length;
            }
        }

        search_from = field_end + 1;
    }

    return 0;
}

}  // namespace bubblegun::fs
