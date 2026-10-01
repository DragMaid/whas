#pragma once
#include "whas/game/character.h"
#include <vector>

// The green vertex line left behind someone riding wind underfoot, and the
// rush of air while they do. Presentation only: it reads characters and
// never changes them.
class FlightTrail {
public:
  // Once a frame with every character that can fly
  void Update(const Character *characters, int count, float dt);
  void Draw() const;
  void Clear() { m_tracks.clear(); }

private:
  struct Vertex {
    Vector2 pos; // cells
    float age;   // seconds
  };
  struct Track {
    int id = 0;
    Vector2 last{0.0f, 0.0f}; // feet last frame
    bool seen = false;
    std::vector<Vertex> vertices;
  };

  Track &TrackOf(int id);

  std::vector<Track> m_tracks;
};
