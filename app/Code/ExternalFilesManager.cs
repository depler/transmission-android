using System.Text;

using Android.Content;
using Android.Content.Res;

namespace TransmissionAndroid.Code
{
    public class ExternalFilesManager
    {
        private static readonly Encoding _utf8 = new UTF8Encoding(false);

        private readonly AssetManager _assetManager;
        private readonly string _externalFilesFolder;
        private readonly string _externalStorageFolder;

        public ExternalFilesManager(ContextWrapper context)
        {
            _assetManager = context.Assets;
            _externalFilesFolder = context.GetExternalFilesDir(null).AbsolutePath;
            _externalStorageFolder = Android.OS.Environment.ExternalStorageDirectory.Path;            
        }

        public string CombinePath(string relativePath)
        {
            return System.IO.Path.Combine(_externalFilesFolder, relativePath);
        }

        public string CombinePath(string relativePath1, string relativePath2)
        {
            return System.IO.Path.Combine(_externalFilesFolder, relativePath1, relativePath2);
        }

        public bool TryCreateFolder(string relativePath)
        {
            var path = CombinePath(relativePath);
            if (System.IO.Directory.Exists(path))
                return false;

            System.IO.Directory.CreateDirectory(path);
            return true;
        }

        public bool TryClearFolder(string relativePath)
        {
            var path = CombinePath(relativePath);
            if (!System.IO.Directory.Exists(path))
                return false;

            foreach (var file in System.IO.Directory.GetFiles(path))
                System.IO.File.Delete(file);

            return true;
        }

        public string[] ReadFileLines(string relativePath)
        {
            var path = CombinePath(relativePath);
            return System.IO.File.ReadAllLines(path, _utf8);
        }

        public void WriteFile(string assetPath)
        {
            var destinationPath = CombinePath(assetPath);

            using var assetStream = _assetManager.Open(assetPath);
            using var fileStream = System.IO.File.OpenWrite(destinationPath);

            assetStream.CopyTo(fileStream);
        }

        public void WriteConfigFile(string assetPath)
        {
            var torrentsFolder = System.IO.Path.Combine(_externalStorageFolder, "Torrents");
            var content = ReadAssetString(assetPath).Replace("TORRENT_FOLDER_PATH", torrentsFolder);

            var destinationPath = CombinePath(assetPath);
            System.IO.File.WriteAllText(destinationPath, content, _utf8);
        }

        public void WriteArgsFile(string assetPath)
        {
            var configRelativePath = System.IO.Path.GetDirectoryName(assetPath);
            var configFolder = CombinePath(configRelativePath);
            var logFile = CombinePath(configRelativePath, "log.txt");

            var content = ReadAssetString(assetPath)
                .Replace("CONFIG_FOLDER_PATH", configFolder)
                .Replace("LOG_FILE_PATH", logFile);

            var destinationPath = CombinePath(assetPath);
            System.IO.File.WriteAllText(destinationPath, content, _utf8);
        }

        private string ReadAssetString(string assetPath)
        {
            using var stream = _assetManager.Open(assetPath);
            using var reader = new System.IO.StreamReader(stream, _utf8);
            return reader.ReadToEnd();
        }
    }
}