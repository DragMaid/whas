#include "imgui.h"
#include "whas/campaign/campaign_play.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/ui/map_thumbnails.h"
#include "whas/ui/theme.h"
#include <algorithm>
#include <set>

// The minimap (top right) and the big map (Tab): rooms on their grid, the
// current one drawn live from the world, the others from their files, and
// rooms next to them that haven't been entered as "?".

using namespace Campaign;
using Theme::Tone;

namespace {

constexpr int LIVE_W = GRID_W / 2;
constexpr int LIVE_H = GRID_H / 2;
constexpr float ROOM_ASPECT = static_cast<float>(GRID_H) / GRID_W;

ImTextureID Tex(const Texture2D &t) { return (ImTextureID)(uintptr_t)t.id; }

} // namespace

void CampaignPlay::UpdateMinimap(const Simulation &sim) {
  m_liveAge += GetFrameTime();
  if (m_liveAge < 0.2f && m_live.id)
    return;
  m_liveAge = 0.0f;
  m_livePixels.resize(static_cast<size_t>(LIVE_W) * LIVE_H);
  for (int y = 0; y < LIVE_H; ++y)
    for (int x = 0; x < LIVE_W; ++x) {
      const Cell &c = sim.GetCell(x * 2, y * 2);
      m_livePixels[y * LIVE_W + x] =
          c.element == Element::AIR ? Color{0, 0, 0, 0} : m_colors.CellColor(c);
    }
  if (!m_live.id) {
    Image image = GenImageColor(LIVE_W, LIVE_H, Color{0, 0, 0, 0});
    m_live = LoadTextureFromImage(image);
    UnloadImage(image);
  }
  UpdateTexture(m_live, m_livePixels.data());
}

void CampaignPlay::ForgetMaps() {
  if (m_live.id)
    UnloadTexture(m_live);
  m_live = {};
  for (auto &[pos, seen] : m_seen)
    if (seen.picture.id)
      UnloadTexture(seen.picture);
  m_seen.clear();
}

const Texture2D *CampaignPlay::RoomPicture(RoomPos pos, bool load) {
  if (auto it = m_seen.find(pos); it != m_seen.end())
    return it->second.picture.id ? &it->second.picture : nullptr;
  if (!load)
    return nullptr;
  std::optional<RoomDef> room;
  if (auto it = m_cache.find(pos);
      it != m_cache.end() &&
      it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    room = it->second.get()->room;
  else {
    std::string error;
    room = LoadRoom(m_def.id, pos, error);
  }
  Seen &seen = m_seen[pos];
  if (!room)
    return nullptr;
  Image image = MapThumbnails::Render(room->terrain, false);
  seen.picture = LoadTextureFromImage(image);
  SetTextureFilter(seen.picture, TEXTURE_FILTER_BILINEAR);
  UnloadImage(image);
  seen.objects = room->objects;
  for (ObjectDef &o : seen.objects)
    o.spells.clear();
  return &seen.picture;
}

void CampaignPlay::DrawMap(ImDrawList *dl, ImVec2 min, ImVec2 max, float roomW,
                           bool all) {
  float s = View::UiScale();
  float roomH = roomW * ROOM_ASPECT;
  float gap = std::max(2.0f, roomW * 0.06f);
  // Rooms entered, and the ones beside them that exist
  std::set<RoomPos> unknown;
  for (RoomPos p : m_save.visited)
    for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
      if (RoomPos n = p.Step(dx, dy); m_def.HasRoom(n) && !m_save.visited.count(n))
        unknown.insert(n);

  ImVec2 origin; // screen position of room (0, 0)'s corner
  if (all) {
    int x0 = m_room.pos.x, x1 = x0, y0 = m_room.pos.y, y1 = y0;
    auto grow = [&](RoomPos p) {
      x0 = std::min(x0, p.x), x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
    };
    for (RoomPos p : m_save.visited)
      grow(p);
    for (RoomPos p : unknown)
      grow(p);
    float w = (x1 - x0 + 1) * (roomW + gap), h = (y1 - y0 + 1) * (roomH + gap);
    origin = {(min.x + max.x - w) * 0.5f - x0 * (roomW + gap),
              (min.y + max.y - h) * 0.5f - y0 * (roomH + gap)};
  } else {
    origin = {(min.x + max.x - roomW) * 0.5f - m_room.pos.x * (roomW + gap),
              (min.y + max.y - roomH) * 0.5f - m_room.pos.y * (roomH + gap)};
  }
  auto box = [&](RoomPos p) {
    ImVec2 a{origin.x + p.x * (roomW + gap), origin.y + p.y * (roomH + gap)};
    return std::pair{a, ImVec2{a.x + roomW, a.y + roomH}};
  };
  auto mark = [&](ImVec2 a, Vector2 cell) {
    return ImVec2{a.x + cell.x / GRID_W * roomW, a.y + cell.y / GRID_H * roomH};
  };

  dl->PushClipRect(min, max, true);
  for (RoomPos p : unknown) {
    auto [a, b] = box(p);
    dl->AddRectFilled(a, b, Theme::U32(Tone::Ink, 0.55f));
    dl->AddRect(a, b, Theme::U32(Tone::Line, 0.8f));
    ImVec2 ts = ImGui::CalcTextSize("?");
    dl->AddText({(a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f},
                Theme::U32(Tone::Faint), "?");
  }
  int loads = 2; // reading room files is quick, but not all at once
  for (RoomPos p : m_save.visited) {
    auto [a, b] = box(p);
    if (b.x < min.x || a.x > max.x || b.y < min.y || a.y > max.y)
      continue;
    bool here = p == m_room.pos;
    dl->AddRectFilled(a, b, Theme::U32(Tone::Ink, 0.85f));
    if (here && m_live.id)
      dl->AddImage(Tex(m_live), a, b);
    else if (const Texture2D *pic = RoomPicture(p, loads > 0 && (loads--, true)))
      dl->AddImage(Tex(*pic), a, b, {0, 0}, {1, 1}, IM_COL32(255, 255, 255, 170));
    // Gates and shrines still to take
    const std::vector<ObjectDef> *objects =
        here ? &m_room.objects
             : (m_seen.count(p) ? &m_seen[p].objects : nullptr);
    for (int i = 0; objects && i < (int)objects->size(); ++i) {
      const ObjectDef &o = (*objects)[i];
      ImVec2 c = mark(a, {o.pos.x, o.pos.y - 6});
      float r = std::clamp(roomW * 0.025f, 2.0f, 5.0f * s);
      if (o.kind == ObjectKind::Gate) {
        bool open = std::find(m_save.gates.begin(), m_save.gates.end(),
                              std::pair{p, i}) != m_save.gates.end();
        Theme::Diamond(dl, c, r * 1.3f, Theme::U32(open ? Tone::Verdigris : Tone::Faint));
      } else if (o.kind == ObjectKind::Shrine && !m_save.shrinesTaken.count({p, i})) {
        dl->AddCircleFilled(c, r, Theme::U32(Tone::BrassBright));
      } else if (o.kind == ObjectKind::Workbench) {
        dl->AddRectFilled({c.x - r, c.y - r * 0.6f}, {c.x + r, c.y + r * 0.6f},
                          IM_COL32(170, 120, 70, 255));
      }
    }
    if (here) {
      for (const Enemies::Enemy &e : m_enemies)
        if (e.body.Alive())
          dl->AddCircleFilled(mark(a, e.body.Center()), std::clamp(roomW * 0.018f, 1.5f, 4.0f * s),
                              Theme::U32(Tone::Oxblood));
      dl->AddCircleFilled(mark(a, m_player.Center()), std::clamp(roomW * 0.03f, 2.5f, 6.0f * s),
                          Theme::U32(Tone::Parchment));
      // Sealed edges still shut
      if (!RoomOpen()) {
        ImU32 seal = IM_COL32(150, 90, 220, 230);
        float t = 2.0f * s;
        if (m_room.sealed[EdgeLeft])
          dl->AddLine(a, {a.x, b.y}, seal, t);
        if (m_room.sealed[EdgeRight])
          dl->AddLine({b.x, a.y}, b, seal, t);
        if (m_room.sealed[EdgeUp])
          dl->AddLine(a, {b.x, a.y}, seal, t);
        if (m_room.sealed[EdgeDown])
          dl->AddLine({a.x, b.y}, b, seal, t);
      }
    }
    dl->AddRect(a, b, Theme::U32(here ? Tone::BrassBright : Tone::BrassDim),
                0.0f, 0, here ? 2.0f : 1.0f);
  }
  dl->PopClipRect();
}

float CampaignPlay::DrawMinimap(ImDrawList *dl) {
  float s = View::UiScale();
  float w = 220 * s, h = 120 * s;
  ImVec2 max{ImGui::GetIO().DisplaySize.x - 12 * s, 12 * s + h};
  ImVec2 min{max.x - w, 12 * s};
  dl->AddRectFilled({min.x - 4, min.y - 4}, {max.x + 4, max.y + 4},
                    Theme::U32(Tone::Ink, 0.7f), 4.0f);
  DrawMap(dl, min, max, 120 * s, false);
  dl->AddText({min.x + 4, max.y - ImGui::GetTextLineHeight() - 2},
              Theme::U32(Tone::Faint), "Tab: map");
  return max.y + 8 * s;
}

void CampaignPlay::DrawBigMap() {
  float s = View::UiScale();
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 screen = ImGui::GetIO().DisplaySize;
  dl->AddRectFilled({0, 0}, screen, Theme::U32(Tone::Ink, 0.8f));
  ImVec2 min{40 * s, 60 * s}, max{screen.x - 40 * s, screen.y - 40 * s};
  // Big enough rooms that the whole map still fits
  int x0 = m_room.pos.x, x1 = x0, y0 = m_room.pos.y, y1 = y0;
  for (RoomPos p : m_save.visited) {
    x0 = std::min(x0, p.x - 1), x1 = std::max(x1, p.x + 1);
    y0 = std::min(y0, p.y - 1), y1 = std::max(y1, p.y + 1);
  }
  float roomW = std::min((max.x - min.x) / (x1 - x0 + 1),
                         (max.y - min.y) / ((y1 - y0 + 1) * ROOM_ASPECT)) * 0.92f;
  roomW = std::min(roomW, 360 * s);
  const char *title = "The rooms you've been through";
  ImVec2 ts = ImGui::CalcTextSize(title);
  dl->AddText({(screen.x - ts.x) * 0.5f, 24 * s}, Theme::U32(Tone::Parchment), title);
  DrawMap(dl, min, max, roomW, true);
}
