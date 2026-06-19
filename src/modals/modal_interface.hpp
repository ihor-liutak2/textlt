#pragma once

#include <string>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

namespace textlt {

struct ModalSizePreference {
    int width = 0;
    int height = 0;
};

class IModalContent {
public:
    virtual ~IModalContent() = default;

    // Returns the ftxui::Element to be rendered as the content of the modal.
    virtual ftxui::Element Render() = 0;

    // Returns the main ftxui::Component that handles events for the content.
    // This component will receive focus and events when the modal is active.
    virtual ftxui::Component GetMainComponent() = 0;

    // Returns the title of the modal window.
    virtual std::string GetTitle() = 0;

    // Allows modal content to request a preferred outer modal size.
    virtual ModalSizePreference GetModalSizePreference() const { return {}; }
};

} // namespace textlt
