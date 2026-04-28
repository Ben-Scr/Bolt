using System;
using System.IO;
using System.Security.Cryptography;

namespace Bolt.Cryptography
{
    public enum KeySize
    {
        Bits128 = 128,
        Bits192 = 192,
        Bits256 = 256
    }

    public static class AesHelper
    {
        public static byte[] GenerateKey(KeySize keySize)
            => RandomNumberGenerator.GetBytes((int)keySize / 8);

        public static byte[] EncryptBytes(byte[] bytes, byte[] key, byte[] iv)
        {
            using var aes = Aes.Create();
            aes.Key = key;
            aes.IV = iv;

            using var encryptor = aes.CreateEncryptor();
            using var ms = new MemoryStream();
            using (var cs = new CryptoStream(ms, encryptor, CryptoStreamMode.Write))
            {
                cs.Write(bytes, 0, bytes.Length);
                cs.FlushFinalBlock();
            }
            return ms.ToArray();
        }

        public static byte[] DecryptBytes(byte[] bytes, byte[] key, byte[] iv)
        {
            using var aes = Aes.Create();
            aes.Key = key;
            aes.IV = iv;

            using var decryptor = aes.CreateDecryptor();
            using var input = new MemoryStream(bytes);
            using var cs = new CryptoStream(input, decryptor, CryptoStreamMode.Read);
            using var output = new MemoryStream();

            cs.CopyTo(output);
            return output.ToArray();
        }

        // INFO(Ben-Scr):
        // Encrypts the bytes with the IV packed in: iv(16) | bytes(x)
        public static byte[] EncryptBytes(byte[] bytes, byte[] key)
        {
            using var aes = Aes.Create();
            aes.Key = key;
            aes.IV = RandomNumberGenerator.GetBytes(16);

            using var encryptor = aes.CreateEncryptor();
            using var ms = new MemoryStream();

            ms.Write(aes.IV, 0, aes.IV.Length);

            using (var cs = new CryptoStream(ms, encryptor, CryptoStreamMode.Write))
            {
                cs.Write(bytes, 0, bytes.Length);
                cs.FlushFinalBlock();
            }

            return ms.ToArray();
        }

        // INFO(Ben-Scr):
        // Decrypts the bytes unpacking the IV that is packed in: iv(16) | bytes(x)
        public static byte[] DecryptBytes(byte[] bytes, byte[] key)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            if (key == null) throw new ArgumentNullException(nameof(key));
            if (bytes.Length < 16 + 1)
                throw new CryptographicException("Input too short.");

            using var aes = Aes.Create();
            aes.Key = key;

            byte[] iv = new byte[16];
            Buffer.BlockCopy(bytes, 0, iv, 0, 16);
            aes.IV = iv;

            using var decryptor = aes.CreateDecryptor();
            using var input = new MemoryStream(bytes, 16, bytes.Length - 16);
            using var cs = new CryptoStream(input, decryptor, CryptoStreamMode.Read);
            using var output = new MemoryStream();

            cs.CopyTo(output);
            return output.ToArray();
        }
    }
}