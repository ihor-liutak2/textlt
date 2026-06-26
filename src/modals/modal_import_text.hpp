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
#include "text_importer.hpp"
#include "theme.hpp"

namespace textlt {

class ImportTextModalContent : public IModalContent {
public:
    using StartDirectoryProvider = std::function<std::filesystem::path()>;
    using ImportTextCallback = std::function<bool(
        const std::filesystem::path& path,
        const std::string& text,
        std::string& error)>;
    using CloseCallback = std::function<void()>;

    ImportTextModalContent(
        const Theme* theme,
        StartDirectoryProvider start_directory_provider,
        ImportTextCallback on_import_text,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Import Text"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {92, 28}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();

    void Refresh();
    void ImportSelected();
    bool HandleEvent(ftxui::Event event);

private:
    ftxui::Component MakeTextButton(
        std::string label,
        std::function<void()> on_click);

    void LoadDirectory(const std::filesystem::path& directory);
    void NavigateUp();
    void ActivateSelected();
    void MoveSelection(int delta);
    void ClampSelection();
    void RebuildEntryLabels();
    bool HandleEntryMouseEvent(ftxui::Event event);

    ftxui::Element RenderEntryList() const;
    ftxui::Element RenderHelpLine() const;
    std::string FormatEntryLabel(const TextImportEntry& entry) const;
    std::string FormatCurrentDirectory() const;
    std::string FormatSelectedFile() const;
    std::string TrimForDisplay(const std::string& text, size_t max_size) const;

    const Theme* theme_ = nullptr;
    StartDirectoryProvider start_directory_provider_;
    ImportTextCallback on_import_text_;
    CloseCallback on_close_;

    TextImporter importer_;
    std::filesystem::path current_directory_;
    std::vector<TextImportEntry> entries_;
    std::vector<std::string> entry_labels_;
    int selected_entry_ = 0;
    std::string status_ = "Select a .docx or .fb2 file to import.";

    std::chrono::steady_clock::time_point last_entry_click_time_{};
    int last_clicked_entry_ = -1;

    ftxui::Component entry_menu_;
    ftxui::Component entry_list_component_;
    ftxui::Component import_button_;
    ftxui::Component up_button_;
    ftxui::Component refresh_button_;
    ftxui::Component container_;
};

class ImportTextModal {
public:
    using StartDirectoryProvider = ImportTextModalContent::StartDirectoryProvider;
    using ImportTextCallback = ImportTextModalContent::ImportTextCallback;

    ImportTextModal(
        const Theme* theme,
        StartDirectoryProvider start_directory_provider,
        ImportTextCallback on_import_text);

    ftxui::Component View() const;

    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;

    std::shared_ptr<ImportTextModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
