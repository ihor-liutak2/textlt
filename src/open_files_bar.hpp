#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "context_menu_component.hpp"
#include "ftxui/component/component_base.hpp"

namespace textlt {

class DocumentWorkspace;
struct Theme;

class OpenFilesBar : public ftxui::ComponentBase {
public:
    static constexpr int kMinTabWidth = 20;
    static constexpr int kMaxTabWidth = 35;
    static constexpr int kTabSpacing = 3;
    static constexpr int kTabRows = 2;
    static constexpr int kBottomSpacing = 1;

    using SessionAction = std::function<void(size_t)>;

    OpenFilesBar(const DocumentWorkspace& workspace, const Theme& theme,
        SessionAction activate, SessionAction close);

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    ftxui::Element RenderFileList();
    void OpenFileList();
    void CloseFileList();
    void ClearHitboxes();
    bool IsFileListOpen() const;

private:
    class TabRows;

    struct TabHit {
        ftxui::Box box;
        std::optional<size_t> session_index;
        bool has_close;
    };

    void OpenFileList(const std::vector<size_t>& indices, int x, int y);

    const DocumentWorkspace& workspace_;
    const Theme& theme_;
    SessionAction activate_;
    SessionAction close_;
    ContextMenuComponent file_list_;
    ftxui::Box box_{1, 0, 1, 0};
    std::vector<TabHit> tabs_;
    std::vector<size_t> hidden_;
};

} // namespace textlt
