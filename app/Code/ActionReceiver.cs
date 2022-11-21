using System;

using Android.Content;

namespace TransmissionAndroid.Code
{
    [BroadcastReceiver(Enabled = true, Exported = false)]
    public class ActionReceiver : BroadcastReceiver
    {
        public override void OnReceive(Context context, Intent intent)
        {
            try
            {
                var id = intent.GetStringExtra("id").ToLowerInvariant();

                switch (id)
                {
                    case "exit":
                        {
                            context.StopService<TransmissionService>();
                            Native.AbortProcess();
                            break;
                        }
                }
            }
            catch (Exception ex)
            {
                context.ShowTextLong(ex.Message);
            }
        }
    }
}
