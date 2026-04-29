using System;
using System.Linq;

namespace Bolt;
public enum FilterOption { Digits, Letters, Alphanumeric };

public static class TextUtils
{
    public const string Digits = "0123456789";
    public const string LettersUppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    public const string LettersLowercase = "abcdefghijklmnopqrstuvwxyz";
    public const string Letters = LettersLowercase + LettersUppercase;

    public const char Space = ' ';

    public static string RemoveChars(string input, params char[] nonAllowedChars)
    {
        return new string(input.Where(c => !nonAllowedChars.Contains(c)).ToArray());
    }
    public static string KeepOnly(string input, params char[] allowedChars)
    {
        return new string(input.Where(c => allowedChars.Contains(c)).ToArray());
    }
    public static char[] GetFilterCharset(FilterOption filterOption, bool useSpace = true)
    {
        string allowedChars = filterOption == FilterOption.Digits ? Digits : (filterOption == FilterOption.Letters ? Letters : (Digits + Letters));
        allowedChars += useSpace ? Space : "";
        return allowedChars.ToArray();
    }

    public static string ToHex(int value) => $"#{value:X2}";
    public static string ToHex(long value) => $"#{value:X2}";
    public static string ToHex(params int[] values)
    {
        string result = "#";
        foreach (var value in values)
        {
            result += $"{value:X2}";
        }
        return result;
    }
    public static string ToHex(params long[] values)
    {
        string result = string.Empty;
        foreach (var l in values)
        {
            result += ToHex(l);
        }
        return result;
    }


    public static int WordsCount(string s) => s.Split(' ').Length;


    public static string WrapWith(string s, string beginMark, string endMark)
    {
        s.Insert(0, beginMark);
        s += endMark;
        return s;
    }
    public static string WrapWith(string s, string mark)
    {
        s.Insert(0, mark.ToString());
        s += mark;
        return s;
    }

    public static string WrapWith(string s, char beginMark, char endMark)
    {
        s.Insert(0, beginMark.ToString());
        s += endMark;
        return s;
    }
    public static string WrapWith(string s, char mark)
    {
        s.Insert(0, mark.ToString());
        s += mark;
        return s;
    }


    public static string ToColoredString(string text, string hexColor) 
    {
        return $"<color=#{hexColor}>{text}</color>";
    }

    public static int DifferentCharsCount(string s)
    {
        if (string.IsNullOrEmpty(s))
            return 0;

        return s.Distinct().Count();
    }

    public static string ArrayToString<T>(T[] arr)
    {
        string text = "[";
        for (int i = 0; i < arr.Length; i++)
        {
            if (i == 0)
            {
                text += arr[i];
            }
            else
            {
                text += ", " + arr[i];
            }
        }

        return text + "]";
    }

    public static string Capitalize(string input)
    {
        if (string.IsNullOrEmpty(input))
            return input;

        return char.ToUpper(input[0]) + input.Substring(1).ToLower();
    }
}
