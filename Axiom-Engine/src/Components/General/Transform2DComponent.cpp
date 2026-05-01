#include "pch.hpp"
#include "Components/General/Transform2DComponent.hpp"

namespace Axiom {

	Transform2DComponent Transform2DComponent::FromPosition(const Vec2& pos) {
		Transform2DComponent tr;
		tr.Position = pos;
		return tr;
	}
	Transform2DComponent Transform2DComponent::FromScale(const Vec2& scale) {
		Transform2DComponent tr;
		tr.Scale = scale;
		return tr;
	}


	float Transform2DComponent::GetRotationDegrees() const { return Degrees(Rotation); }

	Vec2 Transform2DComponent::GetForwardDirection() const
	{
		const float a = Rotation;
		return Vec2(std::sin(a), std::cos(a));
	}

	glm::mat3 Transform2DComponent::GetModelMatrix() const {
		const float s = glm::sin(Rotation);
		const float c = glm::cos(Rotation);

		glm::mat3 rotMatrix{
			{ c,  s, 0.0f },
			{ -s, c, 0.0f },
			{ 0.0f, 0.0f, 1.0f }
		};


		glm::mat3 scaleMat{
			{ Scale.x, 0.0f,   0.0f },
			{ 0.0f,   Scale.y, 0.0f },
			{ 0.0f,   0.0f,    1.0f }
		};


		glm::mat3 transMat{
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ Position.x, Position.y, 1.0f }
		};

		return transMat * rotMatrix * scaleMat;
	}

	b2Rot Transform2DComponent::GetB2Rotation() const {
		return  b2Rot(Cos(Rotation), Sin(Rotation));
	}
}