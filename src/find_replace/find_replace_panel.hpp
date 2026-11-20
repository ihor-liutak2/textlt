#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "find_replace/find_target.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt {

enum class FindReplaceTab {
    Find,
    Replace,
};

// Shared left-rail Find/Replace surface. It deliberately lives outside the
// FTXUI modal/layer stack: it replaces the normal sidebar and is routed by
// AppEventDispatcher while it owns focus.
class FindReplacePanel {
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using RedrawCallback = std::function<void()>;
    using CloseCallback = std::function<void()>;

    FindReplacePanel(const Theme* theme, StatusCallback status,
        RedrawCallback redraw, CloseCallback close);

    void SetTarget(FindTarget* target);
    FindTarget* Target() const { return target_; }

    void Open(FindReplaceTab tab);
    void Close();
    bool IsOpen() const { return open_; }

    void SetFocused(bool focused);
    bool HasFocus() const { return focused_; }
    bool Contains(int x, int y) const;

    // Re-runs the query immediately.
    void Refresh();
    // Checks target availability/content version once per frame and refreshes
    // when the underlying workspace changed.
    void Tick();

    ftxui::Element Render();
    bool OnEvent(ftxui::Event event);

private:
    enum class Field {
        FindTab,
        ReplaceTab,
        MatchCase,
        WholeWord,
        Query,
        Clear,
        Previous,
        Next,
        ReplaceInput,
        Replace,
        ReplaceAll,
        Results,
    };

    std::vector<Field> FocusOrder() const;
    void CycleFocus(int direction);
    void FocusField(Field field);
    void OnQueryChanged();
    void OnReplacementChanged();
    void EnsureSelectedVisible();
    void MoveSelection(int delta, bool activate = true);
    void ActivateSelected();
    void ReplaceSelected();
    void ReplaceAll();
    void ClearQuery();
    void ToggleMatchCase();
    void ToggleWholeWord();
    void Report(const std::string& message);
    void RequestRedraw();
    void HandleMouse(ftxui::Event event);
    bool HandleFieldKey(ftxui::Event event, std::string& value, std::size_t& cursor,
        bool& select_all, bool query_field);
    bool HandleKey(ftxui::Event event);
    void CycleTab();

    ftxui::Element RenderTabs();
    ftxui::Element RenderOptions();
    ftxui::Element RenderQueryField();
    ftxui::Element RenderReplacementField(const char* placeholder);
    ftxui::Element RenderButton(const std::string& label, Field field,
        bool enabled = true);
    ftxui::Element RenderOption(const std::string& label, bool selected, Field field);
    ftxui::Element RenderResults();

    const Theme* theme_ = nullptr;
    StatusCallback status_;
    RedrawCallback redraw_;
    CloseCallback close_;
    FindTarget* target_ = nullptr;

    bool open_ = false;
    bool focused_ = false;
    FindReplaceTab tab_ = FindReplaceTab::Find;
    Field focus_ = Field::Query;

    FindOptions options_;
    std::string query_;
    std::size_t query_cursor_ = 0;
    bool query_select_all_ = false;
    std::string replacement_;
    std::size_t replacement_cursor_ = 0;
    bool replacement_select_all_ = false;

    std::vector<FindResult> results_;
    bool truncated_ = false;
    std::size_t selected_ = 0;
    bool activated_ = false;
    std::size_t result_scroll_ = 0;
    std::size_t result_visible_rows_ = 10;

    std::uint64_t known_version_ = 0;
    bool known_available_ = false;
    bool have_snapshot_ = false;

    // Hitboxes.
    ftxui::Box panel_box_;
    ftxui::Box find_tab_box_;
    ftxui::Box replace_tab_box_;
    ftxui::Box match_case_box_;
    ftxui::Box whole_word_box_;
    ftxui::Box query_box_;
    ftxui::Box clear_box_;
    ftxui::Box previous_box_;
    ftxui::Box next_box_;
    ftxui::Box replacement_box_;
    ftxui::Box replace_box_;
    ftxui::Box replace_all_box_;
    ftxui::Box results_view_box_;
    std::vector<ftxui::Box> result_row_boxes_;
    std::size_t result_slice_start_ = 0;

    // Double-click detection for the two text fields.
    Field last_clicked_field_ = Field::Query;
    std::chrono::steady_clock::time_point last_field_click_{};
};

} // namespace textlt
