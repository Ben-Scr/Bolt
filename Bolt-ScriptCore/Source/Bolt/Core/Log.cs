using System;

namespace Bolt
{
    /// <summary>
    /// Logging interface that forwards messages to the native Bolt logging system (spdlog).
    /// </summary>
    public static class Log
    {
        public static event Action<string>? LogMessage;

        internal static void RaiseLogMessage(string message) => LogMessage?.Invoke(message);

        private static void Write(string message, Action<string> nativeWrite)
        {
            nativeWrite(message);
            RaiseLogMessage(message);
            Hosting.ScriptInstanceManager.DispatchLogMessage(message);
        }

        public static void Trace(string message) => Write(message, InternalCalls.Log_Trace);
        public static void Trace(object obj) => Trace(obj?.ToString() ?? "null");

        public static void Info(string message) => Write(message, InternalCalls.Log_Info);
        public static void Info(object obj) => Info(obj?.ToString() ?? "null");

        public static void Warn(string message) => Write(message, InternalCalls.Log_Warn);
        public static void Warn(object obj) => Warn(obj?.ToString() ?? "null");

        public static void Error(string message) => Write(message, InternalCalls.Log_Error);
        public static void Error(object obj) => Error(obj?.ToString() ?? "null");
    }
}
