using System;
using System.Security.Cryptography;
using System.Text;

namespace Axiom.Cryptography;
public static class Encryptor
{
    public static string EncryptString(string input, string password, KeySize keySize = KeySize.Bits256)
    {
        if (input is null) throw new ArgumentNullException(nameof(input));
        if (password is null) throw new ArgumentNullException(nameof(password));

        byte[] salt = RandomNumberGenerator.GetBytes(16);
        byte[] iv = RandomNumberGenerator.GetBytes(16);

        int keySizeBytes = (int)keySize / 8;
        byte[] key = DeriveKeyFromPassword(password, salt, keySizeBytes);

        byte[] ciphertext = AesHelper.EncryptBytes(Encoding.UTF8.GetBytes(input), key, iv);

        // INFO(Ben-Scr): Pack Salt, IV and the Key size into the data: salt(16) || iv(16) || keySize(1) || ciphertext
        byte[] packed = new byte[33 + ciphertext.Length];
        Buffer.BlockCopy(salt, 0, packed, 0, 16);
        Buffer.BlockCopy(iv, 0, packed, 16, 16);
        packed[32] = (byte)((int)keySize / 8);

        Buffer.BlockCopy(ciphertext, 0, packed, 33, ciphertext.Length);

        return Convert.ToBase64String(packed);
    }

    public static string DecryptString(string encryptedBase64, string password)
    {
        if (encryptedBase64 is null) throw new ArgumentNullException(nameof(encryptedBase64));
        if (password is null) throw new ArgumentNullException(nameof(password));

        byte[] packed = Convert.FromBase64String(encryptedBase64);

        if (packed.Length < 33 + 1)
            throw new CryptographicException("Ciphertext is too short or invalid.");

        // INFO(Ben-Scr): Unpack Salt and IV from the data: salt(16) || iv(16)
        byte[] salt = new byte[16];
        byte[] iv = new byte[16];

        Buffer.BlockCopy(packed, 0, salt, 0, 16);
        Buffer.BlockCopy(packed, 16, iv, 0, 16);

        byte keySizeBytes = packed[32];
        byte[] ciphertext = new byte[packed.Length - 33];
        Buffer.BlockCopy(packed, 33, ciphertext, 0, ciphertext.Length);

        byte[] key = DeriveKeyFromPassword(password, salt, keySizeBytes);

        try
        {
            byte[] plain = AesHelper.DecryptBytes(ciphertext, key, iv);
            return Encoding.UTF8.GetString(plain);
        }
        catch
        {
            throw new CryptographicException("Either the password is invalid or something else went wrong");
        }
    }

    private static byte[] DeriveKeyFromPassword(string password, byte[] salt, int keySizeBytes)
    {
        const int iterations = 100_000;
        return Rfc2898DeriveBytes.Pbkdf2(Encoding.UTF8.GetBytes(password), salt, iterations, HashAlgorithmName.SHA256, keySizeBytes);
    }
}
