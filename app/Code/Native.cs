using System;
using System.Runtime.InteropServices;

namespace TransmissionAndroid.Code
{
    public static class Native
    {
        public const string libtransmission = "libtransmission.so";

        [DllImport(libtransmission)]
        public static extern void AbortProcess();

        [DllImport(libtransmission)]
        public static extern TransmissionStatus GetTransmissionStatus();

        [DllImport(libtransmission)]
        private static extern TransmissionStatus StartTransmission(int argc, IntPtr argv, IntPtr web_folder, IntPtr session_folder);

        public static TransmissionStatus StartTransmission(TransmissionConfig config)
        {
            using var nativeArgs = new NativeArray(config.Args);
            using var nativeWebFolder = new NativeString(config.WebFolder);
            using var nativeSessionFolder = new NativeString(config.SessionFolder);

            return StartTransmission(
                nativeArgs.Length, 
                nativeArgs.Pointer,
                nativeWebFolder.Pointer,
                nativeSessionFolder.Pointer);
        }
    }
}