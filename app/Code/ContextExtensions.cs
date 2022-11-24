using Android.App;
using Android.Content;
using Android.OS;
using Android.Widget;

namespace TransmissionAndroid.Code
{
    public static class ContextExtensions
    {
        public static void ShowTextLong(this Context context, string text)
        {
            Toast.MakeText(context, text, ToastLength.Long).Show();
        }

        public static void StartService<T>(this Context context) where T: Service
        {
            var intent = new Intent(context, typeof(T));
            intent.AddFlags(ActivityFlags.NewTask);

            if (Build.VERSION.SdkInt >= BuildVersionCodes.O)
                context.StartForegroundService(intent);
            else
                context.StartService(intent);
        }

        public static void StopService<T>(this Context context) where T : Service
        {
            var intent = new Intent(context, typeof(T));
            context.StopService(intent);
        }

        public static string CreateNotificationChannel(this Context context)
        {
            var notificationChannel = context.Resources.GetString(Resource.String.app_name);

            if (Build.VERSION.SdkInt >= BuildVersionCodes.O)
            {
                var channel = new NotificationChannel(notificationChannel, notificationChannel, NotificationImportance.Default);
                var manager = (NotificationManager)context.GetSystemService(Context.NotificationService);
                manager.CreateNotificationChannel(channel);
            }

            return notificationChannel;
        }

        public static void TryCloseSystemDialogs(this Context context)
        {
            if (Build.VERSION.SdkInt < BuildVersionCodes.S)
            {
#pragma warning disable CS0618
                var closeDialogsIntent = new Intent(Intent.ActionCloseSystemDialogs);
#pragma warning restore CS0618
                context.SendBroadcast(closeDialogsIntent);
            }
        }
    }
}