#pragma once

#include <memory>

#include "remote/remote_connection_config.hpp"

namespace textlt {

class IRemoteProvider;

std::unique_ptr<IRemoteProvider> CreateRemoteProvider(RemoteConnectionType type);

} // namespace textlt
