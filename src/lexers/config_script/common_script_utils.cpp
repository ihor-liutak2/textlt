const std::unordered_set<std::string>& BashKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "if",
        "then",
        "else",
        "elif",
        "fi",
        "for",
        "in",
        "while",
        "until",
        "do",
        "done",
        "case",
        "esac",
        "function",
        "return",
        "exit",
        "local",
        "export",
        "alias",
        "echo",
        "printf",
    };
    return keywords;
}

const std::unordered_set<std::string>& PythonKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "def",
        "class",
        "return",
        "if",
        "elif",
        "else",
        "for",
        "while",
        "break",
        "continue",
        "import",
        "from",
        "as",
        "try",
        "except",
        "with",
        "lambda",
        "pass",
        "in",
        "is",
        "and",
        "or",
        "not",
    };
    return keywords;
}

const std::unordered_set<std::string>& PythonTypes() {
    static const std::unordered_set<std::string> types = {
        "print",
        "len",
        "range",
        "str",
        "int",
        "float",
        "list",
        "dict",
        "set",
        "tuple",
        "True",
        "False",
        "None",
        "self",
    };
    return types;
}

const std::unordered_set<std::string>& DockerfileKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "ADD",
        "ARG",
        "CMD",
        "COPY",
        "ENTRYPOINT",
        "ENV",
        "EXPOSE",
        "FROM",
        "RUN",
        "USER",
        "VOLUME",
        "WORKDIR",
    };
    return keywords;
}

const std::unordered_set<std::string>& CMakeKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "add_compile_definitions",
        "add_compile_options",
        "add_custom_command",
        "add_custom_target",
        "add_definitions",
        "add_executable",
        "add_library",
        "add_subdirectory",
        "cmake_minimum_required",
        "configure_file",
        "else",
        "elseif",
        "endforeach",
        "endfunction",
        "endif",
        "endmacro",
        "endwhile",
        "find_package",
        "foreach",
        "function",
        "if",
        "include",
        "include_directories",
        "install",
        "link_directories",
        "list",
        "macro",
        "message",
        "option",
        "project",
        "set",
        "set_property",
        "target_compile_definitions",
        "target_compile_features",
        "target_compile_options",
        "target_include_directories",
        "target_link_libraries",
        "target_sources",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& CMakeConstants() {
    static const std::unordered_set<std::string> constants = {
        "AND",
        "BOOL",
        "CACHE",
        "FALSE",
        "INTERFACE",
        "LANGUAGES",
        "OFF",
        "ON",
        "OR",
        "PRIVATE",
        "PUBLIC",
        "REQUIRED",
        "STATIC",
        "TRUE",
        "VERSION",
    };
    return constants;
}

const std::unordered_set<std::string>& RubyKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "class",
        "def",
        "else",
        "elsif",
        "end",
        "false",
        "if",
        "include",
        "module",
        "nil",
        "require",
        "return",
        "true",
        "unless",
        "until",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& PowershellKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "begin",
        "catch",
        "class",
        "continue",
        "data",
        "do",
        "else",
        "elseif",
        "end",
        "enum",
        "exit",
        "filter",
        "finally",
        "for",
        "foreach",
        "from",
        "function",
        "if",
        "in",
        "param",
        "process",
        "return",
        "switch",
        "throw",
        "trap",
        "try",
        "until",
        "using",
        "while",
        "workflow",
    };
    return keywords;
}

const std::unordered_set<std::string>& PowershellTypes() {
    static const std::unordered_set<std::string> types = {
        "bool",
        "byte",
        "datetime",
        "decimal",
        "double",
        "false",
        "hashtable",
        "int",
        "long",
        "null",
        "object",
        "pscustomobject",
        "string",
        "true",
    };
    return types;
}

const std::unordered_set<std::string>& HclKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "check",
        "data",
        "for_each",
        "import",
        "locals",
        "module",
        "moved",
        "output",
        "provider",
        "provisioner",
        "removed",
        "resource",
        "terraform",
        "variable",
    };
    return keywords;
}

const std::unordered_set<std::string>& HclConstants() {
    static const std::unordered_set<std::string> constants = {
        "false",
        "null",
        "true",
    };
    return constants;
}

const std::unordered_set<std::string>& TomlConstants() {
    static const std::unordered_set<std::string> constants = {
        "false",
        "inf",
        "nan",
        "true",
    };
    return constants;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

size_t ParsePowershellVariableEnd(const std::string& line, size_t index) {
    if (index + 1 >= line.size()) {
        return index + 1;
    }

    const char next = line[index + 1];
    if (next == '{') {
        const size_t close = line.find('}', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }

    size_t current = index + 1;
    while (current < line.size()) {
        const char character = line[current];
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '_' && character != ':' && character != '?') {
            break;
        }
        ++current;
    }
    return current == index + 1 ? index + 1 : current;
}

bool IsTomlBareKeyBody(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '_' || character == '-';
}

size_t FindTomlCommentStart(const std::string& line, size_t start) {
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    for (size_t index = start; index < line.size(); ++index) {
        const char character = line[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_double) {
            if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_double = false;
            }
            continue;
        }
        if (in_single) {
            if (character == '\'') {
                in_single = false;
            }
            continue;
        }
        if (character == '"') {
            in_double = true;
            continue;
        }
        if (character == '\'') {
            in_single = true;
            continue;
        }
        if (character == '#') {
            return index;
        }
    }
    return std::string::npos;
}
