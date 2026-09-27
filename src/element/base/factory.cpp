#include "whas/element/base/factory.h"
#include "whas/core/config.h"

Cell ElementFactory::Create(Element element, const SimulationConfig& config) {
  const ElementProperties &props = config.elements[static_cast<std::size_t>(element)];

  Cell c;
  c.element = element;

  c.temperature = props.defaultTemperature;
  c.hardness = props.defaultHardness;
  c.density = props.density;

  c.mass = props.defaultMass;
  c.lifetime = props.defaultLifetime;
  c.moisture = props.defaultMoisture;

  c.vx = props.initialVx;
  c.vy = props.initialVy;
  
  // Explicitly initialize metadata
  c.lastUpdateFrame = 0;
  c.bodyID = -1;
  c.triangleID = -1;
  c.u = 0.0f;
  c.v = 0.0f;
  c.isStatic = false;

  return c;
}
