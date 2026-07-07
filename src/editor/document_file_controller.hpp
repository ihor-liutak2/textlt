#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "editor/document_workspace.hpp"
#include "file_manager.hpp"

namespace textlt {

class Document;

class DocumentFileController {
public:
    static constexpr size_t kMaxRecentFiles = 60;

    struct RecentFileEntry {
        std::filesystem::path full_path;
        std::filesystem::path folder_path;
        std::string file_name;
    };

    struct FavoriteFileEntry {
        std::filesystem::path path;
        size_t row = 0;
        size_t column = 0;
    };

    struct SaveAllResult {
        size_t saved_count = 0;
        size_t skipped_count = 0;
        std::string first_error;
    };

    enum class FavoriteToggleStatus {
        Added,
        Removed,
        NeedsSave,
    };

    struct FavoriteToggleResult {
        FavoriteToggleStatus status = FavoriteToggleStatus::NeedsSave;
        std::string path;
    };

    DocumentFileController(FileManager& file_manager, DocumentWorkspace& workspace);

    std::shared_ptr<Document> ActiveDocument() const;

    size_t AddDocument(std::shared_ptr<Document> document);
    size_t NewDocument();
    void EnsureOneDocument(size_t visible_pane_count);
    bool RemoveDocument(size_t index, size_t visible_pane_count);
    bool ActivateDocument(size_t index, size_t visible_pane_count);

    bool OpenDocument(const std::filesystem::path& path, std::string& error);
    bool OpenOrCreateDocument(const std::filesystem::path& path, std::string& error);
    bool SaveActiveDocument(std::string& error);
    bool SaveActiveDocumentAs(const std::filesystem::path& path, std::string& error);
    SaveAllResult SaveAllDirtyDocuments();

    bool CloseActiveDocument(size_t visible_pane_count, std::string& closed_name);
    void CloseAllDocuments(size_t visible_pane_count);

    void PersistOpenedDocuments();
    bool RestoreOpenedDocuments(size_t visible_pane_count);

    const std::vector<RecentFileEntry>& RecentFiles() const;
    void RefreshRecentFiles();
    bool RemoveRecentFile(const std::filesystem::path& path);

    const std::vector<FavoriteFileEntry>& FavoriteFiles() const;
    std::vector<std::filesystem::path> FavoriteFilePaths() const;
    bool RemoveFavorite(const std::filesystem::path& path);
    std::string ActiveDocumentFavoritePath() const;
    bool IsActiveDocumentFavorite() const;
    void PersistActiveFavoriteCursor();
    void RestoreFavoriteCursor(const std::filesystem::path& path);
    FavoriteToggleResult ToggleActiveFavorite();

    bool LoadState();
    bool SaveState() const;
    std::filesystem::path StatePath() const;

private:
    struct OpenedFileState {
        std::filesystem::path path;
        std::string content;
        size_t row = 0;
        size_t column = 0;
        bool memory_only = false;
    };

    struct OpenedConfig {
        std::vector<OpenedFileState> files;
        size_t active_index = 0;
    };

    void AddRestoredDocument(const OpenedFileState& entry);
    void AddRecentFile(const std::filesystem::path& path);
    bool AddFavorite(const std::filesystem::path& path);
    bool IsFavorite(const std::filesystem::path& path) const;
    const FavoriteFileEntry* FindFavorite(const std::filesystem::path& path) const;
    bool UpdateFavoriteCursor(const std::filesystem::path& path, size_t row, size_t column);
    OpenedConfig CurrentOpenedConfig() const;
    void SetOpenedConfig(OpenedConfig config);
    bool LoadUnifiedState();
    void LoadRecentFilesFromJson(const std::filesystem::path& path);
    void LoadOpenedFilesFromJson(const std::filesystem::path& path);
    bool RemoveMissingRecentFiles();

    static std::filesystem::path DefaultStatePath();
    static std::filesystem::path UserConfigDirectory();
    static std::filesystem::path NormalizePath(const std::filesystem::path& path);
    static std::filesystem::path NormalizeDocumentPath(const std::filesystem::path& path);
    static RecentFileEntry MakeRecentEntry(const std::filesystem::path& path);
    static std::string DisplayName(const std::shared_ptr<Document>& document);

    FileManager& file_manager_;
    DocumentWorkspace& workspace_;
    std::filesystem::path state_path_;
    std::vector<RecentFileEntry> recent_files_;
    std::vector<FavoriteFileEntry> favorite_files_;
    OpenedConfig opened_config_;
};

} // namespace textlt
