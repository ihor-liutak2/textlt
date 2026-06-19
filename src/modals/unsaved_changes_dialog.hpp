#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class UnsavedChangesContent : public IModalContent {
public:
    UnsavedChangesContent(const Theme* theme, std::string* display_name);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "Unsaved Changes"; }
    ModalSizePreference GetModalSizePreference() const override { return {64, 9}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    const Theme* theme_ = nullptr;
    std::string* display_name_ = nullptr;
    ftxui::Component renderer_;
};

class UnsavedChangesDialog {
public:
    using Action = std::function<void()>;

    UnsavedChangesDialog(const Theme* theme,
                         Action on_save,
                         Action on_discard,
                         Action on_cancel);

    ftxui::Component View() const;
    void Open(std::string display_name);
    void Close();
    bool IsOpen() const;
    void SetTheme(const Theme* theme);
    void TakeFocus();

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::string display_name_;
    std::shared_ptr<UnsavedChangesContent> content_impl_;
    std::shared_ptr<ModalWindow> modal_window_;
};

} // namespace textlt
