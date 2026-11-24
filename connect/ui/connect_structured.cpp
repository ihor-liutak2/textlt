#include "connect_workspace.hpp"

#include <algorithm>
#include <chrono>

#include "editor/clipboard_controller.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt::connect_ui {
namespace {
using namespace connect;
using namespace ftxui;

Elements PreviewLines(std::string_view preview, int width) {
    Elements rows;
    while (true) {
        const auto end = preview.find('\n');
        std::string row;
        int column = 0;
        for (const auto& glyph : Utf8ToGlyphs(preview.substr(0, end))) {
            const int cells = string_width(glyph);
            if (column && column + cells > width) {
                rows.push_back(text(std::move(row)));
                row.clear();
                column = 0;
            }
            row += glyph;
            column += cells;
        }
        rows.push_back(text(std::move(row)));
        if (end == std::string_view::npos) break;
        preview.remove_prefix(end + 1);
    }
    return rows;
}
} // namespace

void ConnectWorkspace::RegisterContentHandler(std::unique_ptr<connect::ContentHandler> handler) {
    if (!Running()) content_.RegisterHandler(std::move(handler));
}

void ConnectWorkspace::InitializeStructuredControls() {
    structured_menu_ = Menu(&choice_labels_, &choice_index_);
    structured_send_ = FlatAction("Send snapshot", ButtonRole::Primary, [this] {
        ConfirmStructuredChoice();
    });
    structured_import_ = FlatAction("Import copy", ButtonRole::Primary, [this] {
        ConfirmStructuredChoice();
    });
    structured_close_ = FlatAction("Close", ButtonRole::Cancel, [this] {
        CloseStructuredPicker();
    });
    structured_picker_ = Container::Vertical({
        structured_menu_, structured_send_, structured_import_, structured_close_,
    });
    Add(structured_picker_);
}

void ConnectWorkspace::Share(TransferMetadata metadata, StructuredPayload payload) {
    CloseStructuredPicker();
    if (!metadata.schema_version) return;
    const auto result = content_.ResolveHandler(metadata.type_id, *metadata.schema_version);
    if (!result.handler) {
        status_ = "This snapshot type or schema version is unsupported";
        return;
    }
    if (const auto error = result.handler->Validate(*metadata.schema_version, payload)) {
        status_ = *error;
        return;
    }
    pending_share_ = std::make_pair(std::move(metadata), std::move(payload));
    choice_index_ = 0;
    structured_menu_->TakeFocus();
}

bool ConnectWorkspace::Supports(const Conversation& chat, const TransferMetadata& metadata) const {
    return Running() && chat.online && metadata.schema_version &&
        std::any_of(chat.authenticated_capabilities.begin(), chat.authenticated_capabilities.end(),
            [&](const Capability& capability) {
                return capability.type_id == metadata.type_id &&
                    std::find(capability.schema_versions.begin(), capability.schema_versions.end(),
                              *metadata.schema_version) != capability.schema_versions.end();
            });
}

void ConnectWorkspace::OpenPreview(std::size_t index) {
    auto* chat = Selected();
    if (!chat || index >= chat->messages.size() || !chat->messages[index].item) return;
    pending_share_.reset();
    preview_ = std::make_pair(*selected_, index);
    const auto& item = *chat->messages[index].item;
    const auto handler = content_.ResolveHandler(item.metadata.type_id, *item.metadata.schema_version).handler;
    if (!handler) {
        CloseStructuredPicker();
        status_ = "This received type or schema version is unsupported";
        return;
    }
    preview_text_ = ClipboardController::NormalizeText(
        handler->Preview(*item.metadata.schema_version, std::get<StructuredPayload>(item.payload)));
    destinations_ = handler->Destinations();
    choice_index_ = 0;
    preview_scroll_ = 0;
    if (chat->messages[index].imported || destinations_.empty()) structured_close_->TakeFocus();
    else structured_menu_->TakeFocus();
}

ConnectWorkspace::Message* ConnectWorkspace::PreviewMessage() {
    if (!preview_) return nullptr;
    return &conversations_.at(preview_->first).messages.at(preview_->second);
}

void ConnectWorkspace::CloseStructuredPicker() {
    const bool open = pending_share_ || preview_;
    pending_share_.reset();
    preview_.reset();
    choice_labels_.clear();
    if (open) composer_->TakeFocus();
}

void ConnectWorkspace::ConfirmStructuredChoice() {
    if (choice_index_ < 0) return;
    const auto index = static_cast<std::size_t>(choice_index_);
    if (pending_share_) {
        if (index >= share_peers_.size()) return;
        const auto peer = share_peers_[index];
        auto& chat = conversations_.at(peer);
        const auto& [metadata, payload] = *pending_share_;
        if (!Supports(chat, metadata)) {
            status_ = "This peer no longer supports the snapshot version";
            return;
        }
        Select(peer);
        const auto id = "snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                        "-" + std::to_string(++next_message_);
        auto* provider = service_->FindProvider(peer.first);
        const auto result = provider->Send({{{peer.first, PeerRoute()}, id}, metadata, payload});
        if (!result.accepted) {
            status_ = result.reason;
            return;
        }
        chat.messages.push_back({id, *metadata.display_name, "Waiting for acceptance", true});
        chat.scroll = 0;
        status_ = "Waiting for acceptance";
        CloseStructuredPicker();
    } else if (auto* message = PreviewMessage(); message && !message->imported && index < destinations_.size()) {
        const auto& item = *message->item;
        auto* handler = content_.ResolveHandler(item.metadata.type_id, *item.metadata.schema_version).handler;
        if (!handler) return;
        const auto error = handler->ImportTo(*item.metadata.schema_version,
            std::get<StructuredPayload>(item.payload), destinations_[index].id);
        message->import_status = error ? "Import failed: " + *error
                                       : "Imported to " + destinations_[index].label;
        message->imported = !error;
        if (message->imported) structured_close_->TakeFocus();
    }
}

bool ConnectWorkspace::StructuredEvent(Event event) {
    if (event == Event::Escape) {
        CloseStructuredPicker();
        return true;
    }
    if (event == Event::Custom) return false;
    if (event.is_mouse()) {
        const auto mouse = event.mouse();
        if (const auto* message = PreviewMessage(); message && preview_content_box_.Contain(mouse.x, mouse.y)) {
            if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
                preview_scroll_ = std::clamp(preview_scroll_ + (mouse.button == Mouse::WheelUp ? -3 : 3),
                                             0, std::max(0, preview_rows_ - 1));
            }
            return true;
        }
        if (!workspace_box_.Contain(mouse.x, mouse.y)) return false;
        if (!choice_labels_.empty()) {
            if (structured_menu_->OnEvent(event)) return true;
            if ((pending_share_ ? structured_send_ : structured_import_)->OnEvent(event)) return true;
        }
        structured_close_->OnEvent(event);
        return true;
    }
    if (!Focused()) return false;
    if (preview_ && (event == Event::PageUp || event == Event::PageDown)) {
        preview_scroll_ = std::clamp(preview_scroll_ + (event == Event::PageUp ? -8 : 8),
                                     0, std::max(0, preview_rows_ - 1));
    } else if (event == Event::Tab || event == Event::TabReverse) {
        Components controls{structured_close_};
        if (!choice_labels_.empty()) {
            controls.push_back(structured_menu_);
            controls.push_back(pending_share_ ? structured_send_ : structured_import_);
        }
        const auto current = std::find_if(controls.begin(), controls.end(), [](const auto& control) {
            return control->Focused();
        });
        const int size = static_cast<int>(controls.size());
        const int index = current == controls.end() ? 0 : static_cast<int>(current - controls.begin());
        controls[(index + size + (event == Event::Tab ? 1 : -1)) % size]->TakeFocus();
    } else {
        structured_picker_->OnEvent(event);
    }
    return true;
}

Element ConnectWorkspace::RenderStructuredPicker(int width) {
    Elements rows;
    choice_labels_.clear();
    if (pending_share_) {
        rows.push_back(text("Send snapshot: " + pending_share_->first.display_name.value_or("")) | bold);
        share_peers_.clear();
        for (const auto& [key, chat] : conversations_) {
            if (!Supports(chat, pending_share_->first)) continue;
            share_peers_.push_back(key);
            choice_labels_.push_back(chat.name + " [" + key.second + "]");
        }
        if (choice_labels_.empty()) rows.push_back(text(Running() ? "No trusted peer supports this version"
                                                                  : "Start Connect networking to send") | dim);
    } else if (auto* message = PreviewMessage()) {
        rows.push_back(text("Received item preview") | bold);
        auto lines = PreviewLines(preview_text_, std::max(1, width - 1));
        preview_rows_ = static_cast<int>(lines.size());
        rows.push_back(vbox(std::move(lines)) | focusPosition(0, preview_scroll_) |
                       vscroll_indicator | yframe | flex | reflect(preview_content_box_));
        rows.push_back(text(message->import_status) | bold);
        if (!message->imported) {
            for (const auto& destination : destinations_) choice_labels_.push_back(destination.label);
            rows.push_back(text(destinations_.empty() ? "No local destinations" : "Import destination"));
        }
    }
    if (!choice_labels_.empty()) {
        choice_index_ = std::clamp(choice_index_, 0, static_cast<int>(choice_labels_.size()) - 1);
        rows.push_back(structured_menu_->Render() | vscroll_indicator | yframe | size(HEIGHT, LESS_THAN, 5));
    }
    rows.push_back(hbox({
        choice_labels_.empty() ? emptyElement() :
            (pending_share_ ? structured_send_->Render() : structured_import_->Render()),
        filler(), structured_close_->Render(),
    }));
    if (pending_share_) rows.push_back(text(status_) | dim);
    return vbox(std::move(rows)) | flex | borderStyled(LIGHT, theme_.gutter);
}
} // namespace textlt::connect_ui
