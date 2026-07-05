const std::unordered_set<std::string>& CppKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "bitand",
        "bitor",
        "break",
        "case",
        "catch",
        "class",
        "compl",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "const_cast",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "requires",
        "reinterpret_cast",
        "return",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "using",
        "virtual",
        "volatile",
        "while",
        "xor",
        "xor_eq",
    };
    return keywords;
}

const std::unordered_set<std::string>& CppTypes() {
    static const std::unordered_set<std::string> types = {
        "auto",
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "size_t",
        "std::filesystem::path",
        "std::optional",
        "std::string",
        "std::string_view",
        "std::unordered_map",
        "std::unordered_set",
        "std::vector",
        "uint8_t",
        "uint16_t",
        "uint32_t",
        "uint64_t",
        "unsigned",
        "void",
        "wchar_t",
    };
    return types;
}

const std::unordered_set<std::string>& JsKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "if",
        "else",
        "return",
        "for",
        "while",
        "switch",
        "case",
        "break",
        "function",
        "class",
        "export",
        "import",
    };
    return keywords;
}

const std::unordered_set<std::string>& JsTypes() {
    static const std::unordered_set<std::string> types = {
        "const",
        "let",
        "var",
        "true",
        "false",
        "null",
        "undefined",
        "document",
        "window",
    };
    return types;
}

const std::unordered_set<std::string>& TypescriptKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "const",
        "let",
        "var",
        "function",
        "return",
        "if",
        "else",
        "for",
        "while",
        "switch",
        "case",
        "break",
        "class",
        "export",
        "import",
        "from",
        "as",
        "extends",
        "implements",
        "new",
        "this",
        "yield",
        "await",
        "async",
    };
    return keywords;
}

const std::unordered_set<std::string>& TypescriptTypes() {
    static const std::unordered_set<std::string> types = {
        "interface",
        "type",
        "enum",
        "public",
        "private",
        "protected",
        "readonly",
        "abstract",
        "keyof",
        "any",
        "unknown",
        "never",
        "void",
        "string",
        "number",
        "boolean",
        "undefined",
        "null",
    };
    return types;
}

const std::unordered_set<std::string>& JavaKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "public",
        "private",
        "protected",
        "class",
        "interface",
        "enum",
        "extends",
        "implements",
        "return",
        "if",
        "else",
        "for",
        "while",
        "switch",
        "case",
        "new",
        "this",
        "super",
        "final",
        "static",
        "void",
        "abstract",
        "synchronized",
        "throw",
        "throws",
        "try",
        "catch",
    };
    return keywords;
}

const std::unordered_set<std::string>& JavaTypes() {
    static const std::unordered_set<std::string> types = {
        "int",
        "float",
        "double",
        "boolean",
        "char",
        "byte",
        "long",
        "short",
        "String",
        "Object",
        "System",
        "Integer",
        "List",
        "ArrayList",
        "Map",
        "HashMap",
    };
    return types;
}

const std::unordered_set<std::string>& CsharpKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "async",
        "await",
        "class",
        "else",
        "foreach",
        "get",
        "if",
        "internal",
        "namespace",
        "new",
        "private",
        "protected",
        "public",
        "return",
        "set",
        "string",
        "struct",
        "using",
        "var",
        "void",
        "int",
    };
    return keywords;
}

const std::unordered_set<std::string>& JsonKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "true",
        "false",
        "null",
    };
    return keywords;
}

const std::unordered_set<std::string>& GoKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "package",
        "import",
        "func",
        "return",
        "var",
        "type",
        "struct",
        "interface",
        "chan",
        "go",
        "select",
        "defer",
        "if",
        "else",
        "for",
        "range",
        "switch",
        "case",
        "default",
        "map",
        "const",
    };
    return keywords;
}

const std::unordered_set<std::string>& RustKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "fn",
        "let",
        "mut",
        "match",
        "impl",
        "trait",
        "struct",
        "enum",
        "pub",
        "use",
        "mod",
        "crate",
        "return",
        "if",
        "else",
        "loop",
        "while",
        "for",
        "in",
        "move",
        "async",
        "await",
        "type",
    };
    return keywords;
}

const std::unordered_set<std::string>& RustTypes() {
    static const std::unordered_set<std::string> types = {
        "bool",
        "char",
        "str",
        "String",
        "usize",
        "isize",
        "u8",
        "u16",
        "u32",
        "u64",
        "u128",
        "i8",
        "i16",
        "i32",
        "i64",
        "i128",
        "f32",
        "f64",
        "Self",
    };
    return types;
}

const std::unordered_set<std::string>& KotlinKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "as",
        "break",
        "class",
        "companion",
        "continue",
        "data",
        "do",
        "else",
        "for",
        "fun",
        "if",
        "import",
        "in",
        "interface",
        "is",
        "object",
        "package",
        "return",
        "sealed",
        "super",
        "this",
        "throw",
        "try",
        "typealias",
        "val",
        "var",
        "when",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& KotlinTypes() {
    static const std::unordered_set<std::string> types = {
        "Any",
        "Boolean",
        "Double",
        "Float",
        "Int",
        "Long",
        "Nothing",
        "String",
        "Unit",
        "false",
        "null",
        "true",
    };
    return types;
}

const std::unordered_set<std::string>& SwiftKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "associatedtype",
        "break",
        "case",
        "catch",
        "class",
        "continue",
        "defer",
        "do",
        "else",
        "enum",
        "extension",
        "fallthrough",
        "for",
        "func",
        "guard",
        "if",
        "import",
        "in",
        "init",
        "let",
        "protocol",
        "repeat",
        "return",
        "struct",
        "switch",
        "throw",
        "throws",
        "try",
        "var",
        "where",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& SwiftTypes() {
    static const std::unordered_set<std::string> types = {
        "Any",
        "AnyObject",
        "Bool",
        "Character",
        "Double",
        "Float",
        "Int",
        "Never",
        "String",
        "UInt",
        "false",
        "nil",
        "self",
        "Self",
        "true",
    };
    return types;
}

const std::unordered_set<std::string>& DartKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "abstract",
        "async",
        "await",
        "break",
        "case",
        "class",
        "const",
        "continue",
        "else",
        "enum",
        "export",
        "extends",
        "factory",
        "final",
        "for",
        "if",
        "implements",
        "import",
        "in",
        "late",
        "mixin",
        "new",
        "part",
        "return",
        "static",
        "switch",
        "this",
        "throw",
        "try",
        "var",
        "void",
        "while",
        "with",
        "yield",
    };
    return keywords;
}

const std::unordered_set<std::string>& DartTypes() {
    static const std::unordered_set<std::string> types = {
        "bool",
        "double",
        "dynamic",
        "false",
        "int",
        "List",
        "Map",
        "null",
        "num",
        "Object",
        "Set",
        "String",
        "true",
    };
    return types;
}
