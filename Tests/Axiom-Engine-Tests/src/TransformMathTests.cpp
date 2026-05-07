#include <doctest/doctest.h>

#include "Collections/Mat2.hpp"
#include "Components/General/Transform2DComponent.hpp"

using namespace Axiom;

TEST_CASE("Transform rotation helpers share the same handedness")
{
	const float radians = Radians(35.0f);
	const Vec2 localPoint{ -5.0f, 0.0f };

	Transform2DComponent parent;
	parent.Position = { 0.0f, 0.0f };
	parent.Scale = { 1.0f, 1.0f };
	parent.Rotation = radians;

	const Vec2 transformed = parent.TransformPoint(localPoint);
	const Vec2 rotated = Rotation(radians) * localPoint;

	const float expectedX = -5.0f * Cos(radians);
	const float expectedY = -5.0f * Sin(radians);

	CHECK(transformed.x == doctest::Approx(expectedX));
	CHECK(transformed.y == doctest::Approx(expectedY));
	CHECK(rotated.x == doctest::Approx(transformed.x));
	CHECK(rotated.y == doctest::Approx(transformed.y));
}
