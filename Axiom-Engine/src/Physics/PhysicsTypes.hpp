#pragma once
#include <cstdint>

namespace Axiom {
    enum  class ShapeType : int { Square, Circle, Polygon };
    enum class BodyType : int { Static, Kinematic, Dynamic };
}