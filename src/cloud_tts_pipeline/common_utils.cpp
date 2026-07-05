namespace {

constexpr size_t kMinimumChunkBytes = 1229;
constexpr size_t kIdealChunkBytes = 1843;
constexpr size_t kMaximumChunkBytes = 2253;
constexpr size_t kMaximumWordLength = 25;

bool IsSentenceBoundary(char character) {
    return character == '.' || character == '!' || character == '?';
}

std::string SplitIdentifierToken(const std::string& token) {
    std::string split;
    split.reserve(token.size() + 4);

    for (size_t index = 0; index < token.size(); ++index) {
        const unsigned char current = static_cast<unsigned char>(token[index]);
        if (current == '_') {
            if (!split.empty() && split.back() != ' ') {
                split.push_back(' ');
            }
            continue;
        }

        const unsigned char previous = index > 0
            ? static_cast<unsigned char>(token[index - 1])
            : 0;
        const unsigned char next = index + 1 < token.size()
            ? static_cast<unsigned char>(token[index + 1])
            : 0;

        const bool lower_to_upper =
            index > 0 && std::islower(previous) && std::isupper(current);
        const bool acronym_to_word =
            index > 0 &&
            index + 1 < token.size() &&
            std::isupper(previous) &&
            std::isupper(current) &&
            std::islower(next);

        if (!split.empty() && (lower_to_upper || acronym_to_word)) {
            split.push_back(' ');
        }
        split.push_back(static_cast<char>(current));
    }

    return split;
}

unsigned long long StableHash(const std::string& text) {
    unsigned long long hash = 14695981039346656037ull;
    for (unsigned char character : text) {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    return hash;
}


std::string SafePathSegment(const std::string& value) {
    std::string segment;
    segment.reserve(value.size());
    for (unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' || character == '.') {
            segment.push_back(static_cast<char>(character));
        } else {
            segment.push_back('_');
        }
    }
    return segment.empty() ? "default" : segment;
}


} // namespace
