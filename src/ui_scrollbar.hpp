#pragma once

#include <algorithm>

namespace textlt {

struct ScrollbarMetrics {
    int length;
    int max_offset;
    int thumb_length = 0;
    int thumb_start = 0;

    ScrollbarMetrics(int viewport_length, int content_length, int offset)
        : length(viewport_length), max_offset(std::max(0, content_length - length)) {
        if (length > 0 && max_offset > 0) {
            thumb_length = std::max(1, static_cast<int>(1LL * length * length / content_length));
            thumb_start = static_cast<int>(1LL * offset * Travel() / max_offset);
        }
    }

    int Travel() const { return length - thumb_length; }

    int OffsetForThumb(int start) const {
        return static_cast<int>((1LL * start * max_offset + Travel() - 1) / Travel());
    }
};

} // namespace textlt
