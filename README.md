# Transmission daemon for Android

This project is based on Windows version: https://github.com/depler/transmission-vs. Some code components may slightly differ.

# Build
Clone current repository and build **TransmissionAndroid.sln** file with Visual Studio 2022 or newer. No, additional black magic is not required. Yes, just that simple. All components, such as third-party modules and SSL library, are included as raw source code files.

# Features
- latest Transmission source code (currently v4.00 beta)
- full control over transmission start arguments
- full control over `settings.json` file and other data
- ability to use custom web UI
- autostart on device boot

# Config
Application uses path `/storage/emulated/0/Android/data/com.depler.transmission/files` as main folder. It contains subfolders `config` (application settings and service data) and `web` (web UI files, not included in package). Some settings will be calculated in rutime at first launch. Probably you want/should change following settings in file `[app_folder]/config/settings.json`: `download-dir`, `incomplete-dir`. 

Default folder for torrents is `[external_storage]/Torrents`. Permission `MANAGE_EXTERNAL_STORAGE` is mandatory to use this folder, you need to grant access manually in your device settings.
