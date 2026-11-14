#include "actions/action_model.hpp"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>

namespace textlt::actions {

std::string GenerateActionId() {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    std::array<unsigned char, 16> bytes{};
    for (auto& byte : bytes) byte = static_cast<unsigned char>(distribution(generator));
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) output << '-';
        output << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return output.str();
}

} // namespace textlt::actions
