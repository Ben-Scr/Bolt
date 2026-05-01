#include <Scripting/NativeScript.hpp>

class NewNativeScript1 : public Axiom::NativeScript {
public:
    void Start() override
    {
        AIM_NATIVE_LOG_INFO("NewNativeScript1 started!");
    }

    void Update(float dt) override
    {
    }

    void OnDestroy() override
    {
    }
};
REGISTER_SCRIPT(NewNativeScript1)
