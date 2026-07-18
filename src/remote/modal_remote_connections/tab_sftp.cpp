ftxui::Element RemoteConnectionsModalContent::RenderSftpTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element hosts = ssh_config_hosts_.empty()
        ? text(" No concrete Host aliases found in ~/.ssh/config") |
            color(ftxui::Color::Red)
        : ssh_config_host_menu_->Render() |
            frame |
            vscroll_indicator;

    return vbox({
        RenderFieldGrid({
            {"Name", name_input_},
            {"Remote root", remote_root_input_},
        }),
        text(" SSH config hosts ") | bold | color(theme.modal_accent),
        hosts |
            size(HEIGHT, EQUAL, 15) |
            borderStyled(LIGHT, theme.modal_border) |
            bgcolor(theme.modal_input_bg),
        filler(),
        RenderActionMessage(),
    }) |
        size(HEIGHT, EQUAL, 25) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background);
}
