#pragma once
#include "imgui.h"
#include "whas/audio/audio_manager.h"

// ImGui buttons that click. Use these instead of ImGui::Button and friends
// so every button in the game sounds the same.
namespace Widgets {

inline void Click() {
  if (AudioManager *audio = AudioManager::Instance())
    audio->PlayUiClick();
}

inline bool Button(const char *label, const ImVec2 &size = ImVec2(0, 0)) {
  bool pressed = ImGui::Button(label, size);
  if (pressed)
    Click();
  return pressed;
}

inline bool SmallButton(const char *label) {
  bool pressed = ImGui::SmallButton(label);
  if (pressed)
    Click();
  return pressed;
}

inline bool InvisibleButton(const char *id, const ImVec2 &size,
                            ImGuiButtonFlags flags = 0) {
  bool pressed = ImGui::InvisibleButton(id, size, flags);
  if (pressed)
    Click();
  return pressed;
}

} // namespace Widgets
