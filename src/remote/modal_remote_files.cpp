#include "remote/modal_remote_files.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

constexpr int kVisibleRows = 14;
constexpr int kDoubleClickMinMs = 80;
constexpr int kDoubleClickMaxMs = 500;

std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

bool IsBackspaceEvent(const ftxui::Event& event) {
    return event == ftxui::Event::Backspace ||
        event.input() == "\x7F" ||
        event.input() == "\x08";
}

bool IsPlainRemoteName(const std::string& name) {
    return !name.empty() &&
        name != "." &&
        name != ".." &&
        name.find('/') == std::string::npos &&
        name.find('\\') == std::string::npos;
}

std::string CurrentConnectionLabel(const std::vector<RemoteConnectionConfig>& connections, int index) {
    if (connections.empty()) {
        return "No connection";
    }
    index = std::clamp(index, 0, static_cast<int>(connections.size()) - 1);
    const RemoteConnectionConfig& config = connections[static_cast<size_t>(index)];
    if (!config.name.empty()) {
        return config.name;
    }
    if (!config.ssh_config_host.empty()) {
        return config.ssh_config_host;
    }
    return config.host;
}

} // namespace

RemoteFilesModalContent::RemoteFilesModalContent(
    const Theme* theme,
    RemoteConfigStore* config_store,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    OpenLocalFileCallback open_local_file,
    CopyTextCallback copy_text,
    CloseCallback on_close)
    : theme_(theme),
      config_store_(config_store),
      file_manager_(file_manager),
      start_directory_provider_(std::move(start_directory_provider)),
      open_local_file_(std::move(open_local_file)),
      copy_text_(std::move(copy_text)),
      on_close_(std::move(on_close)),
      local_provider_(file_manager) {
    local_panel_.title = "Local";
    remote_panel_.title = "Remote";

    prev_connection_button_ = MakeTextButton("Prev Conn", [this] { PreviousConnection(); });
    next_connection_button_ = MakeTextButton("Next Conn", [this] { NextConnection(); });
    refresh_button_ = MakeTextButton("Refresh", [this] { RefreshAll(); });
    copy_to_remote_button_ = MakeTextButton("Copy ->", [this] { CopyToRemote(); });
    copy_to_local_button_ = MakeTextButton("<- Copy", [this] { CopyToLocal(); });
    open_button_ = MakeTextButton("Open", [this] { OpenSelectedFile(); });
    sync_opened_button_ = MakeTextButton("Sync Last", [this] { UploadLastOpenedRemoteFile(); });
    clear_cache_button_ = MakeTextButton("Clear Cache", [this] { ClearCachedRemoteFiles(); });
    copy_path_button_ = MakeTextButton("Copy Path", [this] { CopySelectedPath(); });
    mkdir_button_ = MakeTextButton("Mkdir", [this] { StartMakeDirectory(); });
    rename_button_ = MakeTextButton("Rename", [this] { StartRename(); });
    delete_button_ = MakeTextButton("Delete", [this] { StartDelete(); });
    close_button_ = MakeTextButton("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    });

    local_path_input_ = MakePathInput(PanelSide::Local);
    remote_path_input_ = MakePathInput(PanelSide::Remote);

    local_list_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return RenderPanel(PanelSide::Local); }),
        [this](ftxui::Event event) { return HandlePanelEvent(PanelSide::Local, std::move(event)); });
    remote_list_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return RenderPanel(PanelSide::Remote); }),
        [this](ftxui::Event event) { return HandlePanelEvent(PanelSide::Remote, std::move(event)); });

    ftxui::InputOption operation_option;
    operation_option.multiline = false;
    operation_option.cursor_position = &pending_input_cursor_;
    operation_option.on_enter = [this] { ConfirmPendingOperation(); };
    operation_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg);
    };
    operation_input_ = ftxui::Input(&pending_input_value_, "name", operation_option);
    confirm_button_ = MakeTextButton("Confirm", [this] { ConfirmPendingOperation(); });
    cancel_button_ = MakeTextButton("Cancel", [this] { CancelPendingOperation(); });

    container_inner_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            prev_connection_button_,
            next_connection_button_,
            refresh_button_,
            close_button_,
        }),
        ftxui::Container::Horizontal({
            copy_to_remote_button_,
            copy_to_local_button_,
            open_button_,
            sync_opened_button_,
            clear_cache_button_,
            copy_path_button_,
            mkdir_button_,
            rename_button_,
            delete_button_,
        }),
        ftxui::Container::Horizontal({
            local_path_input_,
            remote_path_input_,
        }),
        ftxui::Container::Horizontal({
            local_list_component_,
            remote_list_component_,
        }),
        ftxui::Container::Horizontal({
            operation_input_,
            confirm_button_,
            cancel_button_,
        }),
    });
    container_ = ftxui::CatchEvent(
        container_inner_,
        [this](ftxui::Event event) { return HandleGlobalContentEvent(std::move(event)); });
}

ftxui::Component RemoteFilesModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };
    return ftxui::Button(option);
}

ftxui::Component RemoteFilesModalContent::MakePathInput(PanelSide side) {
    PanelState& panel = Panel(side);
    ftxui::InputOption option;
    option.multiline = false;
    option.cursor_position = &panel.path_cursor;
    option.on_enter = [this, side] {
        if (side == PanelSide::Local) {
            LoadLocalPathFromInput();
        } else {
            LoadRemotePathFromInput();
        }
    };
    option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg);
    };
    return ftxui::Input(&panel.path_input, side == PanelSide::Local ? "local path" : "remote path", option);
}

void RemoteFilesModalContent::Open() {
    CancelPendingOperation();
    ReloadConnections();

    std::filesystem::path start_path;
    if (start_directory_provider_) {
        start_path = start_directory_provider_();
    }
    if (start_path.empty()) {
        start_path = FileManager::CurrentProcessDirectory();
    }

    RemoteConnectionConfig local_config;
    local_config.remote_root = FileManager::PathToUtf8(start_path);
    std::string error;
    if (!local_provider_.Connect(local_config, error)) {
        SetPanelStatus(PanelSide::Local, error, true);
    }
    LoadPanel(PanelSide::Local, FileManager::PathToUtf8(start_path));

    if (EnsureRemoteProvider()) {
        const RemoteConnectionConfig& config = connections_[static_cast<size_t>(selected_connection_)];
        LoadPanel(PanelSide::Remote, config.remote_root.empty() ? "/" : config.remote_root);
    }
    active_panel_ = PanelSide::Local;
    if (local_list_component_) {
        local_list_component_->TakeFocus();
    }
    SetStatus("Use Enter/double-click to open folders. F5 copies active item to the opposite panel.");
}

void RemoteFilesModalContent::Close() {
    CancelPendingOperation();
    local_panel_.entries.clear();
    remote_panel_.entries.clear();
    remote_provider_.reset();
    SetStatus("Ready.");
}

ftxui::Element RemoteFilesModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string connection_label = CurrentConnectionLabel(connections_, selected_connection_);

    return vbox({
        hbox({
            text(" Connection: ") | color(theme.modal_accent),
            text(TrimForDisplay(connection_label, 28)) | bold | color(theme.modal_text_color),
            filler(),
            prev_connection_button_->Render(), text(" "),
            next_connection_button_->Render(), text(" "),
            refresh_button_->Render(), text(" "),
            close_button_->Render(),
        }),
        hbox({
            copy_to_remote_button_->Render(), text(" "),
            copy_to_local_button_->Render(), text(" "),
            open_button_->Render(), text(" "),
            sync_opened_button_->Render(), text(" "),
            clear_cache_button_->Render(), text(" "),
            copy_path_button_->Render(), text(" "),
            mkdir_button_->Render(), text(" "),
            rename_button_->Render(), text(" "),
            delete_button_->Render(),
        }),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                hbox({text(" Local path ") | color(theme.modal_accent), local_path_input_->Render() | flex}),
            }) | size(WIDTH, EQUAL, 58),
            separator(),
            vbox({
                hbox({text(" Remote path ") | color(theme.modal_accent), remote_path_input_->Render() | flex}),
            }) | size(WIDTH, EQUAL, 58),
        }),
        hbox({
            local_list_component_->Render() | size(WIDTH, EQUAL, 58) | border,
            remote_list_component_->Render() | size(WIDTH, EQUAL, 58) | border,
        }),
        RenderOperationRow(),
        hbox({
            text(" " + LastCachedLabel()) | dim | color(theme.modal_text_color),
            filler(),
        }),
        hbox({
            text(" " + status_) |
                (status_is_error_ ? color(ftxui::Color::Red) : color(theme.modal_text_color)),
            filler(),
            text(" Active: " + std::string(active_panel_ == PanelSide::Local ? "Local" : "Remote") + " ") |
                dim | color(theme.modal_text_color),
        }),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

ftxui::Element RemoteFilesModalContent::RenderPanel(PanelSide side) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const PanelState& panel = Panel(side);
    const bool active = side == active_panel_;

    Elements rows;
    rows.push_back(hbox({
        text(" " + panel.title + " ") | bold |
            (active ? bgcolor(theme.modal_selected_item_bg) : bgcolor(theme.modal_background)) |
            (active ? color(theme.modal_selected_item_fg) : color(theme.modal_accent)),
        filler(),
        text(panel.status.empty() ? " " : TrimForDisplay(panel.status, 36)) |
            dim |
            (panel.status_is_error ? color(ftxui::Color::Red) : color(theme.modal_text_color)),
    }));
    rows.push_back(separator() | color(theme.modal_border));

    if (panel.entries.empty()) {
        rows.push_back(filler());
        rows.push_back(text("No entries") | center | color(theme.modal_text_color));
        rows.push_back(filler());
        return vbox(std::move(rows)) |
            size(HEIGHT, EQUAL, kVisibleRows + 3) |
            bgcolor(theme.modal_input_bg);
    }

    const int start = std::max(0, panel.scroll_offset);
    const int end = std::min(static_cast<int>(panel.entries.size()), start + kVisibleRows);
    for (int index = start; index < end; ++index) {
        rows.push_back(RenderEntryRow(side, static_cast<size_t>(index)));
    }
    while (static_cast<int>(rows.size()) < kVisibleRows + 2) {
        rows.push_back(text(" "));
    }
    return vbox(std::move(rows)) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

ftxui::Element RemoteFilesModalContent::RenderEntryRow(PanelSide side, size_t index) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    PanelState& panel = Panel(side);
    const RemoteEntry& entry = panel.entries[index];
    const bool selected = static_cast<int>(index) == panel.selected;
    const std::string prefix = entry.type == RemoteEntryType::Directory ? "[D] " : "    ";

    Element row = hbox({
        text(" " + prefix + TrimForDisplay(entry.name, 36)) | bold,
        filler(),
        text(EntryTypeLabel(entry.type)) | dim | size(WIDTH, EQUAL, 6),
        text(EntrySizeLabel(entry)) | dim | size(WIDTH, EQUAL, 10),
    }) | reflect(panel.boxes[index]);

    if (selected) {
        row = row |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg);
    } else {
        row = row | color(theme.modal_text_color);
    }
    return row;
}

ftxui::Element RemoteFilesModalContent::RenderOperationRow() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (pending_operation_ == PendingOperation::None) {
        return hbox({
            text(" Enter opens. Backspace goes up. F5 copies file/folder. Existing targets require OVERWRITE confirmation. ") |
                dim | color(theme.modal_text_color),
            filler(),
        });
    }

    return hbox({
        text(" " + pending_input_label_ + " ") | color(theme.modal_accent),
        operation_input_->Render() | size(WIDTH, EQUAL, 42),
        text(" "),
        confirm_button_->Render(),
        text(" "),
        cancel_button_->Render(),
    });
}

bool RemoteFilesModalContent::HandlePanelEvent(PanelSide side, ftxui::Event event) {
    SelectPanel(side);
    PanelState& panel = Panel(side);

    if (event == ftxui::Event::ArrowDown) {
        SelectEntry(side, panel.selected + 1);
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        SelectEntry(side, panel.selected - 1);
        return true;
    }
    if (event == ftxui::Event::Return) {
        OpenSelected(side);
        return true;
    }
    if (IsBackspaceEvent(event)) {
        GoParent(side);
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
        const int index = EntryIndexAtMouse(panel, event.mouse());
        if (index >= 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - panel.last_click_time).count();
            SelectEntry(side, index);
            if (duration >= kDoubleClickMinMs && duration <= kDoubleClickMaxMs &&
                panel.last_clicked_entry == index) {
                panel.last_clicked_entry = -1;
                panel.last_click_time = {};
                OpenSelected(side);
                return true;
            }
            panel.last_clicked_entry = index;
            panel.last_click_time = now;
            return true;
        }
    }
    return false;
}

bool RemoteFilesModalContent::HandleGlobalContentEvent(ftxui::Event event) {
    if (event == ftxui::Event::F5) {
        CopyActiveToOtherPanel();
        return true;
    }
    if (event == ftxui::Event::F6) {
        StartRename();
        return true;
    }
    if (event == ftxui::Event::F7) {
        StartMakeDirectory();
        return true;
    }
    if (event == ftxui::Event::Delete || event == ftxui::Event::F8) {
        StartDelete();
        return true;
    }
    if (event == ftxui::Event::Escape && pending_operation_ != PendingOperation::None) {
        CancelPendingOperation();
        return true;
    }
    return false;
}

int RemoteFilesModalContent::EntryIndexAtMouse(
    const PanelState& panel,
    const ftxui::Mouse& mouse) const {
    for (size_t index = 0; index < panel.boxes.size(); ++index) {
        if (panel.boxes[index].Contain(mouse.x, mouse.y)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void RemoteFilesModalContent::ReloadConnections() {
    std::string error;
    if (config_store_ && !config_store_->Load(error)) {
        SetStatus(error, true);
    }
    connections_ = config_store_ ? config_store_->Connections() : std::vector<RemoteConnectionConfig>{};
    if (connections_.empty()) {
        selected_connection_ = 0;
        remote_provider_.reset();
        remote_panel_.entries.clear();
        remote_panel_.path = "/";
        remote_panel_.path_input = "/";
        SetPanelStatus(PanelSide::Remote, "Create a connection first.", true);
        return;
    }
    selected_connection_ = std::clamp(selected_connection_, 0, static_cast<int>(connections_.size()) - 1);
}

bool RemoteFilesModalContent::EnsureRemoteProvider() {
    if (connections_.empty()) {
        remote_provider_.reset();
        SetPanelStatus(PanelSide::Remote, "No remote connection configured.", true);
        return false;
    }

    const RemoteConnectionConfig& config = connections_[static_cast<size_t>(selected_connection_)];
    std::unique_ptr<IRemoteProvider> provider;
    switch (config.type) {
        case RemoteConnectionType::Sftp:
            provider = std::make_unique<RemoteSftpProvider>();
            break;
        case RemoteConnectionType::Dropbox:
            provider = std::make_unique<RemoteDropboxProvider>();
            break;
        case RemoteConnectionType::GoogleDrive:
            provider = std::make_unique<RemoteGoogleDriveProvider>();
            break;
        case RemoteConnectionType::MicrosoftDrive:
            provider = std::make_unique<RemoteMicrosoftDriveProvider>();
            break;
    }

    std::string error;
    if (!provider || !provider->Connect(config, error)) {
        remote_provider_.reset();
        SetPanelStatus(PanelSide::Remote, error.empty() ? "Cannot connect remote provider." : error, true);
        return false;
    }
    remote_provider_ = std::move(provider);
    SetPanelStatus(PanelSide::Remote, "Connected: " + CurrentConnectionLabel(connections_, selected_connection_));
    return true;
}

void RemoteFilesModalContent::LoadPanel(PanelSide side, const std::string& path) {
    PanelState& panel = Panel(side);
    IRemoteProvider* provider = Provider(side);
    if (!provider) {
        SetPanelStatus(side, "Provider is not available.", true);
        return;
    }

    std::vector<RemoteEntry> listed;
    std::string error;
    if (!provider->List(path, listed, error)) {
        SetPanelStatus(side, error.empty() ? "Cannot list directory." : error, true);
        return;
    }

    panel.path = side == PanelSide::Local ? path : NormalizeRemoteDirectory(path);
    panel.path_input = panel.path;
    panel.path_cursor = static_cast<int>(panel.path_input.size());
    panel.entries.clear();
    panel.boxes.clear();

    const std::string parent = side == PanelSide::Local
        ? FileManager::PathToUtf8(FileManager::PathFromUtf8(panel.path).parent_path())
        : RemoteParentPath(panel.path);
    if (!parent.empty() && parent != panel.path) {
        RemoteEntry parent_entry;
        parent_entry.name = "..";
        parent_entry.path = parent;
        parent_entry.type = RemoteEntryType::Directory;
        panel.entries.push_back(parent_entry);
    }

    panel.entries.insert(panel.entries.end(), listed.begin(), listed.end());
    panel.boxes.resize(panel.entries.size());
    panel.selected = panel.entries.empty()
        ? 0
        : std::clamp(panel.selected, 0, static_cast<int>(panel.entries.size()) - 1);
    panel.scroll_offset = 0;
    SetPanelStatus(side, panel.entries.empty() ? "Directory is empty." : "Ready.");
}

void RemoteFilesModalContent::LoadLocalPathFromInput() {
    LoadPanel(PanelSide::Local, local_panel_.path_input);
}

void RemoteFilesModalContent::LoadRemotePathFromInput() {
    if (EnsureRemoteProvider()) {
        LoadPanel(PanelSide::Remote, remote_panel_.path_input);
    }
}

void RemoteFilesModalContent::RefreshAll() {
    LoadPanel(PanelSide::Local, local_panel_.path);
    if (EnsureRemoteProvider()) {
        LoadPanel(PanelSide::Remote, remote_panel_.path);
    }
    SetStatus("Refreshed panels.");
}

void RemoteFilesModalContent::RefreshActive() {
    LoadPanel(active_panel_, Panel(active_panel_).path);
}

void RemoteFilesModalContent::SelectPanel(PanelSide side) {
    active_panel_ = side;
}

void RemoteFilesModalContent::SelectEntry(PanelSide side, int index) {
    PanelState& panel = Panel(side);
    if (panel.entries.empty()) {
        panel.selected = 0;
        return;
    }
    panel.selected = std::clamp(index, 0, static_cast<int>(panel.entries.size()) - 1);
    if (panel.selected < panel.scroll_offset) {
        panel.scroll_offset = panel.selected;
    } else if (panel.selected >= panel.scroll_offset + kVisibleRows) {
        panel.scroll_offset = panel.selected - kVisibleRows + 1;
    }
}

void RemoteFilesModalContent::OpenSelected(PanelSide side) {
    const RemoteEntry* entry = SelectedEntry(side);
    if (!entry) {
        SetStatus("No entry selected.", true);
        return;
    }
    if (entry->type == RemoteEntryType::Directory) {
        LoadPanel(side, entry->path);
        return;
    }
    if (side == PanelSide::Local) {
        OpenSelectedFile();
        return;
    }
    OpenSelectedFile();
}

void RemoteFilesModalContent::GoParent(PanelSide side) {
    PanelState& panel = Panel(side);
    const std::string parent = side == PanelSide::Local
        ? FileManager::PathToUtf8(FileManager::PathFromUtf8(panel.path).parent_path())
        : RemoteParentPath(panel.path);
    if (!parent.empty() && parent != panel.path) {
        LoadPanel(side, parent);
    }
}

void RemoteFilesModalContent::CopyToRemote() {
    const RemoteEntry* entry = SelectedEntry(PanelSide::Local);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select a local file or folder to upload.", true);
        return;
    }
    if (!EnsureRemoteProvider()) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    const std::string remote_target = JoinRemotePath(remote_panel_.path, entry->name);
    if (PanelContainsName(PanelSide::Remote, entry->name)) {
        StartOverwriteConfirmation(PendingOperation::CopyToRemoteOverwrite, *entry, remote_target);
        return;
    }
    CopyToRemoteConfirmed(*entry, remote_target);
}

void RemoteFilesModalContent::CopyToRemoteConfirmed(
    const RemoteEntry& entry,
    const std::string& remote_target) {
    if (!EnsureRemoteProvider()) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    std::string error;
    bool success = false;
    if (entry.type == RemoteEntryType::Directory) {
        success = remote_provider_->UploadDirectory(entry.path, remote_target, error);
    } else if (entry.type == RemoteEntryType::File || entry.type == RemoteEntryType::Symlink) {
        success = remote_provider_->Upload(entry.path, remote_target, error);
    } else {
        SetStatus("Only files, symlinks, and directories can be uploaded.", true);
        return;
    }

    if (!success) {
        SetStatus(error.empty() ? "Upload failed." : error, true);
        return;
    }
    LoadPanel(PanelSide::Remote, remote_panel_.path);
    SetStatus((entry.type == RemoteEntryType::Directory ? "Uploaded folder: " : "Uploaded: ") + entry.name);
}

void RemoteFilesModalContent::CopyToLocal() {
    const RemoteEntry* entry = SelectedEntry(PanelSide::Remote);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select a remote file or folder to download.", true);
        return;
    }
    if (!remote_provider_) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    const std::string local_target = FileManager::PathToUtf8(
        FileManager::PathFromUtf8(local_panel_.path) / FileManager::PathFromUtf8(entry->name));
    std::error_code exists_error;
    const bool target_exists = std::filesystem::exists(FileManager::PathFromUtf8(local_target), exists_error);
    if (exists_error) {
        SetStatus("Cannot check local target: " + exists_error.message(), true);
        return;
    }
    if (target_exists) {
        StartOverwriteConfirmation(PendingOperation::CopyToLocalOverwrite, *entry, local_target);
        return;
    }
    CopyToLocalConfirmed(*entry, local_target);
}

void RemoteFilesModalContent::CopyToLocalConfirmed(
    const RemoteEntry& entry,
    const std::string& local_target) {
    if (!remote_provider_) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    std::string error;
    bool success = false;
    if (entry.type == RemoteEntryType::Directory) {
        success = remote_provider_->DownloadDirectory(entry.path, local_target, error);
    } else if (entry.type == RemoteEntryType::File || entry.type == RemoteEntryType::Symlink) {
        success = remote_provider_->Download(entry.path, local_target, error);
    } else {
        SetStatus("Only files, symlinks, and directories can be downloaded.", true);
        return;
    }

    if (!success) {
        SetStatus(error.empty() ? "Download failed." : error, true);
        return;
    }
    LoadPanel(PanelSide::Local, local_panel_.path);
    SetStatus((entry.type == RemoteEntryType::Directory ? "Downloaded folder: " : "Downloaded: ") + entry.name);
}

void RemoteFilesModalContent::StartOverwriteConfirmation(
    PendingOperation operation,
    const RemoteEntry& entry,
    const std::string& target_path) {
    pending_operation_ = operation;
    pending_panel_ = operation == PendingOperation::CopyToRemoteOverwrite
        ? PanelSide::Remote
        : PanelSide::Local;
    pending_copy_entry_ = entry;
    pending_copy_target_path_ = target_path;
    pending_input_label_ = "Type OVERWRITE";
    pending_input_value_.clear();
    pending_input_cursor_ = 0;
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Target already exists: " + TrimForDisplay(target_path, 80) + ". Type OVERWRITE and Confirm to replace it.", true);
}

void RemoteFilesModalContent::CopyActiveToOtherPanel() {
    if (active_panel_ == PanelSide::Local) {
        CopyToRemote();
        return;
    }
    CopyToLocal();
}

void RemoteFilesModalContent::OpenSelectedFile() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select a file to open.", true);
        return;
    }
    if (entry->type == RemoteEntryType::Directory) {
        OpenSelected(active_panel_);
        return;
    }

    std::filesystem::path local_path;
    const RemoteEntry* remote_entry_to_cache = nullptr;
    RemoteConnectionConfig remote_connection_to_cache;

    if (active_panel_ == PanelSide::Local) {
        local_path = FileManager::PathFromUtf8(entry->path);
    } else {
        if (!remote_provider_) {
            SetStatus("Remote provider is not available.", true);
            return;
        }
        if (connections_.empty() || selected_connection_ < 0 ||
            selected_connection_ >= static_cast<int>(connections_.size())) {
            SetStatus("Remote connection is not available.", true);
            return;
        }

        local_path = TempDownloadPath(entry->name);
        std::string error;
        if (!remote_provider_->Download(entry->path, FileManager::PathToUtf8(local_path), error)) {
            SetStatus(error.empty() ? "Download failed." : error, true);
            return;
        }
        remote_entry_to_cache = entry;
        remote_connection_to_cache = connections_[static_cast<size_t>(selected_connection_)];
    }

    std::string error;
    if (!open_local_file_ || !open_local_file_(local_path, error)) {
        SetStatus(error.empty() ? "Cannot open file." : error, true);
        return;
    }

    if (remote_entry_to_cache) {
        RememberCachedRemoteFile(local_path, *remote_entry_to_cache, remote_connection_to_cache);
        SetStatus("Opened remote file in local cache. Save keeps the local cached copy; press Sync Last to upload it manually.");
        return;
    }
    SetStatus("Opened: " + local_path.filename().string());
}

bool RemoteFilesModalContent::UploadCachedLocalFile(
    const std::filesystem::path& local_path,
    std::string& status,
    std::string& error) {
    status.clear();
    error.clear();

    const std::string key = LocalPathKey(local_path);
    auto iter = std::find_if(
        cached_remote_files_.begin(),
        cached_remote_files_.end(),
        [&key](const CachedRemoteFile& cached) {
            return LocalPathKey(cached.local_path) == key;
        });

    if (iter == cached_remote_files_.end()) {
        return false;
    }

    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(iter->local_path, exists_error)) {
        error = exists_error ? exists_error.message() : "Cached local file does not exist.";
        SetStatus(error, true);
        return false;
    }

    std::unique_ptr<IRemoteProvider> provider;
    if (iter->connection.type == RemoteConnectionType::Sftp) {
        provider = std::make_unique<RemoteSftpProvider>();
    } else if (iter->connection.type == RemoteConnectionType::Dropbox) {
        provider = std::make_unique<RemoteDropboxProvider>();
    } else if (iter->connection.type == RemoteConnectionType::GoogleDrive) {
        provider = std::make_unique<RemoteGoogleDriveProvider>();
    } else if (iter->connection.type == RemoteConnectionType::MicrosoftDrive) {
        provider = std::make_unique<RemoteMicrosoftDriveProvider>();
    } else {
        error = "Manual sync is implemented for SFTP, Dropbox, Google Drive, and Microsoft cached files only.";
        SetStatus(error, true);
        return false;
    }

    if (!provider->Connect(iter->connection, error)) {
        SetStatus(error.empty() ? "Cannot connect to remote." : error, true);
        return false;
    }

    if (!provider->Upload(FileManager::PathToUtf8(iter->local_path), iter->remote_path, error)) {
        SetStatus(error.empty() ? "Remote upload failed." : error, true);
        return false;
    }

    last_cached_remote_index_ = static_cast<int>(std::distance(cached_remote_files_.begin(), iter));
    status = "manual sync uploaded remote file: " + iter->remote_name;
    SetStatus(status);

    if (remote_provider_ && RemoteParentPath(iter->remote_path) == remote_panel_.path) {
        LoadPanel(PanelSide::Remote, remote_panel_.path);
    }
    return true;
}

void RemoteFilesModalContent::UploadLastOpenedRemoteFile() {
    if (last_cached_remote_index_ < 0 ||
        last_cached_remote_index_ >= static_cast<int>(cached_remote_files_.size())) {
        SetStatus("Open a remote file first, edit/save the local cached copy, then press Sync Last.", true);
        return;
    }

    const std::filesystem::path local_path =
        cached_remote_files_[static_cast<size_t>(last_cached_remote_index_)].local_path;
    std::string status;
    std::string error;
    if (!UploadCachedLocalFile(local_path, status, error)) {
        SetStatus(error.empty() ? "Nothing to sync." : error, true);
        return;
    }
    SetStatus(status);
}


void RemoteFilesModalContent::ClearCachedRemoteFiles() {
    int removed_count = 0;
    int kept_count = 0;
    std::string last_error;

    for (const CachedRemoteFile& cached : cached_remote_files_) {
        std::error_code exists_error;
        if (!std::filesystem::exists(cached.local_path, exists_error)) {
            continue;
        }

        std::error_code remove_error;
        if (std::filesystem::remove(cached.local_path, remove_error)) {
            ++removed_count;
            continue;
        }

        ++kept_count;
        if (last_error.empty()) {
            last_error = remove_error ? remove_error.message() : "file was not removed";
        }
    }

    cached_remote_files_.clear();
    last_cached_remote_index_ = -1;

    if (kept_count > 0) {
        SetStatus(
            "Cleared cache index, but " + std::to_string(kept_count) +
                " cached file(s) were not removed: " + last_error,
            true);
        return;
    }

    SetStatus("Cleared remote cache. Removed files: " + std::to_string(removed_count) + ".");
}

void RemoteFilesModalContent::CopySelectedPath() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    const std::string text = entry ? entry->path : Panel(active_panel_).path;
    if (text.empty()) {
        SetStatus("Nothing to copy.", true);
        return;
    }
    if (copy_text_) {
        copy_text_(text);
    }
    SetStatus("Copied path.");
}

void RemoteFilesModalContent::NextConnection() {
    if (connections_.empty()) {
        SetStatus("No remote connections.", true);
        return;
    }
    selected_connection_ = (selected_connection_ + 1) % static_cast<int>(connections_.size());
    if (EnsureRemoteProvider()) {
        const RemoteConnectionConfig& config = connections_[static_cast<size_t>(selected_connection_)];
        LoadPanel(PanelSide::Remote, config.remote_root.empty() ? "/" : config.remote_root);
        SetStatus("Selected connection: " + CurrentConnectionLabel(connections_, selected_connection_));
    }
}

void RemoteFilesModalContent::PreviousConnection() {
    if (connections_.empty()) {
        SetStatus("No remote connections.", true);
        return;
    }
    selected_connection_ =
        (selected_connection_ + static_cast<int>(connections_.size()) - 1) %
        static_cast<int>(connections_.size());
    if (EnsureRemoteProvider()) {
        const RemoteConnectionConfig& config = connections_[static_cast<size_t>(selected_connection_)];
        LoadPanel(PanelSide::Remote, config.remote_root.empty() ? "/" : config.remote_root);
        SetStatus("Selected connection: " + CurrentConnectionLabel(connections_, selected_connection_));
    }
}

void RemoteFilesModalContent::StartRename() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select an item to rename.", true);
        return;
    }
    pending_operation_ = PendingOperation::Rename;
    pending_panel_ = active_panel_;
    pending_input_label_ = "New name";
    pending_input_value_ = entry->name;
    pending_input_cursor_ = static_cast<int>(pending_input_value_.size());
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Confirm rename.");
}

void RemoteFilesModalContent::StartMakeDirectory() {
    pending_operation_ = PendingOperation::MakeDirectory;
    pending_panel_ = active_panel_;
    pending_input_label_ = "Directory name";
    pending_input_value_ = "New Folder";
    pending_input_cursor_ = static_cast<int>(pending_input_value_.size());
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Confirm directory creation.");
}

void RemoteFilesModalContent::StartDelete() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select an item to delete.", true);
        return;
    }
    pending_operation_ = PendingOperation::Delete;
    pending_panel_ = active_panel_;
    pending_input_label_ = "Type DELETE";
    pending_input_value_.clear();
    pending_input_cursor_ = 0;
    pending_copy_target_path_ = entry->path;
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Delete " + TrimForDisplay(entry->path, 80) + "? Type DELETE and press Confirm.", true);
}

void RemoteFilesModalContent::ConfirmPendingOperation() {
    if (pending_operation_ == PendingOperation::None) {
        return;
    }

    IRemoteProvider* provider = Provider(pending_panel_);
    if (!provider) {
        SetStatus("Provider is not available.", true);
        return;
    }

    if (pending_operation_ == PendingOperation::CopyToRemoteOverwrite ||
        pending_operation_ == PendingOperation::CopyToLocalOverwrite) {
        if (pending_input_value_ != "OVERWRITE") {
            SetStatus("Type OVERWRITE to replace the existing target.", true);
            return;
        }

        const PendingOperation operation = pending_operation_;
        const RemoteEntry entry = pending_copy_entry_;
        const std::string target_path = pending_copy_target_path_;
        CancelPendingOperation();

        if (operation == PendingOperation::CopyToRemoteOverwrite) {
            CopyToRemoteConfirmed(entry, target_path);
            return;
        }
        CopyToLocalConfirmed(entry, target_path);
        return;
    }

    std::string error;
    if (pending_operation_ == PendingOperation::MakeDirectory) {
        if (!IsPlainRemoteName(pending_input_value_)) {
            SetStatus("Directory name must be plain name.", true);
            return;
        }
        const std::string target = pending_panel_ == PanelSide::Local
            ? FileManager::PathToUtf8(
                FileManager::PathFromUtf8(Panel(pending_panel_).path) /
                FileManager::PathFromUtf8(pending_input_value_))
            : JoinRemotePath(Panel(pending_panel_).path, pending_input_value_);
        if (!provider->MakeDirectory(target, error)) {
            SetStatus(error.empty() ? "Mkdir failed." : error, true);
            return;
        }
        LoadPanel(pending_panel_, Panel(pending_panel_).path);
        CancelPendingOperation();
        SetStatus("Created directory.");
        return;
    }

    RemoteEntry* entry = SelectedEntry(pending_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Selected item changed.", true);
        return;
    }

    if (pending_operation_ == PendingOperation::Rename) {
        if (!IsPlainRemoteName(pending_input_value_)) {
            SetStatus("New name must be plain name.", true);
            return;
        }
        const std::string target = pending_panel_ == PanelSide::Local
            ? FileManager::PathToUtf8(
                FileManager::PathFromUtf8(entry->path).parent_path() /
                FileManager::PathFromUtf8(pending_input_value_))
            : JoinRemotePath(RemoteParentPath(entry->path), pending_input_value_);
        if (!provider->Rename(entry->path, target, error)) {
            SetStatus(error.empty() ? "Rename failed." : error, true);
            return;
        }
        LoadPanel(pending_panel_, Panel(pending_panel_).path);
        CancelPendingOperation();
        SetStatus("Renamed item.");
        return;
    }

    if (pending_operation_ == PendingOperation::Delete) {
        if (pending_input_value_ != "DELETE") {
            SetStatus("Type DELETE to confirm deletion.", true);
            return;
        }
        const bool success = entry->type == RemoteEntryType::Directory
            ? provider->RemoveDirectory(entry->path, error)
            : provider->RemoveFile(entry->path, error);
        if (!success) {
            SetStatus(error.empty() ? "Delete failed." : error, true);
            return;
        }
        LoadPanel(pending_panel_, Panel(pending_panel_).path);
        CancelPendingOperation();
        SetStatus("Deleted item.");
    }
}

void RemoteFilesModalContent::CancelPendingOperation() {
    pending_operation_ = PendingOperation::None;
    pending_input_label_.clear();
    pending_input_value_.clear();
    pending_input_cursor_ = 0;
    pending_copy_entry_ = RemoteEntry{};
    pending_copy_target_path_.clear();
}

RemoteEntry* RemoteFilesModalContent::SelectedEntry(PanelSide side) {
    PanelState& panel = Panel(side);
    if (panel.selected < 0 || panel.selected >= static_cast<int>(panel.entries.size())) {
        return nullptr;
    }
    return &panel.entries[static_cast<size_t>(panel.selected)];
}

const RemoteEntry* RemoteFilesModalContent::SelectedEntry(PanelSide side) const {
    const PanelState& panel = Panel(side);
    if (panel.selected < 0 || panel.selected >= static_cast<int>(panel.entries.size())) {
        return nullptr;
    }
    return &panel.entries[static_cast<size_t>(panel.selected)];
}

RemoteFilesModalContent::PanelState& RemoteFilesModalContent::Panel(PanelSide side) {
    return side == PanelSide::Local ? local_panel_ : remote_panel_;
}

const RemoteFilesModalContent::PanelState& RemoteFilesModalContent::Panel(PanelSide side) const {
    return side == PanelSide::Local ? local_panel_ : remote_panel_;
}

IRemoteProvider* RemoteFilesModalContent::Provider(PanelSide side) {
    return side == PanelSide::Local ? static_cast<IRemoteProvider*>(&local_provider_)
                                    : static_cast<IRemoteProvider*>(remote_provider_.get());
}

const IRemoteProvider* RemoteFilesModalContent::Provider(PanelSide side) const {
    return side == PanelSide::Local ? static_cast<const IRemoteProvider*>(&local_provider_)
                                    : static_cast<const IRemoteProvider*>(remote_provider_.get());
}

std::string RemoteFilesModalContent::EntryTypeLabel(RemoteEntryType type) {
    switch (type) {
        case RemoteEntryType::Directory:
            return "DIR";
        case RemoteEntryType::File:
            return "FILE";
        case RemoteEntryType::Symlink:
            return "LINK";
        case RemoteEntryType::Other:
            return "OTHER";
    }
    return "OTHER";
}

std::string RemoteFilesModalContent::EntrySizeLabel(const RemoteEntry& entry) {
    if (entry.type == RemoteEntryType::Directory) {
        return "";
    }
    return FileManager::FormatFileSize(entry.size);
}

std::string RemoteFilesModalContent::TrimForDisplay(const std::string& value, size_t width) {
    if (value.size() <= width) {
        return value;
    }
    if (width <= 3) {
        return value.substr(0, width);
    }
    return value.substr(0, width - 3) + "...";
}

bool RemoteFilesModalContent::IsParentEntry(const RemoteEntry& entry) {
    return entry.name == "..";
}

bool RemoteFilesModalContent::PanelContainsName(PanelSide side, const std::string& name) const {
    const PanelState& panel = Panel(side);
    return std::any_of(panel.entries.begin(), panel.entries.end(), [&name](const RemoteEntry& entry) {
        return !IsParentEntry(entry) && entry.name == name;
    });
}

std::string RemoteFilesModalContent::LocalPathKey(const std::filesystem::path& path) {
    std::error_code absolute_error;
    std::filesystem::path absolute_path = std::filesystem::absolute(path, absolute_error);
    if (absolute_error) {
        absolute_path = path;
    }
    return absolute_path.lexically_normal().string();
}

std::filesystem::path RemoteFilesModalContent::TempDownloadPath(const std::string& name) const {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    std::string safe_name = name.empty() ? "remote.txt" : name;
    for (char& ch : safe_name) {
        if (ch == '/' || ch == '\\') {
            ch = '_';
        }
    }
    return std::filesystem::temp_directory_path() /
        ("textlt-remote-" + std::to_string(ticks) + "-" + safe_name);
}

void RemoteFilesModalContent::RememberCachedRemoteFile(
    const std::filesystem::path& local_path,
    const RemoteEntry& remote_entry,
    const RemoteConnectionConfig& config) {
    const std::string key = LocalPathKey(local_path);
    auto iter = std::find_if(
        cached_remote_files_.begin(),
        cached_remote_files_.end(),
        [&key](const CachedRemoteFile& cached) {
            return LocalPathKey(cached.local_path) == key;
        });

    CachedRemoteFile cached;
    cached.local_path = local_path;
    cached.remote_path = remote_entry.path;
    cached.remote_name = remote_entry.name;
    cached.connection = config;

    if (iter == cached_remote_files_.end()) {
        cached_remote_files_.push_back(std::move(cached));
        last_cached_remote_index_ = static_cast<int>(cached_remote_files_.size()) - 1;
        return;
    }

    *iter = std::move(cached);
    last_cached_remote_index_ = static_cast<int>(std::distance(cached_remote_files_.begin(), iter));
}


std::string RemoteFilesModalContent::LastCachedLabel() const {
    if (last_cached_remote_index_ < 0 ||
        last_cached_remote_index_ >= static_cast<int>(cached_remote_files_.size())) {
        return "Cached remote file: none";
    }

    const CachedRemoteFile& cached = cached_remote_files_[static_cast<size_t>(last_cached_remote_index_)];
    const std::string name = cached.remote_name.empty() ? cached.remote_path : cached.remote_name;
    return "Cached remote file: " + TrimForDisplay(name, 28) +
        "  remote: " + TrimForDisplay(cached.remote_path, 54);
}

void RemoteFilesModalContent::SetStatus(std::string status, bool is_error) {
    status_ = std::move(status);
    status_is_error_ = is_error;
}

void RemoteFilesModalContent::SetPanelStatus(PanelSide side, std::string status, bool is_error) {
    PanelState& panel = Panel(side);
    panel.status = std::move(status);
    panel.status_is_error = is_error;
}

RemoteFilesModal::RemoteFilesModal(
    const Theme* theme,
    RemoteConfigStore* config_store,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    OpenLocalFileCallback open_local_file,
    CopyTextCallback copy_text)
    : theme_(theme) {
    content_ = std::make_shared<RemoteFilesModalContent>(
        theme_,
        config_store,
        file_manager,
        std::move(start_directory_provider),
        std::move(open_local_file),
        std::move(copy_text),
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
    modal_->SetFooterText("SFTP uses external ssh/sftp. F5 copies active item. Existing targets require OVERWRITE. Delete requires DELETE.");
}

ftxui::Component RemoteFilesModal::View() const {
    return modal_;
}

void RemoteFilesModal::Open() {
    open_ = true;
    content_->Open();
    modal_->TakeFocus();
}

void RemoteFilesModal::Close() {
    open_ = false;
    content_->Close();
}

bool RemoteFilesModal::IsOpen() const {
    return open_;
}

bool RemoteFilesModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
