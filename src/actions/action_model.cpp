#include "actions/action_model.hpp"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>

namespace textlt::actions {

std::string LimitUtf8Characters(
    const std::string& value, std::size_t max_characters) {
    std::size_t offset = 0;
    std::size_t characters = 0;
    while (offset < value.size() && characters < max_characters) {
        const unsigned char first = static_cast<unsigned char>(value[offset]);
        std::size_t length = 1;
        if ((first & 0xE0) == 0xC0) length = 2;
        else if ((first & 0xF0) == 0xE0) length = 3;
        else if ((first & 0xF8) == 0xF0) length = 4;

        if (offset + length > value.size()) {
            length = 1;
        } else {
            for (std::size_t index = 1; index < length; ++index) {
                if ((static_cast<unsigned char>(value[offset + index]) & 0xC0) != 0x80) {
                    length = 1;
                    break;
                }
            }
        }
        offset += length;
        ++characters;
    }
    return offset == value.size() ? value : value.substr(0, offset);
}

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
