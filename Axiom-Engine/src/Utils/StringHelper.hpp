#pragma once
#include <string>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Axiom {
	class AXIOM_API StringHelper {
	public:
		static std::string WrapWith(std::string_view s, char mark) {
			std::string out;
			out.reserve(s.size() + 2);
			out.push_back(mark);
			out.append(s);
			out.push_back(mark);
			return out;
		}

		static std::string WrapWith(std::string_view s, char markBegin, char markEnd) {
			std::string out;
			out.reserve(s.size() + 2);
			out.push_back(markBegin);
			out.append(s);
			out.push_back(markEnd);
			return out;
		}

		static std::string WrapWith(std::string_view s, std::string_view mark) {
			std::string result;
			result.reserve(s.size() + (mark.size() * 2));
			result += mark;
			result += s;
			result += mark;
			return result;
		}

		static std::string WrapWith(std::string_view s, std::string_view markBegin, std::string_view markEnd) {
			std::string result;
			result.reserve(s.size() + (markBegin.size() + markEnd.size()));
			result += markBegin;
			result += s;
			result += markEnd;
			return result;
		}


		template <typename... Args>
		static std::string ToString(Args&&... args) {
			std::ostringstream oss;
			((oss << std::forward<Args>(args)), ...);
			return oss.str();
		}

		static std::string ToLower(const std::string& str) {
			std::string result = str;
			std::transform(result.begin(), result.end(), result.begin(),
				[](unsigned char c) { return std::tolower(c); });
			return result;
		}
		static std::string ToUpper(const std::string& str) {
			std::string result = str;
			std::transform(result.begin(), result.end(), result.begin(),
				[](unsigned char c) { return std::toupper(c); });
			return result;
		}

		static bool EndsWith(const std::string& str, const std::string& suffix) {
			if (suffix.size() > str.size()) return false;
			return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
		}
		static bool StartsWith(const std::string& str, const std::string& prefix) {
			if (prefix.size() > str.size()) return false;
			return std::equal(prefix.begin(), prefix.end(), str.begin());
		}

		static std::string Replace(const std::string& str, const std::string& from, const std::string& to) {
			if (from.empty()) {
				return str;
			}

			std::string result = str;
			std::size_t pos = 0;
			while ((pos = result.find(from, pos)) != std::string::npos) {
				result.replace(pos, from.length(), to);
				pos += to.length();
			}

			return result;
		}

		static std::string Trim(const std::string& str) {
			const std::string whitespace = " \n\r\t\f\v";
			size_t start = str.find_first_not_of(whitespace);
			if (start == std::string::npos)
				return ""; // Info: All whitespace
			size_t end = str.find_last_not_of(whitespace);
			return str.substr(start, end - start + 1);
		}


		static std::string ToIEC(std::size_t size)
		{
			const double s = static_cast<double>(size);
			constexpr double k = 1024.0;
			constexpr double m = k * k;
			constexpr double g = m * k;
			constexpr double t = g * k;

			if (s < k) return ToString(static_cast<std::size_t>(s), " B");
			if (s < m) return FormatWithPrecision(s / k, " KiB");
			if (s < g) return FormatWithPrecision(s / m, " MiB");
			if (s < t) return FormatWithPrecision(s / g, " GiB");
			return FormatWithPrecision(s / t, " TiB");
		}

		static std::string ToSI(std::size_t size) {
			const double s = static_cast<double>(size);

			if (s < 1000.0) return ToString(static_cast<std::size_t>(s), " B");
			if (s < 1e6)    return FormatWithPrecision(s / 1e3, " kB");
			if (s < 1e9)    return FormatWithPrecision(s / 1e6, " MB");
			if (s < 1e12)   return FormatWithPrecision(s / 1e9, " GB");
			return FormatWithPrecision(s / 1e12, " TB");
		}

		static std::string Remove(std::string s, size_t start, size_t count)
		{
			if (start >= s.size() || count == 0) return s;
			count = std::min(count, s.size() - start);
			s.erase(start, count);
			return s;
		}

		//static bool IsDigit(const std::string& str) {
		//	return !str.empty() && std::all_of(str.begin(), str.end(), std::isdigit);
		//}
		//static bool IsAlpha(const std::string& str) {
		//	return !str.empty() && std::all_of(str.begin(), str.end(), std::isalpha);
		//}
	private:
		static std::string FormatWithPrecision(double value, std::string_view unit) {
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(2) << value << unit;
			return oss.str();
		}
	};
}