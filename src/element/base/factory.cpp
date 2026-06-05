#include "whas/element/base/factory.h"
#include "whas/element/base/properties.h"

Cell ElementFactory::Create(Element element) {
  const ElementProperties &props = ElementRegistry::GetProperties(element);

  Cell c;
  c.element = element;
  c.temperature = props.defaultTemperature;
  c.hardness = props.defaultHardness;
  c.density = props.density;
  c.mass = props.defaultMass;
  c.lifetime = props.defaultLifetime;
  c.moisture = props.defaultMoisture;

  // Handle additional attribute if needed
  switch (element) {
  case Element::STEAM:
    // TODO: move this to property also
    c.velocityY = -1.0f;
    break;
  }

  return c;
}
