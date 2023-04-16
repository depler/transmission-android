<a href="https://apt.izzysoft.de/fdroid/index/apk/com.depler.transmission">
<img src="https://img.shields.io/endpoint?url=https://apt.izzysoft.de/fdroid/api/v1/shield/com.depler.transmission" />
</a>

# Transmission daemon for Android

This project is based on Windows version: https://github.com/depler/transmission-vs. Some code components may slightly differ.

# Build
Clone current repository and build **TransmissionAndroid.sln** file with Visual Studio 2022 or newer. No, additional black magic is not required. Yes, just that simple. All components, such as third-party modules and SSL library, are included as raw source code files.

# Features
- latest Transmission source code (currently v4.0.x)
- full control over transmission start arguments
- full control over `settings.json` file and other data
- ability to use custom Web UI
- autostart on device boot
- compatible with Android 5.0 or newer versions
- root is not required

# Config
Application uses path `[external_storage]/Transmission` as root folder. Permission `MANAGE_EXTERNAL_STORAGE` is mandatory to use this folder, you need to grant access manually in your device settings. Root folder contains subfolders `Config` (application settings and service data) and `Web` (web UI files). Some settings will be calculated at runtime during first launch. 

Probably you want/should change following settings in file `[external_storage]/Transmission/Config/settings.json`: `download-dir`, `incomplete-dir`. Default folder for torrents is `[external_storage]/Transmission/Torrents`. 

Also, be aware: **default settings allow any remote user connect to your transmission server without password**. If you plan to use it over public network - then you you need to tweak `settings.json` file to restrict access. See transmission documentation about how to do this.

# UI client
Transmission is a background service, it has no build-in interface. If you see the following screen - then transmission is running just fine.

You need some third-party client for transmission server, for example this one: https://play.google.com/store/apps/details?id=com.sleroy.transmissionic. Just install it and connect to localhost on port 9091. 

![image](https://user-images.githubusercontent.com/13541699/218250684-718abf37-fba2-4921-88d9-92457d6fb993.png)


# Web UI
Web UI files are not included in package. You can put your own files into `[app_folder]/Web` folder (for example https://github.com/6c65726f79/Transmissionic). Then open http://localhost:9091 and you should see UI:

![image](https://user-images.githubusercontent.com/13541699/217871490-69514a56-fe53-4095-89db-8c1aba49f1d3.png)


