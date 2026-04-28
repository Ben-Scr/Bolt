#include <Bolt/Core.hpp>

int main()
{
    Bolt::InitializeCore();
    BT_CORE_INFO("Bolt core {} initialized", BT_VERSION);
    Bolt::ShutdownCore();
    return 0;
}
