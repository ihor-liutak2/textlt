ftxui::Element RemoteConnectionsModalContent::RenderFtpsTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    const auto field = [&](const std::string& label, const Component& component, int width) {
        return RenderRemoteDialogInputFrame(theme, label, component) |
            size(WIDTH, EQUAL, width);
    };

    return vbox({
        hbox({
            field("Name", name_input_, 44),
            text("  "),
            field("Host", host_input_, 44),
            text("  "),
            field("Port", port_input_, 16),
        }),
        hbox({
            field("Username", user_input_, 53),
            text("  "),
            field("Password", password_input_, 53),
        }),
        hbox({
            field("TLS mode", ftps_tls_mode_input_, 53),
            text("  "),
            field("Passive", ftps_passive_checkbox_, 53),
        }),
        field("Remote root", remote_root_input_, 108),
        filler(),
        RenderActionMessage(),
    }) |
        size(HEIGHT, EQUAL, 25) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background);
}
