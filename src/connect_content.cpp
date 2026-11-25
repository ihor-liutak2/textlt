#include "connect_content.hpp"

#include <stdexcept>

#include "actions/action_snapshot.hpp"
#include "actions/actions_workspace_component.hpp"
#include "notes/notes_workspace_component.hpp"
#include "nlohmann/json.hpp"

namespace textlt {
namespace {

using namespace connect;
using Json = nlohmann::json;
constexpr SchemaVersion kSchema = 1;
constexpr char kNoteType[] = "textlt.note";
constexpr char kActionType[] = "textlt.action";

Json Decode(const StructuredPayload& payload) {
    return Json::parse(payload.bytes.begin(), payload.bytes.end(),
        [](int depth, Json::parse_event_t, Json&) {
            if (depth > 8) throw std::runtime_error("snapshot is nested too deeply");
            return true;
        });
}

template <class Decoder>
std::optional<std::string> ValidateSnapshot(SchemaVersion version, const StructuredPayload& payload,
                                            Decoder decode, const char* label) {
    if (version != kSchema) return "Unsupported schema version";
    try {
        decode(payload);
        return std::nullopt;
    } catch (const std::exception&) {
        return std::string("Invalid ") + label + " snapshot";
    }
}

std::pair<TransferMetadata, StructuredPayload> Snapshot(
    const char* type, std::string name, std::string encoded) {
    for (char& byte : name) {
        if (static_cast<unsigned char>(byte) < 32 || byte == 127) byte = ' ';
    }
    if (name.size() > 255) {
        std::size_t length = 255;
        while ((static_cast<unsigned char>(name[length]) & 0xc0) == 0x80) --length;
        name.resize(length);
    }
    return {{type, kSchema, std::move(name), encoded.size()}, {{encoded.begin(), encoded.end()}}};
}

notes::NoteDocument DecodeNote(const StructuredPayload& payload) {
    const auto json = Decode(payload);
    notes::NoteDocument note;
    note.title = json.at("title").get<std::string>();
    const auto& blocks = json.at("blocks");
    if (!blocks.is_array() || blocks.empty()) throw std::runtime_error("missing note blocks");
    for (const auto& item : blocks) {
        notes::NoteBlock block;
        const auto& indent = item.at("indent");
        const auto& heading = item.at("heading");
        if (!indent.is_number_integer() || indent < 0 || indent > 6 ||
            !heading.is_number_integer() || heading < 0 || heading > 3 ||
            !notes::ParseBlockType(item.at("type").get<std::string>(), block.type)) {
            throw std::runtime_error("invalid note block");
        }
        block.indent = indent.get<int>();
        block.heading = heading.get<int>();
        block.checked = item.at("checked").get<bool>();
        const auto& runs = item.at("runs");
        if (!runs.is_array() ||
            (block.heading && block.type != notes::NoteBlockType::Paragraph)) {
            throw std::runtime_error("invalid note runs or heading");
        }
        for (const auto& run : runs) block.runs.push_back({run.get<std::string>()});
        note.blocks.push_back(std::move(block));
    }
    return note;
}

std::optional<actions::ActionItem> DecodeAction(const StructuredPayload& payload) {
    if (payload.bytes.empty()) return std::nullopt;
    return actions::DecodeActionSnapshot(
        {reinterpret_cast<const char*>(payload.bytes.data()), payload.bytes.size()});
}

class NoteHandler final : public ContentHandler {
public:
    explicit NoteHandler(notes::NotesWorkspaceComponent& workspace) : workspace_(workspace) {}
    std::string TypeId() const override { return kNoteType; }
    std::vector<SchemaVersion> SupportedVersions() const override { return {kSchema}; }

    std::optional<std::string> Validate(SchemaVersion version, const StructuredPayload& payload) const override {
        return ValidateSnapshot(version, payload, DecodeNote, "note");
    }

    std::string Preview(SchemaVersion, const StructuredPayload& payload) const override {
        const auto note = DecodeNote(payload);
        std::string preview = "Note: " + note.title;
        for (const auto& block : note.blocks) {
            preview += '\n' + std::string(block.indent * 2, ' ');
            if (block.heading) preview += std::string(block.heading, '#') + ' ';
            if (block.type == notes::NoteBlockType::BulletItem) preview += "- ";
            if (block.type == notes::NoteBlockType::NumberedItem) preview += "1. ";
            if (block.type == notes::NoteBlockType::CheckItem) preview += block.checked ? "[x] " : "[ ] ";
            preview += notes::BlockText(block);
        }
        return preview;
    }

    std::vector<ImportDestination> Destinations() const override {
        std::vector<ImportDestination> destinations{{"", "General (no group)"}};
        for (const auto& section : workspace_.Sections()) {
            if (!section.deleted_at) destinations.push_back({section.id, section.name});
        }
        return destinations;
    }

    std::optional<std::string> ImportTo(SchemaVersion, const StructuredPayload& payload,
                                      const std::string& destination) override {
        auto note = DecodeNote(payload);
        for (auto& block : note.blocks) block.id = notes::GenerateUuid();
        std::string error;
        if (!workspace_.ImportNoteTo(std::move(note.title), std::move(note.blocks), destination, error)) {
            return error;
        }
        return std::nullopt;
    }

private:
    notes::NotesWorkspaceComponent& workspace_;
};

class ActionHandler final : public ContentHandler {
public:
    explicit ActionHandler(actions::ActionsWorkspaceComponent& workspace) : workspace_(workspace) {}
    std::string TypeId() const override { return kActionType; }
    std::vector<SchemaVersion> SupportedVersions() const override { return {kSchema}; }

    std::optional<std::string> Validate(SchemaVersion version, const StructuredPayload& payload) const override {
        if (version != kSchema) return "Unsupported schema version";
        if (!DecodeAction(payload)) return "Invalid action snapshot";
        return std::nullopt;
    }

    std::string Preview(SchemaVersion, const StructuredPayload& payload) const override {
        const auto action = DecodeAction(payload);
        if (!action) return "Invalid action snapshot";
        std::string preview = "Action: " + action->title;
        for (const auto& tab : action->tabs) {
            preview += '\n' + tab.caption + (tab.hide_text ? " (hidden text)" : "\n" + tab.content);
        }
        return preview;
    }

    std::vector<ImportDestination> Destinations() const override {
        std::vector<ImportDestination> destinations;
        for (const auto& group : workspace_.Groups()) {
            if (group.trash) continue;
            for (const auto& page : group.pages) destinations.push_back({page.id, group.name + " / " + page.name});
        }
        return destinations;
    }

    std::optional<std::string> ImportTo(SchemaVersion, const StructuredPayload& payload,
                                      const std::string& destination) override {
        auto action = DecodeAction(payload);
        if (!action) return "Invalid action snapshot";
        for (const auto& group : workspace_.Groups()) {
            if (group.trash) continue;
            for (const auto& page : group.pages) {
                if (page.id != destination) continue;
                std::string error;
                if (!workspace_.ImportActionsTo({std::move(*action)}, group.id, page.id, error)) return error;
                return std::nullopt;
            }
        }
        return "The Actions destination no longer exists";
    }

private:
    actions::ActionsWorkspaceComponent& workspace_;
};

} // namespace

std::unique_ptr<connect::ContentHandler> MakeNoteConnectHandler(notes::NotesWorkspaceComponent& workspace) {
    return std::make_unique<NoteHandler>(workspace);
}

std::unique_ptr<connect::ContentHandler> MakeActionConnectHandler(actions::ActionsWorkspaceComponent& workspace) {
    return std::make_unique<ActionHandler>(workspace);
}

std::pair<TransferMetadata, StructuredPayload> ConnectSnapshot(const notes::NoteDocument& note) {
    auto blocks = Json::array();
    for (const auto& block : note.blocks) {
        auto runs = Json::array();
        for (const auto& run : block.runs) runs.push_back(run.text);
        blocks.push_back({{"type", notes::BlockTypeName(block.type)}, {"indent", block.indent},
            {"heading", block.heading}, {"checked", block.checked}, {"runs", std::move(runs)}});
    }
    return Snapshot(kNoteType, note.title.empty() ? "Untitled note" : note.title,
                    Json({{"title", note.title}, {"blocks", std::move(blocks)}}).dump());
}

std::pair<TransferMetadata, StructuredPayload> ConnectSnapshot(const actions::ActionItem& action) {
    return Snapshot(kActionType, action.title, actions::EncodeActionSnapshot(action));
}

} // namespace textlt
