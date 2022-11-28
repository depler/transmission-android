using System;
using System.IO;
using System.Text;
using System.Threading;

using Android.Content;
using Android.Content.Res;

namespace TransmissionAndroid.Code
{
    public class ExternalFilesManager
    {
        private static readonly Encoding _utf8 = new UTF8Encoding(false);

        private readonly AssetManager _assetManager;
        private readonly Lazy<string> _transmissionFolder;

        public ExternalFilesManager(Context context)
        {
            _assetManager = context.Assets;
            _transmissionFolder = new Lazy<string>(() =>
            {
                var externalStorageFolder = Android.OS.Environment.ExternalStorageDirectory.Path;
                return Path.Combine(externalStorageFolder, "Transmission");
            }, LazyThreadSafetyMode.None);
        }

        public string CombinePath(string relativePath)
        {
            return Path.Combine(_transmissionFolder.Value, relativePath);
        }

        public string CombinePath(string relativePath1, string relativePath2)
        {
            return Path.Combine(_transmissionFolder.Value, relativePath1, relativePath2);
        }

        public bool TryCreateFolder(string relativePath)
        {
            var path = CombinePath(relativePath);
            if (Directory.Exists(path))
                return false;

            Directory.CreateDirectory(path);
            return true;
        }

        public bool TryClearFolder(string relativePath)
        {
            var path = CombinePath(relativePath);
            if (!Directory.Exists(path))
                return false;

            foreach (var file in Directory.GetFiles(path))
                File.Delete(file);

            return true;
        }

        public string[] ReadFileLines(string relativePath)
        {
            var path = CombinePath(relativePath);
            return File.ReadAllLines(path, _utf8);
        }

        public void WriteFile(string assetPath)
        {
            var destinationPath = CombinePath(assetPath);

            using var assetStream = _assetManager.Open(assetPath);
            using var fileStream = File.OpenWrite(destinationPath);

            assetStream.CopyTo(fileStream);
        }

        public void WriteConfigFile(string assetPath)
        {
            var torrentsFolder = Path.Combine(_transmissionFolder.Value, "Torrents");
            var content = ReadAssetString(assetPath).Replace("TORRENT_FOLDER_PATH", torrentsFolder);

            var destinationPath = CombinePath(assetPath);
            File.WriteAllText(destinationPath, content, _utf8);
        }

        public void WriteArgsFile(string assetPath)
        {
            var configRelativePath = Path.GetDirectoryName(assetPath);
            var configFolder = CombinePath(configRelativePath);
            var logFile = CombinePath(configRelativePath, "log.txt");

            var content = ReadAssetString(assetPath)
                .Replace("CONFIG_FOLDER_PATH", configFolder)
                .Replace("LOG_FILE_PATH", logFile);

            var destinationPath = CombinePath(assetPath);
            File.WriteAllText(destinationPath, content, _utf8);
        }

        private string ReadAssetString(string assetPath)
        {
            using var stream = _assetManager.Open(assetPath);
            using var reader = new StreamReader(stream, _utf8);
            return reader.ReadToEnd();
        }
    }
}