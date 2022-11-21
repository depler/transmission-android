using System;

using Android.App;
using Android.OS;

namespace TransmissionAndroid.Code
{
    [Activity(Label = "@string/app_name", Theme = "@android:style/Theme.Translucent.NoTitleBar", MainLauncher = true)]
    public class MainActivity : Activity
    {
        protected override void OnCreate(Bundle savedInstanceState)
        {
            base.OnCreate(savedInstanceState);

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