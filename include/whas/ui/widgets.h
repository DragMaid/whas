#pragma once
#include "imgui.h"
#include "whas/audio/audio_manager.h"

// The game's own controls, drawn in the Theme: engraved brass-trimmed plates
// instead of ImGui's flat boxes. They all click, so use these instead of
// ImGui::Button and friends.
namespace Widgets {

inline void Click() {
  if (AudioManager *audio = AudioManager::Instance())
    audio->PlayUiClick();
}

// size like ImGui::Button: 0 fits the label, negative fills the width minus
// that much. A selected button wears brighter trim (toggles, current tab).
bool Button(const char *label, const ImVec2 &size = ImVec2(0, 0),
            bool selected = false);
// Compact, for rows of small actions
bool SmallButton(const char *label, bool selected = false);

inline bool InvisibleButton(const char *id, const ImVec2 &size,
                            ImGuiButtonFlags flags = 0) {
  bool pressed = ImGui::InvisibleButton(id, size, flags);
  if (pressed)
    Click();
  return pressed;
}

// A checkbox drawn as a brass diamond
bool Toggle(const char *label, bool *value);

// Heading text in the display face with a rule beneath
void SectionHeader(const char *text);
// Large heading, for page titles
void Title(const char *text);

// A sidebar entry: text with a brass bar and diamond when selected
bool NavItem(const char *label, bool selected, float width);

} // namespace Widgets
