#ifndef BUBBLEGUN_CPP_FS_GFA_LINE_PARSERS_H
#define BUBBLEGUN_CPP_FS_GFA_LINE_PARSERS_H

#include <cstdint>
#include <string_view>

namespace bubblegun::fs {

struct SegmentFields {
    std::string_view id;
    std::string_view sequence;
    std::string_view optional_fields;
};

struct LinkFields {
    std::string_view from_id;
    char from_orient = '+';
    std::string_view to_id;
    char to_orient = '+';
    std::uint32_t overlap = 0;
};

bool parse_segment_line(std::string_view line, SegmentFields& fields);
bool parse_link_line(std::string_view line, LinkFields& fields);
std::uint32_t segment_length(std::string_view sequence, std::string_view optional_fields);

}  // namespace bubblegun::fs

#endif  // BUBBLEGUN_CPP_FS_GFA_LINE_PARSERS_H
