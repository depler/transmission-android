using System;
using System.Threading.Tasks;

using Android.Content;

using Xamarin.Essentials;

namespace TransmissionAndroid.Code
{
    public static class ContextAction
    {
        public const string Id = "id";

        public const string Stop = "stop";
        public const string Update = "update";

        public static async Task<bool> TryHandle(Context context, string action)
        {
            try
            {
                switch (action)
                {
                    case Stop:
                        {
                            context.StopService<TransmissionService>();
                            context.TryCloseSystemDialogs();

                            System.Diagnostics.Process.GetCurrentProcess().Kill();
                            return true;
                        }
                    case Update:
                        {
                            await Launcher.OpenAsync("https://github.com/depler/transmission-android/releases");
                            context.TryCloseSystemDialogs();
                            return true;
                        }
                    case null:
                        {
                            return false;
                        }
                    default: throw new Exception($"Unknown action: {action}");
                }
            }
            catch (Exception ex)
            {
                context.ShowTextLong(ex.Message);
                return false;
            }
        }
    }
}