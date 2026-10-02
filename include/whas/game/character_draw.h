#pragma once
#include "whas/game/character.h"
#include <raylib.h>

// Animated sprite (idle / fly / fall, mirrored to face its way), flames while burning and (optionally) the health bar
void DrawCharacterBody(const Character &c, Color color, bool drawHp);

// Outline each body's collision box (the debug overlay turns it on)
void SetCharacterHitboxVisible(bool visible);

// Free the sprite sheets; call before CloseWindow
void UnloadCharacterSprites();
