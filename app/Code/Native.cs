using System;
using System.Runtime.InteropServices;

namespace TransmissionAndroid.Code
{
    public static class Native
    {
        public const string libtransmission = "libtransmission.so";

        [DllImport(libtransmission)]
        private static extern IntPtr InitDaemon(int argc, IntPtr argv, IntPtr web_folder, IntPtr session_folder);

        [DllImport(libtransmission)]
        public static extern bool StartDaemon(IntPtr daemon, bool foreground);

        public static IntPtr InitDaemon(TransmissionConfig config)
        {
            using var nativeArgs = new NativeArray(config.Args);
            using var nativeWebFolder = new NativeString(config.WebFolder);
            using var nativeSessionFolder = new NativeString(config.SessionFolder);

            return InitDaemon(
                nativeArgs.Length, 
                nativeArgs.Pointer,
                nativeWebFolder.Pointer,
                nativeSessionFolder.Pointer);
        }
    }
}