ftxui::Element RemoteConnectionsModalContent::RenderSftpTab() {
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
            {"Auth mode", auth_mode_input_},
            {"Private key file", identity_file_input_},
            {"Key passphrase", key_passphrase_input_},
            {"Known hosts file", known_hosts_file_input_},
            {"SSH config host", ssh_config_host_input_},
        }),
        filler(),
    }) |
        size(HEIGHT, EQUAL, 25) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background);
}
