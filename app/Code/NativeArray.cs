using System;
using System.Runtime.InteropServices;
using System.Text;

namespace TransmissionAndroid.Code
{
    public class NativeArray : IDisposable
    {
        public IntPtr Pointer { get; private set; }
        public int Length { get; private set; }

        public NativeArray(string[] values)
        {
            Pointer = Marshal.AllocHGlobal(values.Length * IntPtr.Size);
            Length = values.Length;

            for (int i = 0; i < Length; i++)
            {
                var data = Encoding.UTF8.GetBytes(values[i]);
                var ptr = Marshal.AllocHGlobal(data.Length + 1);

                Marshal.Copy(data, 0, ptr, data.Length);
                Marshal.WriteByte(ptr, data.Length, 0);
                Marshal.WriteIntPtr(Pointer, i * IntPtr.Size, ptr);
            }
        }

        public void Dispose()
        {
            if (Pointer != IntPtr.Zero)
            {
                for (int i = 0; i < Length; i++)
                {
                    var ptr = Marshal.ReadIntPtr(Pointer, i * IntPtr.Size);
                    if (ptr != IntPtr.Zero)
                        Marshal.FreeHGlobal(ptr);
                }

                Marshal.FreeHGlobal(Pointer);
                Pointer = IntPtr.Zero;
            }
        }
    }
}