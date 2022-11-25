using System;

using Android.App;
using Android.Content;
using Android.OS;
using Android.Text.Style;
using Android.Text;

using AndroidX.Core.App;
using Android.Graphics;
using Xamarin.Essentials;

namespace TransmissionAndroid.Code
{
    [BroadcastReceiver(Enabled = true, Exported = false)]
    public class NotificationActionReceiver : BroadcastReceiver
    {
        private static int ActionCounter = 0;

        public override async void OnReceive(Context context, Intent intent)
        {
            try
            {
                var id = intent.GetStringExtra(NotificationAction.ExtraId);

                switch (id)
                {
                    case NotificationAction.Exit:
                        {
                            context.StopService<TransmissionService>();
                            context.TryCloseSystemDialogs();

                            System.Diagnostics.Process.GetCurrentProcess().Kill();
                            break;
                        }
                    case NotificationAction.Update:
                        {
                            await Launcher.OpenAsync("https://github.com/depler/transmission-android/releases");
                            context.TryCloseSystemDialogs();
                            break;
                        }
                }
            }
            catch (Exception ex)
            {
                context.ShowTextLong(ex.Message);
            }
        }

        public static NotificationCompat.Action CreateAction(Context context, string text, Color color, string id = null)
        {
            var actionIntent = new Intent(context, typeof(NotificationActionReceiver));
            actionIntent.PutExtra(NotificationAction.ExtraId, id ?? text);

            var pIntent = PendingIntent.GetBroadcast(context, ActionCounter++, actionIntent, PendingIntentFlags.CancelCurrent);
            var spanText = GetSpan(text, color);

            return new NotificationCompat.Action(-1, spanText, pIntent);
        }

        private static ISpannable GetSpan(string text, Color color)
        {
            var spannable = new SpannableString(text);

            if (Build.VERSION.SdkInt >= BuildVersionCodes.NMr1)
            {
                var span = new ForegroundColorSpan(color);
                spannable.SetSpan(span, 0, spannable.Length(), 0);
            }

            return spannable;
        }
    }
}
