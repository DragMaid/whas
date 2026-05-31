#include "whas/element/base/factory.h"
#include "whas/element/base/properties.h"
#include <cstdlib>

Cell ElementFactory::Create(Element element) {
  const ElementProperties &props = ElementRegistry::GetProperties(element);

  Cell c;
  c.element = element;
  c.temperature = props.defaultTemperature;
  c.hardness = props.defaultHardness;
  c.density = props.density;
  c.mass = props.defaultMass;

  // TODO: can make a more abstraction layer in case we want to assign
  // attributes for a spell
  // Handle additional attribute if needed
  switch (element) {
  case Element::FIRE:
    // TODO: change to a more dedicated logic later
    c.lifetime = 3.0f + static_cast<float>(std::rand() % 200) / 100.0f;
    break;
  case Element::STEAM:
    // TODO: we should have a setting for this instead
    c.velocityY = -1.0f;
    c.lifetime = 8.0f;
    break;
  case Element::CLOUD:
    c.moisture = 1.0f;
    break;
  }

  return c;
}
