using System;
using System.Runtime.InteropServices;
using System.Text;

namespace TransmissionAndroid.Code
{
    public class NativeString : IDisposable
    {
        public IntPtr Pointer { get; private set; }

        public NativeString(string value)
        {
            var data = Encoding.UTF8.GetBytes(value);
            Pointer = Marshal.AllocHGlobal(data.Length + 1);

            Marshal.Copy(data, 0, Pointer, data.Length);
            Marshal.WriteByte(Pointer, data.Length, 0);
        }

        public void Dispose()
        {
            if (Pointer != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(Pointer);
                Pointer = IntPtr.Zero;
            }
        }
    }
}