#include <Scripting/NativeScript.hpp>
#include <cmath>

class FloatingMotion : public Axiom::NativeScript {
public:
	void Start() override { m_Time = 0.0f; }

	void Update(float dt) override {
		m_Time += dt;
		float x, y;
		GetPosition(x, y);
		SetPosition(x, std::sin(m_Time * 2.0f) * 1.5f);
	}

private:
	float m_Time = 0.0f;
};

REGISTER_SCRIPT(FloatingMotion)
