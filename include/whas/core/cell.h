#pragma once
#include "whas/core/element.h"

struct Cell {
  Element element = Element::AIR;

  float temperature = 20.0f;  // Celsius
  float pressure    = 0.0f;   // Pascal
  float velocityX   = 0.0f;   // Pixels per frame
  float velocityY   = 0.0f;   // Pixels per frame
  float mass        = 0.0f;   // Grams
  float density     = 0.0f;   // g/cm^3

  float lifetime    = 0.0f;   // Seconds
  float hardness    = 0.0f;
  float moisture    = 0.0f;
  bool updated      = false;
};
