#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "notes/note_repository.hpp"
#include "notes/note_serializer.hpp"
#include "notes/notes_sync.hpp"
#include "remote/remote_provider.hpp"

namespace {

class FakeProvider : public textlt::IRemoteProvider {
public:
    bool connect_succeeds = true;
    std::set<std::string> directories{"/"};
    std::map<std::string, std::string> files;

    bool Connect(const textlt::RemoteConnectionConfig&, std::string& error) override {
        if (!connect_succeeds) {
            error = "offline";
        }
        return connect_succeeds;
    }
    bool TestConnection(std::string& output, std::string&) override {
        output = "ok";
        return true;
    }
    bool List(
        const std::string& path,
        std::vector<textlt::RemoteEntry>& entries,
        std::string& error) override {
        if (!directories.count(path)) {
            error = "missing directory";
            return false;
        }
        entries.clear();
        const std::string prefix = path == "/" ? "/" : path + "/";
        for (const std::string& directory : directories) {
            if (directory == path || directory.rfind(prefix, 0) != 0) continue;
            const std::string relative = directory.substr(prefix.size());
            if (relative.empty() || relative.find('/') != std::string::npos) continue;
            entries.push_back({directory, relative, textlt::RemoteEntryType::Directory});
        }
        for (const auto& file : files) {
            if (file.first.rfind(prefix, 0) != 0) continue;
            const std::string relative = file.first.substr(prefix.size());
            if (relative.empty() || relative.find('/') != std::string::npos) continue;
            entries.push_back({file.first, relative, textlt::RemoteEntryType::File});
        }
        return true;
    }
    bool Download(
        const std::string& remote_path,
        const std::string& local_path,
        std::string& error) override {
        const auto found = files.find(remote_path);
        if (found == files.end()) {
            error = "missing file";
            return false;
        }
        std::ofstream output(local_path, std::ios::binary);
        output << found->second;
        return static_cast<bool>(output);
    }
    bool Upload(
        const std::string& local_path,
        const std::string& remote_path,
        std::string&) override {
        std::ifstream input(local_path, std::ios::binary);
        files[remote_path] = std::string(
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return static_cast<bool>(input) || input.eof();
    }
    bool DownloadDirectory(const std::string&, const std::string&, std::string&) override {
        return false;
    }
    bool UploadDirectory(const std::string&, const std::string&, std::string&) override {
        return false;
    }
    bool Rename(const std::string&, const std::string&, std::string&) override {
        return false;
    }
    bool RemoveFile(const std::string&, std::string&) override { return false; }
    bool MakeDirectory(const std::string& path, std::string&) override {
        directories.insert(path);
        return true;
    }
    bool RemoveDirectory(const std::string&, std::string&) override { return false; }
};

void Expect(bool value, const std::string& message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace textlt;
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "textlt_notes_sync_test";
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);

    notes::NoteRepository repository(root);
    std::string warning;
    Expect(repository.Load(warning), warning);
    notes::NoteDocument local = notes::MakeNewNote(repository.DeviceId());
    local.title = "Local";
    local.revision = 1;
    local.updated_at = "2026-07-23T08:00:00Z";
    std::string error;
    Expect(repository.Save(local, error), error);

    FakeProvider provider;
    RemoteConnectionConfig connection;
    connection.remote_root = "/";
    notes::NotesSyncResult result;
    Expect(notes::SyncNotes(root, connection, provider, result, error), error);
    Expect(provider.files.count("/.textlt-notes/items/" + local.id + ".tlnote") == 1,
        "Local note was not uploaded.");

    notes::NoteDocument remote = local;
    remote.title = "Remote";
    remote.revision = 2;
    remote.updated_at = "2026-07-23T09:00:00Z";
    const fs::path remote_file = root / "remote.tlnote";
    Expect(notes::NoteSerializer::SaveNote(remote, remote_file, error), error);
    std::ifstream input(remote_file, std::ios::binary);
    provider.files["/.textlt-notes/items/" + local.id + ".tlnote"] = std::string(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    Expect(notes::SyncNotes(root, connection, provider, result, error), error);

    notes::NoteRepository reloaded(root);
    Expect(reloaded.Load(warning), warning);
    Expect(reloaded.Notes().size() == 1, "Unexpected synchronized note count.");
    Expect(reloaded.Notes().front().title == "Remote", "Newer remote revision did not win.");

    provider.connect_succeeds = false;
    Expect(!notes::SyncNotes(root, connection, provider, result, error),
        "Offline sync should fail.");
    Expect(reloaded.Notes().front().title == "Remote",
        "Failed sync must not modify loaded local notes.");

    fs::remove_all(root, cleanup_error);
    return 0;
}
