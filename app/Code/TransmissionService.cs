﻿using System;

using Android.App;
using Android.Content;
using Android.OS;

using AndroidX.Core.App;

namespace TransmissionAndroid.Code
{
    [Service]
    public class TransmissionService : Service
    {
        public override IBinder OnBind(Intent intent)
        {
            return null;
        }

        public override StartCommandResult OnStartCommand(Intent intent, StartCommandFlags flags, int startId)
        {
            try
            {
                if (Native.GetTransmissionStatus() < TransmissionStatus.STARTED)
                {
                    var config = ConfigureTransmission();
                    var status = Native.StartTransmission(config);

                    if (status < TransmissionStatus.STARTED)
                        throw new Exception($"Transmission failed to start");

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
                this.ShowTextLong(ex.Message);
                Native.AbortProcess();
                throw;
            }
        }

        private TransmissionConfig ConfigureTransmission()
        {
            var filesManager = new ExternalFilesManager(this);
            if (filesManager.TryCreateFolder("config"))
            {
                filesManager.WriteConfigFile("config/settings.json");
                filesManager.WriteArgsFile("config/args.txt");
                filesManager.WriteFile("config/dht.bootstrap");
            }

            filesManager.TryCreateFolder("web");
            filesManager.TryCreateFolder("session");
            filesManager.TryClearFolder("session");

            return new TransmissionConfig()
            {
                Args = filesManager.ReadFileLines("config/args.txt"),
                WebFolder = filesManager.CombinePath("web"),
                SessionFolder = filesManager.CombinePath("session"),
            };
        }

        private void SetNotification(string title, string text)
        {
            var notificationChannel = this.CreateNotificationChannel();

            using var contentBigText = new NotificationCompat.BigTextStyle().BigText(text);
            using var actionExit = this.CreateNotificationAction<ActionReceiver>("Exit");

            using var notification = new NotificationCompat.Builder(this, notificationChannel)
                .SetContentTitle(title)
                .SetContentText(text)
                .SetStyle(contentBigText)
                .SetSmallIcon(Resource.Mipmap.ic_notification)
                .SetAutoCancel(false)
                .SetOngoing(true)
                .SetShowWhen(false)
                .AddAction(actionExit)
                .Build();

            StartForeground(1, notification);
        }
    }
}