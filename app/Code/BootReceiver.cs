using System;

using Android.App;
using Android.Content;

namespace TransmissionAndroid.Code
{
    [BroadcastReceiver(Enabled = true, Exported = false, DirectBootAware = true)]
    [IntentFilter(new[] { Intent.ActionBootCompleted }, Priority = (int)IntentFilterPriority.HighPriority)]
    public class BootReceiver : BroadcastReceiver
    {
        public override void OnReceive(Context context, Intent intent)
        {
            try
            {
                context.StartService<TransmissionService>();
            }
            catch (Exception ex)
            {
                context.ShowTextLong($"Error. {ex.Message}");
            }
        }
    }
}