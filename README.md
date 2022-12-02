# Transmission daemon for Android

This project is based on Windows version: https://github.com/depler/transmission-vs. Some code components may slightly differ.

# Build
Clone current repository and build **TransmissionAndroid.sln** file with Visual Studio 2022 or newer. No, additional black magic is not required. Yes, just that simple. All components, such as third-party modules and SSL library, are included as raw source code files.

# Features
- latest Transmission source code (currently v4.00 beta)
- full control over transmission start arguments
- full control over `settings.json` file and other data
- ability to use custom Web UI
- autostart on device boot
- compatible with Android 5.0 or newer versions
- root is not required

# Config
Application uses path `[external_storage]/Transmission` as root folder. Permission `MANAGE_EXTERNAL_STORAGE` is mandatory to use this folder, you need to grant access manually in your device settings. Root folder contains subfolders `Config` (application settings and service data) and `Web` (web UI files). Some settings will be calculated in rutime at first launch. 

Probably you want/should change following settings in file `[external_storage]/Transmission/Config/settings.json`: `download-dir`, `incomplete-dir`. Default folder for torrents is `[external_storage]/Transmission/Torrents`. 

# Web UI
Web UI files are not included in package. You can put your own files into `[app_folder]/web` folder (for example https://github.com/6c65726f79/Transmissionic). Then open http://localhost:9091 and you should see UI:

![image](https://user-images.githubusercontent.com/13541699/203150571-73a94e67-7110-4c18-b77a-02f465b02695.png)

