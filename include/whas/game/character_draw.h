#pragma once
#include "whas/game/character.h"
#include <raylib.h>

// Body, facing eye, flames while burning and (optionally) the health bar
void DrawCharacterBody(const Character &c, Color color, bool drawHp);
