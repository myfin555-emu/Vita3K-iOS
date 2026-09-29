#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ime {
// Vita limits count UTF-16 code units. Never cut a surrogate pair in half.
inline std::size_t text_length(std::u16string_view text, std::size_t limit) {
    std::size_t length = std::min(text.size(), limit);
    if (length && length < text.size() && text[length - 1] >= 0xD800
        && text[length - 1] <= 0xDBFF && text[length] >= 0xDC00 && text[length] <= 0xDFFF)
        --length;
    return length;
}
struct EditDelta {
    std::size_t index;
    std::int32_t length_change;
};

// Compare against the last text delivered to the guest, not the previous
// UIKit keystroke: several edits can be coalesced before sceImeUpdate runs.
inline EditDelta describe_edit(std::u16string_view previous, std::u16string_view current) {
    std::size_t prefix = 0;
    while (prefix < previous.size() && prefix < current.size() && previous[prefix] == current[prefix])
        ++prefix;
    prefix = std::min(text_length(previous, prefix), text_length(current, prefix));
    return { prefix, static_cast<std::int32_t>(current.size()) - static_cast<std::int32_t>(previous.size()) };
}
} // namespace ime
