#pragma once

#include "textlt/connect/types.hpp"

namespace textlt::connect::localsend {

struct Error {
    TransferFailure code;
    std::string reason;
};

struct Cancelled {};

} // namespace textlt::connect::localsend
