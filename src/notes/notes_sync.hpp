#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

#include "remote/remote_connection_config.hpp"

namespace textlt {

class IRemoteProvider;

namespace notes {

struct NotesSyncResult {
    size_t downloaded = 0;
    size_t uploaded = 0;
};

using NotesSyncProgressCallback = std::function<void(const std::string&)>;

bool SyncNotes(
    const std::filesystem::path& local_root,
    const RemoteConnectionConfig& connection,
    IRemoteProvider& provider,
    NotesSyncResult& result,
    std::string& error,
    NotesSyncProgressCallback progress = {});

} // namespace notes
} // namespace textlt
