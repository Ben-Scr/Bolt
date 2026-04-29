using Bolt.Interop;

namespace Bolt
{
    public static class Time
    {
        public static float DeltaTime => InternalCalls.Application_GetDeltaTime();
        public static float UnscaledDeltaTime => InternalCalls.Application_GetUnscaledDeltaTime();
        public static float FixedDeltaTime => InternalCalls.Application_GetFixedDeltaTime();
        public static float FixedUnscaledDeltaTime => InternalCalls.Application_GetFixedUnscaledDeltaTime();

        public static float ElapsedTime => InternalCalls.Application_GetElapsedTime();
    }
}
