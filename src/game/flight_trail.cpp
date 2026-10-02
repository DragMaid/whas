#include "whas/game/flight_trail.h"
#include "whas/audio/audio_manager.h"
#include "whas/constants.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kVertexSpacing = 2.0f; // cells between dropped vertices
constexpr float kLifetime = 0.8f;      // seconds a vertex lasts
constexpr float kFullSpeed = 60.0f;    // cells/s that gives the loudest rush
constexpr Color kTrailColor{90, 255, 150, 255};

Vector2 Feet(const Character &c) {
  return {c.pos.x + Character::WIDTH * 0.5f, c.pos.y + Character::HEIGHT};
}

Vector2 ToScreen(Vector2 cells) {
  return {cells.x * CELL_SIZE, cells.y * CELL_SIZE};
}

} // namespace

FlightTrail::Track &FlightTrail::TrackOf(int id) {
  for (Track &t : m_tracks)
    if (t.id == id)
      return t;
  m_tracks.push_back({id});
  return m_tracks.back();
}

void FlightTrail::Update(const Character *characters, int count, float dt) {
  for (Track &t : m_tracks) {
    for (Vertex &v : t.vertices)
      v.age += dt;
    std::erase_if(t.vertices,
                  [](const Vertex &v) { return v.age > kLifetime; });
  }

  float fastest = 0.0f, pan = 0.0f;
  for (int i = 0; i < count; ++i) {
    const Character &c = characters[i];
    Track &t = TrackOf(c.id);
    Vector2 feet = Feet(c);
    float moved = t.seen ? std::hypot(feet.x - t.last.x, feet.y - t.last.y)
                         : 0.0f;
    // A jump in position is a respawn or a new round, not flight
    if (moved > 20.0f)
      moved = 0.0f;
    t.last = feet;
    t.seen = true;
    if (!c.flying || !c.Alive())
      continue;

    if (t.vertices.empty() ||
        std::hypot(feet.x - t.vertices.back().pos.x,
                   feet.y - t.vertices.back().pos.y) >= kVertexSpacing)
      t.vertices.push_back({feet, 0.0f});
    float speed = dt > 0.0f ? moved / dt : 0.0f;
    if (speed > fastest) {
      fastest = speed;
      pan = std::clamp(feet.x / GRID_W * 2.0f - 1.0f, -1.0f, 1.0f) * 0.8f;
    }
  }

  if (AudioManager *audio = AudioManager::Instance())
    audio->SetProfileState(SoundProfile::Flight,
                           {std::min(1.0f, fastest / kFullSpeed), 0.0f, pan});
}

void FlightTrail::Draw() const {
  BeginBlendMode(BLEND_ADDITIVE);
  for (const Track &t : m_tracks) {
    const std::vector<Vertex> &v = t.vertices;
    for (size_t i = 0; i < v.size(); ++i) {
      float life = 1.0f - v[i].age / kLifetime;
      Color c = kTrailColor;
      c.a = static_cast<unsigned char>(220 * life);
      Vector2 at = ToScreen(v[i].pos);
      if (i > 0)
        DrawLineEx(ToScreen(v[i - 1].pos), at, 1.0f + life, c);
      // Each vertex a small square, like a point on a wireframe
      float half = 1.0f + 1.5f * life;
      DrawRectangleV({at.x - half, at.y - half}, {half * 2, half * 2}, c);
      DrawRectangleLinesEx({at.x - half - 1, at.y - half - 1, half * 2 + 2,
                            half * 2 + 2},
                           1.0f, Color{c.r, c.g, c.b, (unsigned char)(c.a / 3)});
    }
  }
  EndBlendMode();
}
