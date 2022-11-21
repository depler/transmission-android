using Android.App;
using Android.Content;
using Android.OS;
using Android.Widget;

using AndroidX.Core.App;

namespace TransmissionAndroid.Code
{
    public static class ContextExtensions
    {
        public static void ShowTextLong(this Context context, string text)
        {
            using var toast = Toast.MakeText(context, text, ToastLength.Long);
            toast.Show();
        }

        public static void StartService<T>(this Context context) where T: Service
        {
            using var intent = new Intent(context, typeof(T));
            intent.AddFlags(ActivityFlags.NewTask);

            if (Build.VERSION.SdkInt >= BuildVersionCodes.O)
                context.StartForegroundService(intent);
            else
                context.StartService(intent);
        }

        public static void StopService<T>(this Context context) where T : Service
        {
            using var intent = new Intent(context, typeof(T));
            context.StopService(intent);
        }

        public static string CreateNotificationChannel(this Context context)
        {
            var notificationChannel = context.Resources.GetString(Resource.String.app_name);

            if (Build.VERSION.SdkInt >= BuildVersionCodes.O)
            {
                using var channel = new NotificationChannel(notificationChannel, notificationChannel, NotificationImportance.Default);
                using var manager = (NotificationManager)context.GetSystemService(Context.NotificationService);
                manager.CreateNotificationChannel(channel);
            }

            return notificationChannel;
        }

        public static NotificationCompat.Action CreateNotificationAction<T>(this Context context, string text, string id = null) where T: BroadcastReceiver
        {
            using var actionIntent = new Intent(context, typeof(T));
            actionIntent.PutExtra("id", id ?? text);

            using var pIntent = PendingIntent.GetBroadcast(context, 0, actionIntent, PendingIntentFlags.CancelCurrent);
            return new NotificationCompat.Action(-1, text, pIntent);
        }
    }
}