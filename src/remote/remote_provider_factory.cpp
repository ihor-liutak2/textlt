#include "remote/remote_provider_factory.hpp"

#include "remote/remote_dropbox_provider.hpp"
#include "remote/remote_ftps_provider.hpp"
#include "remote/remote_google_drive_provider.hpp"
#include "remote/remote_microsoft_drive_provider.hpp"
#include "remote/remote_sftp_provider.hpp"

namespace textlt {

std::unique_ptr<IRemoteProvider> CreateRemoteProvider(RemoteConnectionType type) {
    switch (type) {
        case RemoteConnectionType::Sftp:
            return std::make_unique<RemoteSftpProvider>();
        case RemoteConnectionType::Ftps:
            return std::make_unique<RemoteFtpsProvider>();
        case RemoteConnectionType::Dropbox:
            return std::make_unique<RemoteDropboxProvider>();
        case RemoteConnectionType::GoogleDrive:
            return std::make_unique<RemoteGoogleDriveProvider>();
        case RemoteConnectionType::MicrosoftDrive:
            return std::make_unique<RemoteMicrosoftDriveProvider>();
    }
    return {};
}

} // namespace textlt
