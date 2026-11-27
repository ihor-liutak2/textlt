#pragma once

#include <algorithm>

namespace textlt::connect_ui {

enum class ConnectLayoutMode {
    ThreeColumns,
    CompactNavigationRow,
    StackedNavigation,
};

enum class ConnectColumn {
    LocalConnections,
    Messengers,
    Conversation,
};

struct ConnectWorkspaceLayout {
    ConnectLayoutMode mode = ConnectLayoutMode::ThreeColumns;
    int local_width = 1;
    int messenger_width = 1;
    int conversation_width = 1;
    int navigation_height = 1;
    int local_navigation_height = 1;
    int messenger_navigation_height = 1;
};

inline ConnectWorkspaceLayout ComputeConnectWorkspaceLayout(int columns, int rows) {
    const int width = std::max(1, columns);
    const int height = std::max(1, rows);
    ConnectWorkspaceLayout layout;

    if (width >= 96) {
        layout.mode = ConnectLayoutMode::ThreeColumns;
        // Keep two-column gutters at the outside edges and between each pane.
        const int pane_width = std::max(1, width - 8);
        layout.local_width = std::max(18, pane_width / 5);
        layout.messenger_width = layout.local_width;
        layout.conversation_width =
            std::max(1, pane_width - layout.local_width - layout.messenger_width);
        return layout;
    }

    if (width >= 40) {
        layout.mode = ConnectLayoutMode::CompactNavigationRow;
        layout.local_width = std::max(1, width / 2);
        layout.messenger_width = std::max(1, width - layout.local_width);
        layout.conversation_width = width;
        layout.navigation_height = std::clamp(height / 3, 6, 12);
        return layout;
    }

    layout.mode = ConnectLayoutMode::StackedNavigation;
    layout.local_width = width;
    layout.messenger_width = width;
    layout.conversation_width = width;
    layout.local_navigation_height = std::clamp(height / 4, 7, 10);
    layout.messenger_navigation_height = std::clamp(height / 5, 6, 8);
    return layout;
}

class ConnectNavigationState {
public:
    void Focus(ConnectColumn column) { focused_column_ = column; }
    ConnectColumn FocusedColumn() const { return focused_column_; }

    int ScrollOffset(ConnectColumn column) const {
        if (column == ConnectColumn::LocalConnections) return local_scroll_;
        if (column == ConnectColumn::Messengers) return messenger_scroll_;
        return 0;
    }

    void Scroll(ConnectColumn column, int delta, int maximum) {
        int* offset = nullptr;
        if (column == ConnectColumn::LocalConnections) offset = &local_scroll_;
        if (column == ConnectColumn::Messengers) offset = &messenger_scroll_;
        if (offset) *offset = std::clamp(*offset + delta, 0, std::max(0, maximum));
    }

private:
    ConnectColumn focused_column_ = ConnectColumn::Conversation;
    int local_scroll_ = 0;
    int messenger_scroll_ = 0;
};

} // namespace textlt::connect_ui
