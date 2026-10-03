#pragma once
#include <imgui.h>

// Small drawn icons for the editors' toolbars
enum class EditorIcon {
  Brush,
  Select,
  Start,
  Gate,
  Bench,
  Shrine,
  Mage,
  Undead,
  Flyer,
  Npc,
  Region,
  Settle,
};

void DrawEditorIcon(ImDrawList *dl, EditorIcon icon, ImVec2 centre, float size,
                    ImU32 color);

// Before a toolbar item `width` wide placed with SameLine: start a new row
// when it wouldn't fit
void WrapToolbar(float width);

// A square toolbar button with an icon; the tooltip names it and its key
bool IconButton(const char *id, EditorIcon icon, bool selected,
                const char *tooltip);
