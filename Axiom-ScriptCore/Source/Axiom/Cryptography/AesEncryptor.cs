using System;
using System.Security.Cryptography;

namespace Axiom.Cryptography;
public class AesEncryptor
{
    private AesKey aesKey;


    private bool ValidLength(int length) => length switch
    {
        16 => true,
        24 => true,
        32 => true,
        _ => false
    };

    public AesEncryptor()
    {
        aesKey = new AesKey();
        aesKey.Size = KeySize.Bits256;
        aesKey.Key = AesHelper.GenerateKey(aesKey.Size);
    }
    public AesEncryptor(KeySize keySize)
    {
        aesKey = new AesKey();
        aesKey.Size = keySize;
        aesKey.Key = AesHelper.GenerateKey(aesKey.Size);
    }
    public AesEncryptor(byte[] key)
    {
        if (!ValidLength(key.Length)) throw new ArgumentException("The length of the key can only be 16, 24 or 32 items long");

        aesKey = new AesKey();
        aesKey.Size = (KeySize)(key.Length * 8);
        aesKey.Key = key;
    }


    public void SetKeySize(KeySize keySize)
    {
        if (aesKey.Size != keySize)
        {
            aesKey.Size = keySize;
            RegenerateKey();
        }
    }

    public void RegenerateKey()
    {
        aesKey.Key = AesHelper.GenerateKey(aesKey.Size);
    }

    public void SetKey(byte[] key)
    {
        int requiredSize = (int)aesKey.Size / 8;
        if (requiredSize != key.Length) throw new ArgumentException($"Key length does not match the required length of {requiredSize}");

        aesKey.Key = key;
    }
    public byte[]? GetKey()
    {
        return aesKey.Key;
    }

    public byte[] Encrypt(byte[] bytes)
    {
        return AesHelper.EncryptBytes(bytes, aesKey.Key ?? new byte[0]);
    }
    public byte[] Decrypt(byte[] bytes)
    {
        return AesHelper.DecryptBytes(bytes, aesKey.Key ?? new byte[0]);
    }
}
