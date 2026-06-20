#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

enum class PathOperationMode {
    None,
    Rename,
    Move,
};

class PathOperationContent : public IModalContent {
public:
    using ConfirmAction = std::function<void()>;

    PathOperationContent(const Theme* theme,
                         std::string* from,
                         std::string* to,
                         std::string* error,
                         ConfirmAction on_confirm);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return component_; }
    std::string GetTitle() override { return title_; }
    ModalSizePreference GetModalSizePreference() const override { return {76, 12}; }

    void Configure(std::string title,
                   std::vector<std::string> candidates,
                   size_t from_cursor_position,
                   size_t to_cursor_position);
    void SetTheme(const Theme* theme) { theme_ = theme; }
    void TakeFocus();

private:
    void Rebuild();
    void UpdateMatches();
    void AcceptSelectedCandidate();
    bool HandleEvent(ftxui::Event event);

    const Theme* theme_ = nullptr;
    std::string* from_ = nullptr;
    std::string* to_ = nullptr;
    std::string* error_ = nullptr;
    std::string title_ = "Path";
    std::vector<std::string> candidates_;
    std::vector<std::string> matches_;
    std::vector<ftxui::Box> match_boxes_;
    int selected_match_ = 0;
    int focused_field_ = 0;
    bool to_touched_ = false;
    ConfirmAction on_confirm_;
    ftxui::Component from_input_;
    ftxui::Component to_input_;
    ftxui::Component container_;
    ftxui::Component component_;
};

class PathOperationDialog {
public:
    using ConfirmCallback =
        std::function<bool(PathOperationMode mode,
                           const std::string& from,
                           const std::string& to,
                           std::string& error)>;

    PathOperationDialog(const Theme* theme, ConfirmCallback on_confirm);

    ftxui::Component View() const;
    void Open(PathOperationMode mode,
              std::string initial_from,
              std::vector<std::string> candidates);
    void Close();
    bool IsOpen() const;
    void TakeFocus();

private:
    void Confirm();
    void RebuildModal();

    ConfirmCallback on_confirm_;
    const Theme* theme_ = nullptr;
    PathOperationMode mode_ = PathOperationMode::None;
    std::string from_;
    std::string to_;
    std::string error_;
    std::shared_ptr<PathOperationContent> content_impl_;
    std::shared_ptr<ModalWindow> modal_window_;
};

} // namespace textlt
