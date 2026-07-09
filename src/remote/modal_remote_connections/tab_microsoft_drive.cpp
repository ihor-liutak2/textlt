ftxui::Element RemoteConnectionsModalContent::RenderMicrosoftDriveTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        RenderFieldGrid({
            {"Name", name_input_},
            {"Tenant ID", tenant_id_input_},
            {"Client ID", client_id_input_},
            {"Client secret", client_secret_input_},
            {"Site ID", site_id_input_},
            {"Drive ID", drive_id_input_},
            {"Remote root", remote_root_input_},
            {"Scope", scope_input_},
            {"Access token", access_token_input_},
            {"Refresh token", refresh_token_input_},
        }),
        filler(),
    }) |
        size(HEIGHT, EQUAL, 25) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background);
}
