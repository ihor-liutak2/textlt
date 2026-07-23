#include "notes/notes_sync.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>
#include <vector>

#include "notes/note_repository.hpp"
#include "notes/note_serializer.hpp"
#include "remote/remote_entry.hpp"
#include "remote/remote_provider.hpp"

namespace textlt::notes {
namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("textlt-notes-sync-" + GenerateUuid())) {}
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool FindEntry(
    const std::vector<RemoteEntry>& entries,
    const std::string& name,
    RemoteEntryType type,
    RemoteEntry* found = nullptr) {
    const auto iter = std::find_if(entries.begin(), entries.end(), [&](const RemoteEntry& entry) {
        return entry.name == name && entry.type == type;
    });
    if (iter == entries.end()) {
        return false;
    }
    if (found) {
        *found = *iter;
    }
    return true;
}

bool EnsureRemoteDirectory(
    IRemoteProvider& provider,
    const std::string& parent,
    const std::string& name,
    std::string& path,
    std::string& error) {
    std::vector<RemoteEntry> entries;
    if (!provider.List(parent, entries, error)) {
        return false;
    }
    RemoteEntry found;
    if (FindEntry(entries, name, RemoteEntryType::Directory, &found)) {
        path = found.path;
        return true;
    }
    path = JoinRemotePath(parent, name);
    return provider.MakeDirectory(path, error);
}

bool RemoteWins(const NoteDocument& remote, const NoteDocument& local) {
    if (remote.revision != local.revision) {
        return remote.revision > local.revision;
    }
    if (remote.updated_at != local.updated_at) {
        return remote.updated_at > local.updated_at;
    }
    return remote.modified_by_device_id > local.modified_by_device_id;
}

bool IsSafeId(const std::string& value) {
    return !value.empty() &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '-';
        });
}

} // namespace

bool SyncNotes(
    const std::filesystem::path& local_root,
    const RemoteConnectionConfig& connection,
    IRemoteProvider& provider,
    NotesSyncResult& result,
    std::string& error,
    NotesSyncProgressCallback progress) {
    result = {};
    if (!provider.Connect(connection, error)) {
        return false;
    }
    std::string test_output;
    if (!provider.TestConnection(test_output, error)) {
        return false;
    }
    if (progress) {
        progress("Connection established. Synchronizing notes...");
    }

    NoteRepository local(local_root);
    std::string warning;
    if (!local.Load(warning)) {
        error = warning.empty() ? "Could not load local notes." : warning;
        return false;
    }

    const std::string base = NormalizeRemoteDirectory(
        connection.remote_root.empty() ? "/" : connection.remote_root);
    std::string notes_path;
    if (!EnsureRemoteDirectory(provider, base, ".textlt-notes", notes_path, error)) {
        return false;
    }
    std::string items_path;
    if (!EnsureRemoteDirectory(provider, notes_path, "items", items_path, error)) {
        return false;
    }

    std::vector<RemoteEntry> root_entries;
    std::vector<RemoteEntry> item_entries;
    if (!provider.List(notes_path, root_entries, error) ||
        !provider.List(items_path, item_entries, error)) {
        return false;
    }

    TemporaryDirectory temporary;
    std::error_code fs_error;
    std::filesystem::create_directories(temporary.Path() / "items", fs_error);
    if (fs_error) {
        error = "Could not create temporary Notes sync directory: " + fs_error.message();
        return false;
    }

    std::vector<NoteSection> merged_sections = local.Sections();
    RemoteEntry remote_sections;
    if (FindEntry(root_entries, "sections.json", RemoteEntryType::File, &remote_sections)) {
        const auto downloaded_sections = temporary.Path() / "remote-sections.json";
        if (!provider.Download(remote_sections.path, downloaded_sections.string(), error)) {
            return false;
        }
        std::vector<NoteSection> sections;
        if (!NoteSerializer::LoadSections(downloaded_sections, sections, error)) {
            return false;
        }
        for (const NoteSection& remote : sections) {
            auto local_section = std::find_if(
                merged_sections.begin(), merged_sections.end(),
                [&](const NoteSection& value) { return value.id == remote.id; });
            if (local_section == merged_sections.end()) {
                merged_sections.push_back(remote);
            } else if (remote.updated_at > local_section->updated_at) {
                *local_section = remote;
            }
        }
    }

    std::map<std::string, NoteDocument> merged_notes;
    for (const NoteDocument& note : local.Notes()) {
        if (!IsSafeId(note.id)) {
            error = "Local Notes contains an unsafe note ID.";
            return false;
        }
        merged_notes[note.id] = note;
    }
    for (const RemoteEntry& entry : item_entries) {
        const std::filesystem::path filename(entry.name);
        if (entry.type != RemoteEntryType::File ||
            filename.filename() != filename ||
            filename.extension() != ".tlnote" ||
            !IsSafeId(filename.stem().string())) {
            continue;
        }
        const auto downloaded = temporary.Path() / "items" / entry.name;
        if (!provider.Download(entry.path, downloaded.string(), error)) {
            return false;
        }
        NoteDocument remote;
        if (!NoteSerializer::LoadNote(downloaded, remote, error)) {
            error = "Invalid remote note " + entry.name + ": " + error;
            return false;
        }
        if (!IsSafeId(remote.id) || remote.id != filename.stem().string()) {
            error = "Remote note filename does not match its safe note ID: " + entry.name;
            return false;
        }
        auto local_note = merged_notes.find(remote.id);
        if (local_note == merged_notes.end() || RemoteWins(remote, local_note->second)) {
            merged_notes[remote.id] = remote;
            ++result.downloaded;
        }
    }

    const auto merged_sections_path = temporary.Path() / "sections.json";
    if (!NoteSerializer::SaveSections(merged_sections, merged_sections_path, error) ||
        !provider.Upload(
            merged_sections_path.string(),
            JoinRemotePath(notes_path, "sections.json"),
            error)) {
        return false;
    }

    for (auto& pair : merged_notes) {
        const auto staged = temporary.Path() / "items" / (pair.first + ".tlnote");
        if (!NoteSerializer::SaveNote(pair.second, staged, error) ||
            !provider.Upload(
                staged.string(),
                JoinRemotePath(items_path, staged.filename().string()),
                error)) {
            return false;
        }
        ++result.uploaded;
    }

    for (auto& pair : merged_notes) {
        if (!NoteSerializer::SaveNote(
                pair.second,
                local_root / "items" / (pair.first + ".tlnote"),
                error)) {
            return false;
        }
    }
    if (!NoteSerializer::SaveSections(
            merged_sections, local_root / "sections.json", error)) {
        return false;
    }
    return true;
}

} // namespace textlt::notes
