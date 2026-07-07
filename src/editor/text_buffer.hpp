#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace textlt {

class TextBuffer {
public:
    TextBuffer();
    explicit TextBuffer(std::vector<std::string> lines);

    const std::vector<std::string>& Lines() const;
    std::vector<std::string>& MutableLines();
    const std::string& Line(size_t index) const;
    std::string& MutableLine(size_t index);

    void SetLines(std::vector<std::string> lines);
    void SetText(const std::string& text);
    std::string ToText(const std::string& line_ending) const;

    size_t LineCount() const;
    bool Empty() const;
    void EnsureValid();

    bool Dirty() const;
    bool& DirtyFlag();
    void SetDirty(bool dirty);
    void MarkDirty();

    std::uint64_t Version() const;
    void Touch();

private:
    std::vector<std::string> lines_{""};
    bool dirty_ = false;
    std::uint64_t version_ = 0;
};

} // namespace textlt
