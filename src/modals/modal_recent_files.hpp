#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "editor/document_file_controller.hpp"
#include "theme.hpp"

namespace textlt {

class RecentFilesModalContent : public IModalContent {
public:
    using OpenFileCallback =
        std::function<bool(const std::filesystem::path&, std::string&)>;
    using CloseCallback = std::function<void()>;

    RecentFilesModalContent(
        const Theme* theme,
        DocumentFileController* document_file_controller,
        OpenFileCallback on_open,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "Recent Files"; }
    ModalSizePreference GetModalSizePreference() const override { return {72, 18}; }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Refresh();
    void OpenSelected();

private:
    struct FolderGroup {
        std::filesystem::path folder_path;
        size_t first_entry = 0;
        size_t entry_count = 0;
    };

    bool OnEvent(ftxui::Event event);
    int EntryIndexAtMouse(const ftxui::Mouse& mouse) const;

    const Theme* theme_ = nullptr;
    DocumentFileController* document_file_controller_ = nullptr;
    OpenFileCallback on_open_;
    CloseCallback on_close_;
    std::vector<DocumentFileController::RecentFileEntry> entries_;
    std::vector<FolderGroup> groups_;
    std::vector<ftxui::Box> entry_boxes_;
    int selected_entry_ = 0;
    std::filesystem::path selected_full_path_;
    std::string status_ = "Enter or double-click opens selected file";
    int last_clicked_entry_ = -1;
    std::chrono::steady_clock::time_point last_click_time_;
    ftxui::Component renderer_;
};

class RecentFilesModal {
public:
    using OpenFileCallback = RecentFilesModalContent::OpenFileCallback;

    RecentFilesModal(
        const Theme* theme,
        DocumentFileController* document_file_controller,
        OpenFileCallback on_open);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<RecentFilesModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
