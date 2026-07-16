ftxui::Element RemoteConnectionsModalContent::RenderSshTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        RenderFieldGrid({
            {"Name", name_input_},
            {"Host", host_input_},
            {"Port", port_input_},
            {"Username", user_input_},
            {"Password", password_input_},
            {"Remote root", remote_root_input_},
        }),
        filler(),
    }) |
        size(HEIGHT, EQUAL, 25) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background);
}
