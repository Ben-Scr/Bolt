using System.IO;
using System.Security.Cryptography;
using System.Text.Json;

namespace Bolt.Serialization
{
    public static class JsonSecure
    {
        private const int saltSize = 16;
        private const int ivSize = 16;
        private const int keySize = 32;
        private const int iterations = 100_000;

        public static void Save<T>(string path, string password, T obj, JsonSerializerOptions? options = null)
        {
            if (!string.IsNullOrEmpty(path)) throw new System.ArgumentException("path can't be null or empty");

            string dirPath = Path.GetDirectoryName(path) ??  "";

            if (!Directory.Exists(dirPath))
                Directory.CreateDirectory(dirPath);

            using FileStream fs = new FileStream(
                path,
                FileMode.Create,
                FileAccess.Write,
                FileShare.None,
                bufferSize: 1 << 20,
                options: FileOptions.SequentialScan);

            options ??= Json.DefaultJson;

            byte[] salt = RandomNumberGenerator.GetBytes(saltSize);
            byte[] iv = RandomNumberGenerator.GetBytes(ivSize);

            using var kdf = new Rfc2898DeriveBytes(password, salt, iterations, HashAlgorithmName.SHA256);
            byte[] key = kdf.GetBytes(keySize);

            fs.Write(salt);
            fs.Write(iv);

            using Aes aes = Aes.Create()!;
            aes.KeySize = keySize * 8;
            aes.BlockSize = ivSize * 8;
            aes.Mode = CipherMode.CBC;
            aes.Padding = PaddingMode.PKCS7;
            aes.Key = key;
            aes.IV = iv;


            using var crypto = new CryptoStream(fs, aes.CreateEncryptor(), CryptoStreamMode.Write);
            JsonSerializer.Serialize(crypto, obj, options);
        }
        public static T Load<T>(string path, string password, T defaultValue = default!, JsonSerializerOptions? options = null)
        {
            if (!File.Exists(path))
                return defaultValue;

            try
            {
                using FileStream fs = new FileStream(
                    path,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.Read,
                    bufferSize: 1 << 20,
                    options: FileOptions.SequentialScan);

                byte[] salt = new byte[saltSize];
                byte[] iv = new byte[ivSize];
                if (fs.Read(salt) != saltSize || fs.Read(iv) != ivSize)
                    throw new InvalidDataException("Invalid fileformat");

                options ??= Json.DefaultJson;

                using var kdf = new Rfc2898DeriveBytes(password, salt, iterations, HashAlgorithmName.SHA256);
                byte[] key = kdf.GetBytes(keySize);

                using Aes aes = Aes.Create()!;
                aes.KeySize = keySize * 8;
                aes.BlockSize = ivSize * 8;
                aes.Mode = CipherMode.CBC;
                aes.Padding = PaddingMode.PKCS7;
                aes.Key = key;
                aes.IV = iv;

                using var crypto = new CryptoStream(fs, aes.CreateDecryptor(), CryptoStreamMode.Read);

                try
                {
                    return JsonSerializer.Deserialize<T>(crypto, options)!;
                }
                catch
                {
                    return defaultValue;
                }
            }
            catch
            {
                throw new InvalidDataException("Wrong password or damaged file");
            }
        }
    }
}
