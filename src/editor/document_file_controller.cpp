#include "editor/document_file_controller.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include "document.hpp"
#include "json_utils.hpp"

namespace textlt {
namespace {

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string PathComparisonKey(const std::filesystem::path& path) {
#ifdef _WIN32
    return Lowercase(path.string());
#else
    return path.string();
#endif
}

bool PathsEqual(const std::filesystem::path& left, const std::filesystem::path& right) {
    return PathComparisonKey(left) == PathComparisonKey(right);
}

} // namespace

DocumentFileController::DocumentFileController(
    FileManager& file_manager,
    DocumentWorkspace& workspace)
    : file_manager_(file_manager),
      workspace_(workspace),
      state_path_(DefaultStatePath()) {
    LoadState();
}

std::shared_ptr<Document> DocumentFileController::ActiveDocument() const {
    return workspace_.ActiveDocument();
}

size_t DocumentFileController::AddDocument(std::shared_ptr<Document> document) {
    const size_t document_index = workspace_.AddDocument(std::move(document));
    workspace_.AssignDocumentToActivePane(document_index);
    return document_index;
}

size_t DocumentFileController::NewDocument() {
    PersistActiveFavoriteCursor();
    const size_t document_index = workspace_.AddUntitledDocument();
    workspace_.AssignDocumentToActivePane(document_index);
    PersistOpenedDocuments();
    return document_index;
}

void DocumentFileController::EnsureOneDocument(size_t visible_pane_count) {
    if (!workspace_.Empty()) {
        workspace_.ClampActiveDocumentIndex();
        workspace_.AssignDocumentToActivePane(workspace_.ActiveDocumentIndex());
        workspace_.SyncEditorPaneDocuments(visible_pane_count);
        return;
    }

    const size_t document_index = workspace_.AddUntitledDocument();
    workspace_.AssignDocumentToActivePane(document_index);
    workspace_.SyncEditorPaneDocuments(visible_pane_count);
}

bool DocumentFileController::RemoveDocument(size_t index, size_t visible_pane_count) {
    if (!workspace_.HasDocumentAt(index)) {
        return false;
    }

    workspace_.RemoveDocument(index);
    EnsureOneDocument(visible_pane_count);
    workspace_.SyncEditorPaneDocuments(visible_pane_count);
    return true;
}

bool DocumentFileController::ActivateDocument(size_t index, size_t visible_pane_count) {
    const auto document = workspace_.DocumentAt(index);
    if (!document) {
        return false;
    }

    PersistActiveFavoriteCursor();
    workspace_.AssignDocumentToActivePane(index);
    workspace_.SyncEditorPaneDocuments(visible_pane_count);
    RestoreFavoriteCursor(document->path);
    PersistOpenedDocuments();
    return true;
}

bool DocumentFileController::OpenDocument(const std::filesystem::path& path, std::string& error) {
    PersistActiveFavoriteCursor();

    const std::filesystem::path normalized_path = NormalizeDocumentPath(path);
    if (normalized_path.empty()) {
        error = "Invalid file path.";
        return false;
    }

    const int open_index = workspace_.FindDocumentByPath(normalized_path);
    if (open_index >= 0) {
        workspace_.AssignDocumentToActivePane(static_cast<size_t>(open_index));
        RestoreFavoriteCursor(normalized_path);
        AddRecentFile(normalized_path);
        PersistOpenedDocuments();
        return true;
    }

    auto document = file_manager_.Open(normalized_path, error);
    if (!document) {
        return false;
    }

    AddDocument(document);
    RestoreFavoriteCursor(normalized_path);
    AddRecentFile(normalized_path);
    PersistOpenedDocuments();
    return true;
}

bool DocumentFileController::OpenOrCreateDocument(
    const std::filesystem::path& path,
    std::string& error) {
    if (std::filesystem::exists(path)) {
        return OpenDocument(path, error);
    }

    PersistActiveFavoriteCursor();
    auto document = std::make_shared<Document>();
    document->Reset();
    document->SetPath(path);
    AddDocument(document);
    PersistOpenedDocuments();
    return true;
}

bool DocumentFileController::SaveActiveDocument(std::string& error) {
    const auto document = workspace_.ActiveDocument();
    if (!document) {
        error = "No active document.";
        return false;
    }
    if (document->read_only) {
        error = "Cannot save a read-only Git compare document.";
        return false;
    }
    if (DocumentWorkspace::IsMemoryOnlyDocument(document)) {
        error = "Save As required.";
        return false;
    }
    if (!file_manager_.SaveAs(document, document->path, error)) {
        return false;
    }
    AddRecentFile(document->path);
    PersistOpenedDocuments();
    return true;
}

bool DocumentFileController::SaveActiveDocumentAs(
    const std::filesystem::path& path,
    std::string& error) {
    const auto document = workspace_.ActiveDocument();
    if (!document) {
        error = "No active document.";
        return false;
    }
    if (document->read_only) {
        error = "Cannot save a read-only Git compare document.";
        return false;
    }
    if (!file_manager_.SaveAs(document, path, error)) {
        return false;
    }
    AddRecentFile(path);
    PersistOpenedDocuments();
    return true;
}

DocumentFileController::SaveAllResult DocumentFileController::SaveAllDirtyDocuments() {
    SaveAllResult result;

    for (const auto& document : workspace_.OpenDocuments()) {
        if (!document || !document->is_dirty) {
            continue;
        }
        if (document->read_only) {
            ++result.skipped_count;
            continue;
        }
        if (DocumentWorkspace::IsMemoryOnlyDocument(document)) {
            ++result.skipped_count;
            continue;
        }

        std::string error;
        if (file_manager_.SaveAs(document, document->path, error)) {
            AddRecentFile(document->path);
            ++result.saved_count;
        } else if (result.first_error.empty()) {
            result.first_error = error;
        }
    }

    PersistOpenedDocuments();
    return result;
}

bool DocumentFileController::CloseActiveDocument(
    size_t visible_pane_count,
    std::string& closed_name) {
    if (workspace_.Empty()) {
        EnsureOneDocument(visible_pane_count);
        closed_name.clear();
        return false;
    }

    const auto closed_document = workspace_.ActiveDocument();
    closed_name = DisplayName(closed_document);
    RemoveDocument(workspace_.ActiveDocumentIndex(), visible_pane_count);
    PersistOpenedDocuments();
    return true;
}

void DocumentFileController::CloseAllDocuments(size_t visible_pane_count) {
    workspace_.ClearDocuments();
    EnsureOneDocument(visible_pane_count);
    PersistOpenedDocuments();
}

void DocumentFileController::PersistOpenedDocuments() {
    opened_config_ = CurrentOpenedConfig();
    SaveState();
}

bool DocumentFileController::RestoreOpenedDocuments(size_t visible_pane_count) {
    if (opened_config_.files.empty()) {
        return false;
    }

    workspace_.ClearDocuments();
    for (const OpenedFileState& entry : opened_config_.files) {
        AddRestoredDocument(entry);
    }

    if (workspace_.Empty()) {
        return false;
    }

    workspace_.SetActiveDocumentIndex(std::min(opened_config_.active_index, workspace_.DocumentCount() - 1));
    workspace_.AssignDocumentToActivePane(workspace_.ActiveDocumentIndex());
    workspace_.SyncEditorPaneDocuments(visible_pane_count);
    PersistOpenedDocuments();
    return true;
}

const std::vector<DocumentFileController::RecentFileEntry>&
DocumentFileController::RecentFiles() const {
    return recent_files_;
}

void DocumentFileController::RefreshRecentFiles() {
    if (RemoveMissingRecentFiles()) {
        SaveState();
    }
}

bool DocumentFileController::RemoveRecentFile(const std::filesystem::path& path) {
    const std::filesystem::path normalized_path = NormalizeDocumentPath(path);
    if (normalized_path.empty()) {
        return false;
    }

    const size_t old_size = recent_files_.size();
    recent_files_.erase(
        std::remove_if(
            recent_files_.begin(),
            recent_files_.end(),
            [&normalized_path](const RecentFileEntry& entry) {
                return PathsEqual(entry.full_path, normalized_path);
            }),
        recent_files_.end());
    if (recent_files_.size() == old_size) {
        return false;
    }
    SaveState();
    return true;
}

const std::vector<DocumentFileController::FavoriteFileEntry>&
DocumentFileController::FavoriteFiles() const {
    return favorite_files_;
}

std::vector<std::filesystem::path> DocumentFileController::FavoriteFilePaths() const {
    std::vector<std::filesystem::path> paths;
    paths.reserve(favorite_files_.size());
    for (const FavoriteFileEntry& favorite : favorite_files_) {
        paths.push_back(favorite.path);
    }
    return paths;
}

bool DocumentFileController::RemoveFavorite(const std::filesystem::path& path) {
    const std::filesystem::path normalized_path = NormalizeDocumentPath(path);
    if (normalized_path.empty()) {
        return false;
    }

    const size_t old_size = favorite_files_.size();
    favorite_files_.erase(
        std::remove_if(
            favorite_files_.begin(),
            favorite_files_.end(),
            [&normalized_path](const FavoriteFileEntry& favorite) {
                return PathsEqual(favorite.path, normalized_path);
            }),
        favorite_files_.end());
    if (favorite_files_.size() == old_size) {
        return false;
    }
    SaveState();
    return true;
}

std::string DocumentFileController::ActiveDocumentFavoritePath() const {
    const auto document = workspace_.ActiveDocument();
    if (!document) {
        return {};
    }
    const std::filesystem::path normalized_path = NormalizeDocumentPath(document->CurrentFilePath());
    return normalized_path.string();
}

bool DocumentFileController::IsActiveDocumentFavorite() const {
    const std::string path = ActiveDocumentFavoritePath();
    return !path.empty() && IsFavorite(path);
}

void DocumentFileController::PersistActiveFavoriteCursor() {
    const auto document = workspace_.ActiveDocument();
    if (!document) {
        return;
    }

    const std::filesystem::path favorite_path = NormalizeDocumentPath(document->CurrentFilePath());
    if (favorite_path.empty() || !IsFavorite(favorite_path)) {
        return;
    }

    UpdateFavoriteCursor(favorite_path, document->cursor_row, document->cursor_col);
}

void DocumentFileController::RestoreFavoriteCursor(const std::filesystem::path& path) {
    const FavoriteFileEntry* favorite = FindFavorite(path);
    const auto document = workspace_.ActiveDocument();
    if (!favorite || !document) {
        return;
    }
    document->SetCursorPosition(favorite->row, favorite->column);
}

DocumentFileController::FavoriteToggleResult DocumentFileController::ToggleActiveFavorite() {
    FavoriteToggleResult result;
    result.path = ActiveDocumentFavoritePath();
    if (result.path.empty()) {
        result.status = FavoriteToggleStatus::NeedsSave;
        return result;
    }

    if (IsFavorite(result.path)) {
        RemoveFavorite(result.path);
        result.status = FavoriteToggleStatus::Removed;
        return result;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(result.path, error)) {
        result.status = FavoriteToggleStatus::NeedsSave;
        return result;
    }

    AddFavorite(result.path);
    PersistActiveFavoriteCursor();
    result.status = FavoriteToggleStatus::Added;
    return result;
}

bool DocumentFileController::LoadState() {
    recent_files_.clear();
    favorite_files_.clear();
    opened_config_ = {};

    const bool loaded_unified_state = LoadUnifiedState();
    if (!loaded_unified_state) {
        LoadLegacyState();
    }
    RemoveMissingRecentFiles();
    SaveState();
    return loaded_unified_state;
}

bool DocumentFileController::SaveState() const {
    Json root = Json::object();

    root["recent_files"] = Json::array();
    for (const RecentFileEntry& entry : recent_files_) {
        root["recent_files"].push_back({{"path", entry.full_path.string()}});
    }

    root["favorites"] = Json::array();
    for (const FavoriteFileEntry& favorite : favorite_files_) {
        root["favorites"].push_back({
            {"path", favorite.path.string()},
            {"row", favorite.row},
            {"column", favorite.column},
        });
    }

    root["active_opened_index"] = opened_config_.active_index;
    root["opened_files"] = Json::array();
    for (const OpenedFileState& entry : opened_config_.files) {
        Json file = {
            {"memory_only", entry.memory_only},
            {"path", entry.path.string()},
            {"row", entry.row},
            {"column", entry.column},
        };
        if (entry.memory_only) {
            file["content"] = entry.content;
        }
        root["opened_files"].push_back(std::move(file));
    }

    return WriteJsonAtomically(state_path_, root);
}

std::filesystem::path DocumentFileController::StatePath() const {
    return state_path_;
}

void DocumentFileController::AddRestoredDocument(const OpenedFileState& entry) {
    if (entry.memory_only) {
        auto document = std::make_shared<Document>();
        document->Reset();
        document->LoadContent(entry.content, "Untitled");
        document->is_dirty = true;
        document->SetCursorPosition(entry.row, entry.column);
        AddDocument(document);
        return;
    }

    std::error_code error_code;
    if (!std::filesystem::is_regular_file(entry.path, error_code)) {
        return;
    }

    std::string error;
    auto document = file_manager_.Open(entry.path, error);
    if (!document) {
        return;
    }
    document->SetCursorPosition(entry.row, entry.column);
    AddDocument(document);
}

void DocumentFileController::AddRecentFile(const std::filesystem::path& path) {
    const std::filesystem::path normalized_path = NormalizeDocumentPath(path);
    if (normalized_path.empty()) {
        return;
    }

    RecentFileEntry entry = MakeRecentEntry(normalized_path);
    recent_files_.erase(
        std::remove_if(
            recent_files_.begin(),
            recent_files_.end(),
            [&entry](const RecentFileEntry& existing) {
                return PathsEqual(existing.full_path, entry.full_path);
            }),
        recent_files_.end());
    recent_files_.insert(recent_files_.begin(), std::move(entry));

    if (recent_files_.size() > kMaxRecentFiles) {
        recent_files_.resize(kMaxRecentFiles);
    }
    SaveState();
}

bool DocumentFileController::AddFavorite(const std::filesystem::path& path) {
    const std::filesystem::path normalized_path = NormalizeDocumentPath(path);
    if (normalized_path.empty() || IsFavorite(normalized_path)) {
        return false;
    }

    favorite_files_.push_back({normalized_path, 0, 0});
    SaveState();
    return true;
}

bool DocumentFileController::IsFavorite(const std::filesystem::path& path) const {
    return FindFavorite(path) != nullptr;
}

const DocumentFileController::FavoriteFileEntry* DocumentFileController::FindFavorite(
    const std::filesystem::path& path) const {
    const std::filesystem::path normalized_path = NormalizeDocumentPath(path);
    if (normalized_path.empty()) {
        return nullptr;
    }

    auto iter = std::find_if(
        favorite_files_.begin(),
        favorite_files_.end(),
        [&normalized_path](const FavoriteFileEntry& favorite) {
            return PathsEqual(favorite.path, normalized_path);
        });
    return iter == favorite_files_.end() ? nullptr : &*iter;
}

bool DocumentFileController::UpdateFavoriteCursor(
    const std::filesystem::path& path,
    size_t row,
    size_t column) {
    const std::filesystem::path normalized_path = NormalizeDocumentPath(path);
    if (normalized_path.empty()) {
        return false;
    }

    auto iter = std::find_if(
        favorite_files_.begin(),
        favorite_files_.end(),
        [&normalized_path](const FavoriteFileEntry& favorite) {
            return PathsEqual(favorite.path, normalized_path);
        });
    if (iter == favorite_files_.end()) {
        return false;
    }

    iter->row = row;
    iter->column = column;
    return SaveState();
}

DocumentFileController::OpenedConfig DocumentFileController::CurrentOpenedConfig() const {
    OpenedConfig opened_config;
    bool active_index_saved = false;

    for (size_t doc_index = 0; doc_index < workspace_.OpenDocuments().size(); ++doc_index) {
        const auto& document = workspace_.OpenDocuments()[doc_index];
        if (!document || document->temporary || document->read_only) {
            continue;
        }

        OpenedFileState entry;
        entry.row = document->cursor_row;
        entry.column = document->cursor_col;

        if (DocumentWorkspace::IsMemoryOnlyDocument(document)) {
            const std::string content = document->ToContent();
            if (content.empty()) {
                continue;
            }
            entry.memory_only = true;
            entry.path = "Untitled";
            entry.content = content;
        } else {
            const std::filesystem::path normalized_path = NormalizeDocumentPath(document->path);
            if (normalized_path.empty()) {
                continue;
            }
            std::error_code error;
            if (!std::filesystem::is_regular_file(normalized_path, error)) {
                continue;
            }
            entry.memory_only = false;
            entry.path = normalized_path;
        }

        if (doc_index == workspace_.ActiveDocumentIndex()) {
            opened_config.active_index = opened_config.files.size();
            active_index_saved = true;
        }
        opened_config.files.push_back(std::move(entry));
    }

    if (!active_index_saved || opened_config.active_index >= opened_config.files.size()) {
        opened_config.active_index = opened_config.files.empty() ? 0 : opened_config.files.size() - 1;
    }
    return opened_config;
}

void DocumentFileController::SetOpenedConfig(OpenedConfig config) {
    if (config.active_index >= config.files.size()) {
        config.active_index = config.files.empty() ? 0 : config.files.size() - 1;
    }
    opened_config_ = std::move(config);
}

bool DocumentFileController::LoadUnifiedState() {
    const Json root = LoadJsonObject(state_path_);
    if (root.empty()) {
        return false;
    }

    LoadRecentFilesFromJson(state_path_);

    favorite_files_.clear();
    const auto favorites_iter = root.find("favorites");
    if (favorites_iter != root.end() && favorites_iter->is_array()) {
        for (const Json& object : *favorites_iter) {
            if (!object.is_object()) {
                continue;
            }
            const std::filesystem::path normalized_path = NormalizeDocumentPath(JsonString(object, "path"));
            if (normalized_path.empty() || IsFavorite(normalized_path)) {
                continue;
            }
            favorite_files_.push_back({
                normalized_path,
                JsonSize(object, "row", 0),
                JsonSize(object, "column", 0),
            });
        }
    }

    LoadOpenedFilesFromJson(state_path_);
    return true;
}

bool DocumentFileController::LoadLegacyState() {
    bool loaded = false;
    if (std::filesystem::exists(LegacyRecentFilesPath())) {
        LoadRecentFilesFromJson(LegacyRecentFilesPath());
        loaded = true;
    }
    if (std::filesystem::exists(LegacyOpenedFilesPath())) {
        LoadOpenedFilesFromJson(LegacyOpenedFilesPath());
        loaded = true;
    }

    const std::filesystem::path legacy_config_path = LegacyConfigPath();
    if (std::filesystem::exists(legacy_config_path)) {
        LoadFavoritesFromJson(legacy_config_path);
        loaded = true;
    } else {
        const std::filesystem::path fallback_path = LegacyFallbackConfigPath();
        if (!fallback_path.empty() && std::filesystem::exists(fallback_path)) {
            LoadFavoritesFromJson(fallback_path);
            loaded = true;
        }
    }
    return loaded;
}

void DocumentFileController::LoadRecentFilesFromJson(const std::filesystem::path& path) {
    const Json root = LoadJsonObject(path);
    const auto files = root.find("recent_files");
    if (files == root.end() || !files->is_array()) {
        return;
    }

    for (const Json& item : *files) {
        std::string path_value;
        if (item.is_string()) {
            path_value = item.get<std::string>();
        } else if (item.is_object()) {
            path_value = JsonString(item, "path");
        }
        const std::filesystem::path normalized_path = NormalizeDocumentPath(path_value);
        if (normalized_path.empty()) {
            continue;
        }
        RecentFileEntry entry = MakeRecentEntry(normalized_path);
        if (std::find_if(
                recent_files_.begin(),
                recent_files_.end(),
                [&entry](const RecentFileEntry& existing) {
                    return PathsEqual(existing.full_path, entry.full_path);
                }) == recent_files_.end()) {
            recent_files_.push_back(std::move(entry));
        }
        if (recent_files_.size() >= kMaxRecentFiles) {
            break;
        }
    }
}

void DocumentFileController::LoadOpenedFilesFromJson(const std::filesystem::path& path) {
    const Json root = LoadJsonObject(path);
    const size_t active_index = JsonSize(root, "active_opened_index", JsonSize(root, "active_index", 0));
    const auto files_iter = root.find("opened_files");
    if (files_iter == root.end() || !files_iter->is_array()) {
        return;
    }

    OpenedConfig config;
    config.active_index = active_index;
    for (const Json& object : *files_iter) {
        if (!object.is_object()) {
            continue;
        }
        OpenedFileState entry;
        entry.memory_only = JsonBool(object, "memory_only", false);
        entry.path = JsonString(object, "path");
        entry.content = JsonString(object, "content");
        entry.row = JsonSize(object, "row", 0);
        entry.column = JsonSize(object, "column", 0);

        if (entry.memory_only) {
            if (!entry.content.empty()) {
                config.files.push_back(std::move(entry));
            }
            continue;
        }

        const std::filesystem::path normalized_path = NormalizeDocumentPath(entry.path);
        if (normalized_path.empty()) {
            continue;
        }
        entry.path = normalized_path;
        config.files.push_back(std::move(entry));
    }
    SetOpenedConfig(std::move(config));
}

void DocumentFileController::LoadFavoritesFromJson(const std::filesystem::path& path) {
    const Json root = LoadJsonObject(path);

    auto load_objects = [this, &root](const char* key) {
        const auto iter = root.find(key);
        if (iter == root.end() || !iter->is_array()) {
            return false;
        }
        bool loaded = false;
        for (const Json& object : *iter) {
            if (!object.is_object()) {
                continue;
            }
            const std::filesystem::path normalized_path = NormalizeDocumentPath(JsonString(object, "path"));
            if (normalized_path.empty() || IsFavorite(normalized_path)) {
                continue;
            }
            favorite_files_.push_back({
                normalized_path,
                JsonSize(object, "row", 0),
                JsonSize(object, "column", 0),
            });
            loaded = true;
        }
        return loaded;
    };

    if (load_objects("favorites_") || load_objects("favorites")) {
        return;
    }

    auto load_strings = [this, &root](const char* key) {
        const auto iter = root.find(key);
        if (iter == root.end() || !iter->is_array()) {
            return false;
        }
        bool loaded = false;
        for (const Json& value : *iter) {
            if (!value.is_string()) {
                continue;
            }
            const std::filesystem::path normalized_path = NormalizeDocumentPath(value.get<std::string>());
            if (normalized_path.empty() || IsFavorite(normalized_path)) {
                continue;
            }
            favorite_files_.push_back({normalized_path, 0, 0});
            loaded = true;
        }
        return loaded;
    };

    load_strings("favorites_") || load_strings("favorites");
}

bool DocumentFileController::RemoveMissingRecentFiles() {
    const size_t old_size = recent_files_.size();
    recent_files_.erase(
        std::remove_if(
            recent_files_.begin(),
            recent_files_.end(),
            [](const RecentFileEntry& entry) {
                std::error_code error;
                return !std::filesystem::is_regular_file(entry.full_path, error);
            }),
        recent_files_.end());
    return recent_files_.size() != old_size;
}

std::filesystem::path DocumentFileController::DefaultStatePath() {
    const std::filesystem::path directory = UserConfigDirectory();
    if (directory.empty()) {
        return "document_files.json";
    }
    return directory / "document_files.json";
}

std::filesystem::path DocumentFileController::LegacyRecentFilesPath() {
    const std::filesystem::path directory = UserConfigDirectory();
    if (directory.empty()) {
        return "recent_files.json";
    }
    return directory / "recent_files.json";
}

std::filesystem::path DocumentFileController::LegacyOpenedFilesPath() {
    const std::filesystem::path directory = UserConfigDirectory();
    if (directory.empty()) {
        return "opened_config.json";
    }
    return directory / "opened_config.json";
}

std::filesystem::path DocumentFileController::LegacyConfigPath() {
    const std::filesystem::path directory = UserConfigDirectory();
    if (directory.empty()) {
        return "config.json";
    }
    return directory / "config.json";
}

std::filesystem::path DocumentFileController::LegacyFallbackConfigPath() {
    const std::filesystem::path home = UserHomeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / ".textlt_config.json";
}

std::filesystem::path DocumentFileController::UserConfigDirectory() {
#ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && !std::string(app_data).empty()) {
        return std::filesystem::path(app_data) / "textlt";
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile) / "AppData" / "Roaming" / "textlt";
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home) / ".config" / "textlt";
#endif
}

std::filesystem::path DocumentFileController::UserHomeDirectory() {
#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (!user_profile || std::string(user_profile).empty()) {
        return {};
    }
    return std::filesystem::path(user_profile);
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home);
#endif
}

std::filesystem::path DocumentFileController::NormalizePath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    std::filesystem::path normalized_path = std::filesystem::absolute(path, error);
    if (error) {
        return {};
    }

    normalized_path = std::filesystem::weakly_canonical(normalized_path, error);
    if (error) {
        normalized_path = normalized_path.lexically_normal();
    }
    return normalized_path;
}

std::filesystem::path DocumentFileController::NormalizeDocumentPath(
    const std::filesystem::path& path) {
    if (path.empty() || path == "Untitled" || path == "untitled.txt") {
        return {};
    }
    return NormalizePath(path);
}

DocumentFileController::RecentFileEntry DocumentFileController::MakeRecentEntry(
    const std::filesystem::path& path) {
    RecentFileEntry entry;
    entry.full_path = path;
    entry.folder_path = path.parent_path();
    entry.file_name = path.filename().string();
    return entry;
}

std::string DocumentFileController::DisplayName(const std::shared_ptr<Document>& document) {
    if (!document) {
        return {};
    }

    std::string name = document->path.filename().string();
    if (name.empty()) {
        name = document->path.string();
    }
    return name;
}

} // namespace textlt
