#include "whas/ui/audio_settings.h"
#include "imgui.h"
#include "whas/audio/audio_manager.h"

void DrawAudioSettings(bool debug) {
  AudioManager *audio = AudioManager::Instance();
  if (!audio) {
    ImGui::TextDisabled("No audio");
    return;
  }
  AudioSettings &s = audio->Settings();
  ImGui::Checkbox("Mute", &s.muted);
  ImGui::BeginDisabled(s.muted);
  ImGui::SliderFloat("Master", &s.master, 0.0f, 1.0f, "%.2f");
  ImGui::SliderFloat("World", &s.ambient, 0.0f, 1.0f, "%.2f");
  ImGui::SliderFloat("Buttons", &s.ui, 0.0f, 1.0f, "%.2f");
  if (ImGui::TreeNode("Elements")) {
    for (size_t i = 0; i < SOUND_PROFILE_COUNT; ++i) {
      auto profile = static_cast<SoundProfile>(i);
      ImGui::SliderFloat(SoundProfileName(profile), &s.profile[i], 0.0f, 2.0f,
                         "%.2f");
      if (debug) {
        ImGui::SameLine();
        ImGui::TextDisabled("lvl %.2f  voices %d", audio->Level(profile),
                            audio->VoicesInUse(profile));
      }
    }
    ImGui::TreePop();
  }
  ImGui::EndDisabled();
  if (debug)
    ImGui::TextDisabled("Limiter gain %.2f", audio->LimiterGain());
}
