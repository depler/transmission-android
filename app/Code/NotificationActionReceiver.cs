using Android.App;
using Android.Content;
using Android.Graphics;
using Android.OS;
using Android.Text;
using Android.Text.Style;

using AndroidX.Core.App;

namespace TransmissionAndroid.Code
{
    [BroadcastReceiver(Enabled = true, Exported = false)]
    public class NotificationActionReceiver : BroadcastReceiver
    {
        private static int ActionCounter = 0;

        public override async void OnReceive(Context context, Intent intent)
        {
            var action = intent.GetStringExtra(ContextAction.Id);
            await ContextAction.TryHandle(context, action);
        }

        public static NotificationCompat.Action CreateAction(Context context, string text, Color color, string id = null)
        {
            var actionIntent = new Intent(context, typeof(NotificationActionReceiver));
            actionIntent.PutExtra(ContextAction.Id, id ?? text);

            var flags = (Build.VERSION.SdkInt >= BuildVersionCodes.S) ?
                PendingIntentFlags.CancelCurrent | PendingIntentFlags.Immutable :
                PendingIntentFlags.CancelCurrent;

            var pIntent = PendingIntent.GetBroadcast(context, ActionCounter++, actionIntent, flags);
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
