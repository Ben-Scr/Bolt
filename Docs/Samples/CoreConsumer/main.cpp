#include <Axiom/Core.hpp>

int main()
{
    Axiom::InitializeCore();
    AIM_CORE_INFO("Axiom core {} initialized", AIM_VERSION);
    Axiom::ShutdownCore();
    return 0;
}
