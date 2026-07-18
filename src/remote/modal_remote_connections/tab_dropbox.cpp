ftxui::Element RemoteConnectionsModalContent::RenderDropboxTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        RenderFieldGrid({
            {"Name", name_input_},
            {"Remote root", remote_root_input_},
            {"App key", app_key_input_},
            {"App secret", app_secret_input_},
            {"Access token", access_token_input_},
            {"Refresh token", refresh_token_input_},
        }),
        filler(),
        RenderActionMessage(),
    }) |
        size(HEIGHT, EQUAL, 25) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background);
}
