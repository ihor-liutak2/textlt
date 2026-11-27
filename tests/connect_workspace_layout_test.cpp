#include <iostream>

#include "connect_workspace_layout.hpp"

using namespace textlt::connect_ui;

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

int main() {
    const auto wide = ComputeConnectWorkspaceLayout(140, 40);
    if (!Check(wide.mode == ConnectLayoutMode::ThreeColumns, "wide mode")) return 1;
    if (!Check(wide.local_width == 26, "wide local width")) return 1;
    if (!Check(wide.messenger_width == wide.local_width, "wide navigation widths match")) return 1;
    if (!Check(wide.conversation_width == 80, "wide conversation width")) return 1;
    if (!Check(wide.local_width + wide.messenger_width + wide.conversation_width + 8 == 140,
               "wide panes and gutters fill terminal")) return 1;

    const auto minimum_wide = ComputeConnectWorkspaceLayout(96, 30);
    if (!Check(minimum_wide.mode == ConnectLayoutMode::ThreeColumns, "minimum wide mode")) return 1;
    if (!Check(minimum_wide.local_width >= 18, "minimum local width")) return 1;
    if (!Check(minimum_wide.messenger_width == minimum_wide.local_width,
               "minimum navigation widths match")) return 1;
    if (!Check(minimum_wide.conversation_width >= 36, "minimum conversation width")) return 1;

    const auto compact = ComputeConnectWorkspaceLayout(80, 36);
    if (!Check(compact.mode == ConnectLayoutMode::CompactNavigationRow, "compact mode")) return 1;
    if (!Check(compact.local_width + compact.messenger_width == 80, "compact navigation widths")) return 1;
    if (!Check(compact.conversation_width == 80, "compact conversation width")) return 1;
    if (!Check(compact.navigation_height > 0, "compact navigation height")) return 1;

    const auto narrow = ComputeConnectWorkspaceLayout(32, 24);
    if (!Check(narrow.mode == ConnectLayoutMode::StackedNavigation, "narrow mode")) return 1;
    if (!Check(narrow.local_width == 32, "narrow local width")) return 1;
    if (!Check(narrow.messenger_width == 32, "narrow messenger width")) return 1;
    if (!Check(narrow.conversation_width == 32, "narrow conversation width")) return 1;
    if (!Check(narrow.local_navigation_height >= narrow.messenger_navigation_height,
               "narrow navigation heights")) return 1;

    ConnectNavigationState state;
    state.Focus(ConnectColumn::LocalConnections);
    state.Scroll(ConnectColumn::LocalConnections, 4, 20);
    state.Focus(ConnectColumn::Messengers);
    state.Scroll(ConnectColumn::Messengers, 2, 10);
    if (!Check(state.FocusedColumn() == ConnectColumn::Messengers, "messenger focus")) return 1;
    if (!Check(state.ScrollOffset(ConnectColumn::LocalConnections) == 4,
               "local scroll remains independent")) return 1;
    if (!Check(state.ScrollOffset(ConnectColumn::Messengers) == 2,
               "messenger scroll remains independent")) return 1;
    state.Scroll(ConnectColumn::LocalConnections, 100, 5);
    if (!Check(state.ScrollOffset(ConnectColumn::LocalConnections) == 5,
               "local scroll is clamped")) return 1;
    state.Focus(ConnectColumn::Conversation);
    if (!Check(state.FocusedColumn() == ConnectColumn::Conversation,
               "conversation focus")) return 1;
    return Check(state.ScrollOffset(ConnectColumn::Conversation) == 0,
                 "conversation does not borrow navigation scroll") ? 0 : 1;
}
