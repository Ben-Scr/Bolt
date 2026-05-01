#include "pch.hpp"
#include "Serialization/Json.hpp"

#include <cctype>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace Axiom::Json {

	namespace {
		class Parser {
		public:
			explicit Parser(std::string_view text)
				: m_Text(text) {
			}

			bool ParseValue(Value& outValue, std::string* outError) {
				SkipWhitespace();
				if (!ParseAny(outValue, outError)) {
					return false;
				}

				SkipWhitespace();
				if (!IsAtEnd()) {
					SetError(outError, "Unexpected trailing characters");
					return false;
				}

				return true;
			}

		private:
			bool ParseAny(Value& outValue, std::string* outError) {
				SkipWhitespace();
				if (IsAtEnd()) {
					SetError(outError, "Unexpected end of JSON input");
					return false;
				}

				switch (Peek()) {
				case 'n':
					return ParseLiteral("null", Value(), outValue, outError);
				case 't':
					return ParseLiteral("true", Value(true), outValue, outError);
				case 'f':
					return ParseLiteral("false", Value(false), outValue, outError);
				case '"':
					return ParseStringValue(outValue, outError);
				case '{':
					return ParseObject(outValue, outError);
				case '[':
					return ParseArray(outValue, outError);
				default:
					if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek()))) {
						return ParseNumber(outValue, outError);
					}

					SetError(outError, "Unexpected token while parsing JSON");
					return false;
				}
			}

			bool ParseLiteral(std::string_view literal, Value literalValue, Value& outValue, std::string* outError) {
				if (m_Text.substr(m_Position, literal.size()) != literal) {
					SetError(outError, "Invalid JSON literal");
					return false;
				}

				m_Position += literal.size();
				outValue = std::move(literalValue);
				return true;
			}

			bool ParseStringValue(Value& outValue, std::string* outError) {
				std::string parsed;
				if (!ParseString(parsed, outError)) {
					return false;
				}

				outValue = Value(std::move(parsed));
				return true;
			}

			bool ParseString(std::string& outValue, std::string* outError) {
				if (!Consume('"')) {
					SetError(outError, "Expected opening quote");
					return false;
				}

				std::string result;
				while (!IsAtEnd()) {
					char ch = Advance();
					if (ch == '"') {
						outValue = std::move(result);
						return true;
					}

					if (ch != '\\') {
						result.push_back(ch);
						continue;
					}

					if (IsAtEnd()) {
						SetError(outError, "Invalid escape sequence");
						return false;
					}

					char escaped = Advance();
					switch (escaped) {
					case '"': result.push_back('"'); break;
					case '\\': result.push_back('\\'); break;
					case '/': result.push_back('/'); break;
					case 'b': result.push_back('\b'); break;
					case 'f': result.push_back('\f'); break;
					case 'n': result.push_back('\n'); break;
					case 'r': result.push_back('\r'); break;
					case 't': result.push_back('\t'); break;
					case 'u':
					{
						uint32_t codePoint = 0;
						if (!ParseUnicodeEscape(codePoint, outError)) {
							return false;
						}
						AppendUtf8(result, codePoint);
						break;
					}
					default:
						SetError(outError, "Unsupported escape sequence");
						return false;
					}
				}

				SetError(outError, "Unterminated JSON string");
				return false;
			}

			bool ParseUnicodeEscape(uint32_t& outCodePoint, std::string* outError) {
				if (m_Position + 4 > m_Text.size()) {
					SetError(outError, "Incomplete unicode escape");
					return false;
				}

				uint32_t codePoint = 0;
				for (int i = 0; i < 4; i++) {
					char digit = m_Text[m_Position++];
					codePoint <<= 4;
					if (digit >= '0' && digit <= '9') {
						codePoint |= static_cast<uint32_t>(digit - '0');
					}
					else if (digit >= 'a' && digit <= 'f') {
						codePoint |= static_cast<uint32_t>(digit - 'a' + 10);
					}
					else if (digit >= 'A' && digit <= 'F') {
						codePoint |= static_cast<uint32_t>(digit - 'A' + 10);
					}
					else {
						SetError(outError, "Invalid unicode escape");
						return false;
					}
				}

				outCodePoint = codePoint;
				return true;
			}

			static void AppendUtf8(std::string& out, uint32_t codePoint) {
				if (codePoint <= 0x7F) {
					out.push_back(static_cast<char>(codePoint));
				}
				else if (codePoint <= 0x7FF) {
					out.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
					out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
				else if (codePoint <= 0xFFFF) {
					out.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
					out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
				else {
					out.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
					out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
				}
			}

			bool ParseNumber(Value& outValue, std::string* outError) {
				const size_t start = m_Position;

				if (Peek() == '-') {
					Advance();
				}

				if (IsAtEnd()) {
					SetError(outError, "Invalid JSON number");
					return false;
				}

				if (Peek() == '0') {
					Advance();
				}
				else if (std::isdigit(static_cast<unsigned char>(Peek()))) {
					while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
						Advance();
					}
				}
				else {
					SetError(outError, "Invalid JSON number");
					return false;
				}

				if (!IsAtEnd() && Peek() == '.') {
					Advance();
					if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
						SetError(outError, "Invalid JSON number fraction");
						return false;
					}
					while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
						Advance();
					}
				}

				if (!IsAtEnd() && (Peek() == 'e' || Peek() == 'E')) {
					Advance();
					if (!IsAtEnd() && (Peek() == '+' || Peek() == '-')) {
						Advance();
					}
					if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
						SetError(outError, "Invalid JSON exponent");
						return false;
					}
					while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
						Advance();
					}
				}

				const std::string numericText(m_Text.substr(start, m_Position - start));
				char* endPtr = nullptr;
				const double parsed = std::strtod(numericText.c_str(), &endPtr);
				if (endPtr != numericText.c_str() + numericText.size()) {
					SetError(outError, "Failed to parse JSON number");
					return false;
				}

				outValue = Value(parsed);
				return true;
			}

			bool ParseObject(Value& outValue, std::string* outError) {
				if (!Consume('{')) {
					SetError(outError, "Expected object start");
					return false;
				}

				Value objectValue = Value::MakeObject();
				SkipWhitespace();
				if (Consume('}')) {
					outValue = std::move(objectValue);
					return true;
				}

				while (!IsAtEnd()) {
					std::string key;
					if (!ParseString(key, outError)) {
						return false;
					}

					SkipWhitespace();
					if (!Consume(':')) {
						SetError(outError, "Expected ':' after object key");
						return false;
					}

					Value childValue;
					if (!ParseAny(childValue, outError)) {
						return false;
					}

					objectValue.AddMember(std::move(key), std::move(childValue));

					SkipWhitespace();
					if (Consume('}')) {
						outValue = std::move(objectValue);
						return true;
					}

					if (!Consume(',')) {
						SetError(outError, "Expected ',' between object members");
						return false;
					}

					SkipWhitespace();
				}

				SetError(outError, "Unterminated JSON object");
				return false;
			}

			bool ParseArray(Value& outValue, std::string* outError) {
				if (!Consume('[')) {
					SetError(outError, "Expected array start");
					return false;
				}

				Value arrayValue = Value::MakeArray();
				SkipWhitespace();
				if (Consume(']')) {
					outValue = std::move(arrayValue);
					return true;
				}

				while (!IsAtEnd()) {
					Value childValue;
					if (!ParseAny(childValue, outError)) {
						return false;
					}

					arrayValue.Append(std::move(childValue));

					SkipWhitespace();
					if (Consume(']')) {
						outValue = std::move(arrayValue);
						return true;
					}

					if (!Consume(',')) {
						SetError(outError, "Expected ',' between array items");
						return false;
					}

					SkipWhitespace();
				}

				SetError(outError, "Unterminated JSON array");
				return false;
			}

			void SkipWhitespace() {
				while (!IsAtEnd() && std::isspace(static_cast<unsigned char>(m_Text[m_Position]))) {
					m_Position++;
				}
			}

			bool IsAtEnd() const {
				return m_Position >= m_Text.size();
			}

			char Peek() const {
				return IsAtEnd() ? '\0' : m_Text[m_Position];
			}

			char Advance() {
				return IsAtEnd() ? '\0' : m_Text[m_Position++];
			}

			bool Consume(char expected) {
				if (Peek() != expected) {
					return false;
				}
				m_Position++;
				return true;
			}

			void SetError(std::string* outError, std::string_view message) const {
				if (!outError) {
					return;
				}

				std::ostringstream stream;
				stream << message << " at byte " << m_Position;
				*outError = stream.str();
			}

		private:
			std::string_view m_Text;
			size_t m_Position = 0;
		};

		void WriteIndent(std::string& out, int depth, int indentSize) {
			out.append(static_cast<size_t>(depth * indentSize), ' ');
		}

		void WriteValue(std::string& out, const Value& value, bool pretty, int depth, int indentSize) {
			switch (value.GetType()) {
			case Value::Type::Null:
				out += "null";
				break;
			case Value::Type::Bool:
				out += value.AsBoolOr(false) ? "true" : "false";
				break;
			case Value::Type::Number:
			{
				std::ostringstream stream;
				stream.precision(15);
				stream << value.AsDoubleOr(0.0);
				out += stream.str();
				break;
			}
			case Value::Type::String:
				out += '"';
				out += EscapeString(value.AsStringOr());
				out += '"';
				break;
			case Value::Type::Object:
			{
				out += '{';
				const auto& members = value.GetObject();
				if (!members.empty()) {
					if (pretty) {
						out += '\n';
					}

					for (size_t i = 0; i < members.size(); i++) {
						if (pretty) {
							WriteIndent(out, depth + 1, indentSize);
						}
						out += '"';
						out += EscapeString(members[i].first);
						out += '"';
						out += pretty ? ": " : ":";
						WriteValue(out, members[i].second, pretty, depth + 1, indentSize);
						if (i + 1 < members.size()) {
							out += ',';
						}
						if (pretty) {
							out += '\n';
						}
					}

					if (pretty) {
						WriteIndent(out, depth, indentSize);
					}
				}
				out += '}';
				break;
			}
			case Value::Type::Array:
			{
				out += '[';
				const auto& items = value.GetArray();
				if (!items.empty()) {
					if (pretty) {
						out += '\n';
					}

					for (size_t i = 0; i < items.size(); i++) {
						if (pretty) {
							WriteIndent(out, depth + 1, indentSize);
						}
						WriteValue(out, items[i], pretty, depth + 1, indentSize);
						if (i + 1 < items.size()) {
							out += ',';
						}
						if (pretty) {
							out += '\n';
						}
					}

					if (pretty) {
						WriteIndent(out, depth, indentSize);
					}
				}
				out += ']';
				break;
			}
			}
		}
	} // namespace

	Value::Value(std::nullptr_t) {
	}

	Value::Value(bool value)
		: m_Type(Type::Bool)
		, m_Bool(value) {
	}

	Value::Value(double value)
		: m_Type(Type::Number)
		, m_Number(value) {
	}

	Value::Value(int value)
		: Value(static_cast<int64_t>(value)) {
	}

	Value::Value(int64_t value)
		: m_Type(Type::Number)
		, m_Number(static_cast<double>(value)) {
	}

	Value::Value(uint64_t value)
		: m_Type(Type::Number)
		, m_Number(static_cast<double>(value)) {
	}

	Value::Value(const char* value)
		: Value(value ? std::string(value) : std::string()) {
	}

	Value::Value(std::string value)
		: m_Type(Type::String)
		, m_String(std::move(value)) {
	}

	Value Value::MakeObject() {
		Value value;
		value.SetType(Type::Object);
		return value;
	}

	Value Value::MakeArray() {
		Value value;
		value.SetType(Type::Array);
		return value;
	}

	bool Value::AsBoolOr(bool fallback) const {
		return IsBool() ? m_Bool : fallback;
	}

	double Value::AsDoubleOr(double fallback) const {
		return IsNumber() ? m_Number : fallback;
	}

	int Value::AsIntOr(int fallback) const {
		if (!IsNumber()) {
			return fallback;
		}

		if (m_Number < static_cast<double>(std::numeric_limits<int>::min()) ||
			m_Number > static_cast<double>(std::numeric_limits<int>::max())) {
			return fallback;
		}

		return static_cast<int>(m_Number);
	}

	int64_t Value::AsInt64Or(int64_t fallback) const {
		if (!IsNumber()) {
			return fallback;
		}

		if (m_Number < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
			m_Number > static_cast<double>(std::numeric_limits<int64_t>::max())) {
			return fallback;
		}

		return static_cast<int64_t>(m_Number);
	}

	uint64_t Value::AsUInt64Or(uint64_t fallback) const {
		if (!IsNumber() || m_Number < 0.0 ||
			m_Number > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
			return fallback;
		}

		return static_cast<uint64_t>(m_Number);
	}

	std::string Value::AsStringOr(std::string fallback) const {
		return IsString() ? m_String : std::move(fallback);
	}

	Value::Object& Value::GetObject() {
		SetType(Type::Object);
		return m_Object;
	}

	const Value::Object& Value::GetObject() const {
		static const Object emptyObject;
		return IsObject() ? m_Object : emptyObject;
	}

	Value::Array& Value::GetArray() {
		SetType(Type::Array);
		return m_Array;
	}

	const Value::Array& Value::GetArray() const {
		static const Array emptyArray;
		return IsArray() ? m_Array : emptyArray;
	}

	Value* Value::FindMember(std::string_view key) {
		if (!IsObject()) {
			return nullptr;
		}

		for (auto& [memberKey, memberValue] : m_Object) {
			if (memberKey == key) {
				return &memberValue;
			}
		}

		return nullptr;
	}

	const Value* Value::FindMember(std::string_view key) const {
		if (!IsObject()) {
			return nullptr;
		}

		for (const auto& [memberKey, memberValue] : m_Object) {
			if (memberKey == key) {
				return &memberValue;
			}
		}

		return nullptr;
	}

	Value& Value::AddMember(std::string key, Value value) {
		SetType(Type::Object);
		for (auto& [memberKey, memberValue] : m_Object) {
			if (memberKey == key) {
				memberValue = std::move(value);
				return memberValue;
			}
		}

		m_Object.emplace_back(std::move(key), std::move(value));
		return m_Object.back().second;
	}

	Value& Value::Append(Value value) {
		SetType(Type::Array);
		m_Array.emplace_back(std::move(value));
		return m_Array.back();
	}

	void Value::SetType(Type type) {
		if (m_Type == type) {
			return;
		}

		m_Type = type;
		m_Bool = false;
		m_Number = 0.0;
		m_String.clear();
		m_Object.clear();
		m_Array.clear();
	}

	bool TryParse(std::string_view text, Value& outValue, std::string* outError) {
		Parser parser(text);
		return parser.ParseValue(outValue, outError);
	}

	Value Parse(std::string_view text, std::string* outError) {
		Value parsed;
		TryParse(text, parsed, outError);
		return parsed;
	}

	std::string EscapeString(std::string_view value) {
		std::string escaped;
		escaped.reserve(value.size());

		for (const char ch : value) {
			switch (ch) {
			case '"': escaped += "\\\""; break;
			case '\\': escaped += "\\\\"; break;
			case '\b': escaped += "\\b"; break;
			case '\f': escaped += "\\f"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default:
				if (static_cast<unsigned char>(ch) < 0x20) {
					std::ostringstream stream;
					stream << "\\u"
					       << std::hex
					       << std::uppercase
					       << static_cast<int>(static_cast<unsigned char>(ch) + 0x10000);
					std::string unicode = stream.str();
					escaped += "\\u";
					escaped.append(unicode.end() - 4, unicode.end());
				}
				else {
					escaped.push_back(ch);
				}
				break;
			}
		}

		return escaped;
	}

	std::string Stringify(const Value& value, bool pretty, int indentSize) {
		std::string output;
		WriteValue(output, value, pretty, 0, indentSize);
		if (pretty) {
			output += '\n';
		}
		return output;
	}

}
