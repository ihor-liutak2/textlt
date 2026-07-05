#include "modal_files.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

constexpr int kVisibleEntryRows = 16;
constexpr int kEntryNameWidth = 72;
constexpr int kEntrySizeWidth = 12;
constexpr int kMinDoubleClickMs = 80;
constexpr int kMaxDoubleClickMs = 500;

std::string BracketLabel(const std::string& label) {
    return "[" + label + "]";
}

std::string FileTypeLabel(FileEntryType type) {
    switch (type) {
        case FileEntryType::Directory:
            return "DIR";
        case FileEntryType::File:
            return "FILE";
        case FileEntryType::Symlink:
            return "LINK";
        case FileEntryType::Other:
            return "OTHER";
    }
    return "OTHER";
}

std::string WithTrailingSeparator(std::filesystem::path path) {
    std::string value = FileManager::PathToUtf8(path.lexically_normal());
    if (!value.empty() && value.back() != '/' && value.back() != '\\') {
        value += std::filesystem::path::preferred_separator;
    }
    return value;
}

} // namespace

FilesModalContent::FilesModalContent(
    const Theme* theme,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    FavoriteDirectoriesProvider favorite_directories_provider,
    AddFavoriteDirectoryCallback on_add_favorite_directory,
    CopyPathCallback on_copy_path,
    ConfirmPathCallback on_confirm_path,
    CloseCallback on_close,
    FilesChangedCallback on_files_changed)
    : theme_(theme),
      file_manager_(file_manager),
      start_directory_provider_(std::move(start_directory_provider)),
      favorite_directories_provider_(std::move(favorite_directories_provider)),
      on_add_favorite_directory_(std::move(on_add_favorite_directory)),
      on_copy_path_(std::move(on_copy_path)),
      on_confirm_path_(std::move(on_confirm_path)),
      on_close_(std::move(on_close)),
      on_files_changed_(std::move(on_files_changed)) {
    RebuildComponents();
}

#include "modal_files/components.cpp"
#include "modal_files/open_load.cpp"
#include "modal_files/operations.cpp"
#include "modal_files/selection_navigation.cpp"
#include "modal_files/events.cpp"
#include "modal_files/render.cpp"
#include "modal_files/wrapper.cpp"

} // namespace textlt
