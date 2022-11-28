using System;
using System.IO;
using System.Threading;

using Android.App;
using Android.Content;
using Android.Graphics;
using Android.OS;

using AndroidX.Core.App;

namespace TransmissionAndroid.Code
{
    [Service]
    public class TransmissionService : Service
    {
        public static IntPtr daemonPtr = IntPtr.Zero;
        public static Thread daemonThread = null;

        public override IBinder OnBind(Intent intent)
        {
            return null;
        }

        public override StartCommandResult OnStartCommand(Intent intent, StartCommandFlags flags, int startId)
        {
            try
            {
                if (daemonPtr == IntPtr.Zero)
                {
                    var config = ConfigureTransmission();

                    daemonPtr = Native.InitDaemon(config);
                    if (daemonPtr == IntPtr.Zero)
                        throw new Exception($"Transmission failed to initialize");

                    daemonThread = new Thread(() => Native.StartDaemon(daemonPtr, true));
                    daemonThread.Start();

                    SetNotification("Transmission is running", string.Join('\n', config.Args));
                    this.ShowTextLong($"Transmission started");
                }
                else
                {
                    this.ShowTextLong($"Transmission is running");
                }

                return StartCommandResult.Sticky;
            }
            catch (Exception ex)
            {
                if (ex is IOException || ex is UnauthorizedAccessException)
                    this.ShowTextLong("Transmission failed. Check storage permissions.");

                this.ShowTextLong(ex.Message);
                throw;
            }
        }

        private TransmissionConfig ConfigureTransmission()
        {
            var filesManager = new ExternalFilesManager(this);
            if (filesManager.TryCreateFolder("Config"))
            {
                filesManager.WriteConfigFile("Config/settings.json");
                filesManager.WriteArgsFile("Config/args.txt");
                filesManager.WriteFile("Config/dht.bootstrap");
            }

            filesManager.TryCreateFolder("Web");
            filesManager.TryCreateFolder("Session");
            filesManager.TryClearFolder("Session");

            return new TransmissionConfig()
            {
                Args = filesManager.ReadFileLines("Config/args.txt"),
                WebFolder = filesManager.CombinePath("Web"),
                SessionFolder = filesManager.CombinePath("Session"),
            };
        }

        private void SetNotification(string title, string text)
        {
            var notificationChannel = this.CreateNotificationChannel();

            var contentBigText = new NotificationCompat.BigTextStyle().BigText(text);

            var actionExit = NotificationActionReceiver.CreateAction(this, ContextAction.Stop, Color.DarkRed);
            var actionConfig = NotificationActionReceiver.CreateAction(this, ContextAction.Update, Color.DarkBlue);

            var notification = new NotificationCompat.Builder(this, notificationChannel)
                .SetContentTitle(title)
                .SetContentText(text)
                .SetStyle(contentBigText)
                .SetSmallIcon(Resource.Mipmap.ic_notification)
                .SetAutoCancel(false)
                .SetOngoing(true)
                .SetShowWhen(false)
                .AddAction(actionExit)
                .AddAction(actionConfig)
                .Build();

            StartForeground(1, notification);
        }
    }
}