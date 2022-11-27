using System;

using Android.App;
using Android.OS;

namespace TransmissionAndroid.Code
{
    [Activity(Name = "transmission.MainActivity", Label = "@string/app_name", Theme = "@android:style/Theme.Translucent.NoTitleBar", MainLauncher = true)]
    [MetaData("android.app.shortcuts", Resource = "@xml/shortcuts")]
    public class MainActivity : Activity
    {
        protected override async void OnCreate(Bundle savedInstanceState)
        {
            base.OnCreate(savedInstanceState);

            if (await ContextAction.TryHandle(this, Intent?.Data?.LastPathSegment))
                return;

            try
            {
                this.StartService<TransmissionService>();
                FinishAffinity();
            }
            catch (Exception ex)
            {
                this.ShowTextLong(ex.Message);
            }
        }
    }
}