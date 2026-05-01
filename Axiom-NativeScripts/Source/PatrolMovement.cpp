#include <Scripting/NativeScript.hpp>

class PatrolMovement : public Axiom::NativeScript {
public:
	void Update(float dt) override {
		float x, y;
		GetPosition(x, y);
		m_Distance += 3.0f * dt * m_Dir;
		if (m_Distance > 5.0f || m_Distance < -5.0f) {
			m_Dir *= -1.0f;
		}
		SetPosition(x + 3.0f * dt * m_Dir, y);
	}

private:
	float m_Distance = 0.0f;
	float m_Dir = 1.0f;
};

REGISTER_SCRIPT(PatrolMovement)
