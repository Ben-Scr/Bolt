#include <Scripting/NativeScript.hpp>

class Spinner : public Axiom::NativeScript {
public:
	void Update(float dt) override {
		SetRotation(GetRotation() + 3.14159f * dt);
	}
};

REGISTER_SCRIPT(Spinner)
