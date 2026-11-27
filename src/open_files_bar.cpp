#include "open_files_bar.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "editor/document_session.hpp"
#include "editor/document_workspace.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/string.hpp"
#include "path_utils.hpp"
#include "ui_horizontal_scroll.hpp"

namespace textlt {
namespace {

using ftxui::string_width;

std::string SingleLine(std::string label) {
    for (char& character : label) {
        if ((character >= 0 && character < ' ') || character == 127) character = '?';
    }
    return label;
}

std::string TruncateMiddle(const std::string& text, int width, int extension_width = 0) {
    if (width <= 0) return {};
    if (string_width(text) <= width) return text;

    // FTXUI emits a continuation cell for wide glyphs. Join its combining
    // marks to the preceding glyph before taking either side of the label.
    std::vector<std::string> glyphs;
    for (auto& glyph : ftxui::Utf8ToGlyphs(text)) {
        if (string_width(glyph) > 0) {
            glyphs.push_back(std::move(glyph));
        } else if (!glyphs.empty()) {
            glyphs.back() += glyph;
        }
    }

    const int budget = width - 1;
    const int suffix_width = std::max(budget / 2, std::min(extension_width, budget));
    std::string suffix;
    int used = 0;
    for (auto glyph = glyphs.rbegin(); glyph != glyphs.rend(); ++glyph) {
        const int columns = string_width(*glyph);
        if (used + columns > suffix_width) break;
        suffix.insert(0, *glyph);
        used += columns;
    }

    std::string prefix;
    for (const auto& glyph : glyphs) {
        const int columns = string_width(glyph);
        if (used + columns > budget) break;
        prefix += glyph;
        used += columns;
    }
    return prefix + "…" + suffix;
}

std::vector<size_t> OpenIndices(const DocumentWorkspace& workspace) {
    std::vector<size_t> indices;
    for (size_t index = 0; index < workspace.SessionCount(); ++index) {
        if (workspace.HasSessionAt(index)) indices.push_back(index);
    }
    return indices;
}

std::map<size_t, std::string> FileLabels(const DocumentWorkspace& workspace,
    const std::vector<size_t>& indices, int width) {
    std::map<std::string, std::vector<size_t>> by_title;
    for (size_t index : indices) {
        by_title[SingleLine(workspace.SessionAt(index)->DisplayTitle())].push_back(index);
    }

    std::map<size_t, std::string> labels;
    for (const auto& [title, group] : by_title) {
        std::map<std::string, int> parent_counts;
        for (size_t index : group) {
            const auto parent = workspace.SessionAt(index)->path.parent_path();
            ++parent_counts[SingleLine(Utf8FromPath(parent.filename()))];
        }

        std::set<std::string> used_labels;
        for (size_t index : group) {
            const auto& session = *workspace.SessionAt(index);
            const int extension_width = string_width(SingleLine(Utf8FromPath(session.path.extension())));
            std::string label = TruncateMiddle(title, width, extension_width);
            if (group.size() > 1 && width >= 5) {
                const auto parent_path = session.path.parent_path();
                const auto nearest = SingleLine(Utf8FromPath(parent_path.filename()));
                const auto parent = parent_counts[nearest] > 1
                    ? SingleLine(Utf8FromPath(parent_path)) : nearest;
                const int minimum_name_width = std::min(extension_width + 2, width - 2);
                const int context_width = std::min((width - 1) / 2, width - minimum_name_width - 1);
                std::string context = TruncateMiddle(parent, context_width);
                auto format = [&] {
                    return context + "/" +
                        TruncateMiddle(title, width - string_width(context) - 1, extension_width);
                };
                label = format();
                if (context.empty() || used_labels.count(label)) {
                    const std::string tag = "#" + std::to_string(index + 1);
                    context = TruncateMiddle(parent, context_width - string_width(tag)) + tag;
                    label = TruncateMiddle(format(), width, extension_width);
                }
            }
            used_labels.insert(label);
            labels.emplace(index, std::move(label));
        }
    }
    return labels;
}

std::string PadToWidth(std::string label, int width) {
    label.append(std::max(0, width - string_width(label)), ' ');
    return label;
}

} // namespace

class OpenFilesBar::TabRows final : public ftxui::Node {
public:
    explicit TabRows(OpenFilesBar& bar) : bar_(bar) {}

private:
    void ComputeRequirement() override {
        requirement_.min_y = kTabRows + kBottomSpacing;
    }

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        bar_.ClearHitboxes();
        bar_.box_ = box;
        children_.clear();
        const int width = BoxWidth(box);
        if (width == 0) return;

        const auto open = OpenIndices(bar_.workspace_);
        if (open.empty()) return;
        auto indices = open;

        int tab_width = std::min(width, kMaxTabWidth);
        const int min_width = std::min(width, kMinTabWidth);
        const auto per_row = [width](int tab_width) {
            return 1 + (width - tab_width) / (tab_width + kTabSpacing);
        };
        const int rows = std::min(kTabRows, box.y_max - box.y_min + 1);
        while (tab_width > min_width &&
            indices.size() > static_cast<size_t>(rows * per_row(tab_width))) {
            --tab_width;
        }

        const int columns = per_row(tab_width);
        const auto capacity = static_cast<size_t>(rows * columns);
        const bool overflow = indices.size() > capacity;
        const bool has_more_button = overflow && capacity > 1;
        const size_t shown = overflow ? capacity - (has_more_button ? 1 : 0) : indices.size();
        const size_t active = bar_.workspace_.ActiveSessionIndex();
        if (overflow) {
            const auto hidden_start = indices.begin() + static_cast<std::ptrdiff_t>(shown);
            const auto active_hidden = std::find(hidden_start, indices.end(), active);
            if (active_hidden != indices.end()) {
                std::iter_swap(hidden_start - 1, active_hidden);
            }
            bar_.hidden_.assign(hidden_start, indices.end());
            std::sort(bar_.hidden_.begin(), bar_.hidden_.end());
            indices.resize(shown);
        }

        const bool has_close = tab_width >= 4;
        const int fixed_width = has_close ? (tab_width >= 6 ? 5 : 3) : 1;
        const int label_width = tab_width - fixed_width;
        const auto labels = FileLabels(bar_.workspace_, open, label_width);
        for (size_t slot = 0; slot < shown; ++slot) {
            const size_t index = indices[slot];
            std::string caption(1, bar_.workspace_.SessionAt(index)->is_dirty ? '*' : ' ');
            if (fixed_width == 5) caption += ' ';
            caption += PadToWidth(labels.at(index), label_width);
            if (has_close) caption += fixed_width == 5 ? " ┊×" : "┊×";
            AddTab(slot, columns, tab_width, index, has_close, std::move(caption), index == active);
        }

        if (has_more_button) {
            const auto count = std::to_string(bar_.hidden_.size());
            const std::string label = "+" + count + " more";
            const std::string compact = "+" + count;
            const auto caption = string_width(label) <= tab_width ? label
                : string_width(compact) <= tab_width ? compact : "+";
            AddTab(shown, columns, tab_width, std::nullopt, false,
                PadToWidth(caption, tab_width), false);
        }
    }

    void AddTab(size_t slot, int columns, int width, std::optional<size_t> index,
        bool has_close, const std::string& caption, bool active) {
        using namespace ftxui;
        const int x = box_.x_min + static_cast<int>(slot % columns) * (width + kTabSpacing);
        const int y = box_.y_min + static_cast<int>(slot / columns);
        const Box tab_box{x, x + width - 1, y, y};
        bar_.tabs_.push_back({tab_box, index, has_close});

        auto tab = text(caption) | color(active ? bar_.theme_.modal_selected_item_fg
                                                : FileListTextColor(bar_.theme_));
        if (active) tab = tab | bgcolor(bar_.theme_.modal_selected_item_bg);
        tab->ComputeRequirement();
        tab->SetBox(tab_box);
        children_.push_back(std::move(tab));
    }

    OpenFilesBar& bar_;
};

OpenFilesBar::OpenFilesBar(const DocumentWorkspace& workspace, const Theme& theme,
    SessionAction activate, SessionAction close)
    : workspace_(workspace), theme_(theme), activate_(std::move(activate)), close_(std::move(close)),
      file_list_(theme, [this](const std::string& index) {
          activate_(std::stoull(index));
      }) {}

ftxui::Element OpenFilesBar::OnRender() {
    return std::make_shared<TabRows>(*this) |
        ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kTabRows + kBottomSpacing);
}

bool OpenFilesBar::OnEvent(ftxui::Event event) {
    if (file_list_.OnEvent(event)) return true;
    if (!event.is_mouse()) return false;
    const auto& mouse = event.mouse();
    if (!box_.Contain(mouse.x, mouse.y)) return false;
    auto capture = CaptureMouse(event);
    if (!capture) return false;

    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
        for (const auto& tab : tabs_) {
            if (!tab.box.Contain(mouse.x, mouse.y)) continue;
            if (!tab.session_index) {
                OpenFileList(hidden_, tab.box.x_min, tab.box.y_max + 1);
            } else if (tab.has_close && mouse.x == tab.box.x_max) {
                close_(*tab.session_index);
            } else {
                activate_(*tab.session_index);
            }
            break;
        }
    }
    return true;
}

void OpenFilesBar::OpenFileList(const std::vector<size_t>& indices, int x, int y) {
    std::vector<ContextMenuComponent::Item> items;
    // Leave room for the marker, the menu's padding, and its frame.
    const int label_width = std::max(1, ftxui::Terminal::Size().dimx - 6);
    for (size_t index : indices) {
        const auto* session = workspace_.SessionAt(index);
        if (!session) continue;
        const std::string title = session->IsMemoryOnly()
            ? session->DisplayTitle() + " #" + std::to_string(index + 1)
            : Utf8FromPath(session->path) + session->git_compare_label;
        const std::string label = TruncateMiddle(SingleLine(title), label_width,
            string_width(session->DisplayTitle()));
        items.push_back({std::to_string(index), (session->is_dirty ? "* " : "  ") + label});
    }
    file_list_.Open(x, y, std::move(items));
}

void OpenFilesBar::OpenFileList() {
    OpenFileList(OpenIndices(workspace_), 0, box_.y_min + 1);
}

void OpenFilesBar::ClearHitboxes() {
    box_ = {1, 0, 1, 0};
    tabs_.clear();
    hidden_.clear();
}

void OpenFilesBar::CloseFileList() {
    file_list_.Close();
}

bool OpenFilesBar::IsFileListOpen() const {
    return file_list_.IsOpen();
}

ftxui::Element OpenFilesBar::RenderFileList() {
    return file_list_.Render();
}

} // namespace textlt
