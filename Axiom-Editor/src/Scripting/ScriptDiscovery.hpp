#pragma once

#include "Project/ProjectManager.hpp"
#include "Scripting/ScriptType.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace Axiom::EditorScriptDiscovery {

	struct ScriptEntry {
		std::string ClassName;
		std::filesystem::path Path;
		std::string Extension;
		ScriptType Type = ScriptType::Unknown;
		bool IsManagedComponent = false;
		bool IsGameSystem = false;
		bool IsGlobalSystem = false;
	};

	inline std::string ToLowerCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}

	inline std::string TrimWhitespace(std::string value)
	{
		const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
			return std::isspace(ch) != 0;
		});
		const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
			return std::isspace(ch) != 0;
		}).base();

		if (first >= last) {
			return {};
		}

		return std::string(first, last);
	}

	inline bool IsNativeScriptBootstrapFile(const std::filesystem::path& path)
	{
		return path.filename().string() == "NativeScriptExports.cpp";
	}

	inline bool IsCSharpScriptExtension(const std::string& extension)
	{
		return ToLowerCopy(extension) == ".cs";
	}

	inline bool IsNativeScriptSourceExtension(const std::string& extension)
	{
		const std::string normalized = ToLowerCopy(extension);
		return normalized == ".cpp" || normalized == ".cc" || normalized == ".cxx";
	}

	inline bool IsValidRegisteredScriptToken(const std::string& token)
	{
		if (token.empty()) {
			return false;
		}

		for (char ch : token) {
			const unsigned char uch = static_cast<unsigned char>(ch);
			if (std::isalnum(uch) != 0 || ch == '_' || ch == ':') {
				continue;
			}
			return false;
		}

		return true;
	}

	inline bool ContainsScriptEntry(
		const std::vector<ScriptEntry>& entries,
		const std::string& className,
		ScriptType type)
	{
		return std::any_of(entries.begin(), entries.end(), [&](const ScriptEntry& entry) {
			return entry.ClassName == className && entry.Type == type;
		});
	}

	inline void AppendScriptEntry(
		std::vector<ScriptEntry>& entries,
		const std::string& className,
		const std::filesystem::path& path,
		const std::string& extension,
		ScriptType type,
		bool isManagedComponent = false,
		bool isGameSystem = false,
		bool isGlobalSystem = false)
	{
		if (className.empty() || type == ScriptType::Unknown || ContainsScriptEntry(entries, className, type)) {
			return;
		}

		entries.push_back({ className, path, extension, type, isManagedComponent, isGameSystem, isGlobalSystem });
	}

	inline std::string ReadSourceWithoutWhitespace(const std::filesystem::path& filePath)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input) {
			return {};
		}

		const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		std::string compactSource;
		compactSource.reserve(source.size());
		for (char ch : source) {
			if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
				compactSource.push_back(ch);
			}
		}

		return compactSource;
	}

	inline bool SourceHasBaseClass(const std::string& compactSource, std::string_view baseClassName)
	{
		const std::string shortPattern = ":" + std::string(baseClassName);
		const std::string qualifiedPattern = ":Axiom." + std::string(baseClassName);
		const std::string globalPattern = ":global::Axiom." + std::string(baseClassName);
		return compactSource.find(shortPattern) != std::string::npos
			|| compactSource.find(qualifiedPattern) != std::string::npos
			|| compactSource.find(globalPattern) != std::string::npos;
	}

	inline bool SourceLooksLikeManagedComponent(const std::filesystem::path& filePath)
	{
		const std::string compactSource = ReadSourceWithoutWhitespace(filePath);
		return compactSource.find(":Component") != std::string::npos
			|| compactSource.find(":Axiom.Component") != std::string::npos
			|| compactSource.find(":global::Axiom.Component") != std::string::npos;
	}

	inline std::vector<std::string> ExtractRegisteredNativeScriptClasses(const std::filesystem::path& filePath)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input) {
			return {};
		}

		const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		constexpr std::string_view token = "REGISTER_SCRIPT";

		std::vector<std::string> classes;
		std::size_t searchOffset = 0;
		while (true) {
			const std::size_t tokenPosition = source.find(token.data(), searchOffset, token.size());
			if (tokenPosition == std::string::npos) {
				break;
			}

			const std::size_t openParen = source.find('(', tokenPosition + token.size());
			if (openParen == std::string::npos) {
				break;
			}

			const std::size_t closeParen = source.find(')', openParen + 1);
			if (closeParen == std::string::npos) {
				break;
			}

			std::string className = TrimWhitespace(source.substr(openParen + 1, closeParen - openParen - 1));
			if (IsValidRegisteredScriptToken(className)
				&& std::find(classes.begin(), classes.end(), className) == classes.end()) {
				classes.push_back(std::move(className));
			}

			searchOffset = closeParen + 1;
		}

		return classes;
	}

	inline void CollectScriptFile(const std::filesystem::path& filePath, std::vector<ScriptEntry>& entries)
	{
		std::string extension = ToLowerCopy(filePath.extension().string());
		if (IsCSharpScriptExtension(extension)) {
			const std::string compactSource = ReadSourceWithoutWhitespace(filePath);
			const bool isManagedComponent = SourceHasBaseClass(compactSource, "Component");
			const bool isGameSystem = SourceHasBaseClass(compactSource, "GameSystem");
			const bool isGlobalSystem = SourceHasBaseClass(compactSource, "GlobalSystem");
			AppendScriptEntry(entries, filePath.stem().string(), filePath, extension, ScriptType::Managed, isManagedComponent, isGameSystem, isGlobalSystem);
			return;
		}

		if (!IsNativeScriptSourceExtension(extension) || IsNativeScriptBootstrapFile(filePath)) {
			return;
		}

		for (const std::string& className : ExtractRegisteredNativeScriptClasses(filePath)) {
			AppendScriptEntry(entries, className, filePath, extension, ScriptType::Native);
		}
	}

	inline void CollectScriptFiles(const std::filesystem::path& directory, std::vector<ScriptEntry>& entries)
	{
		std::error_code ec;
		if (directory.empty() || !std::filesystem::exists(directory, ec) || ec) {
			return;
		}

		for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec), end;
			 it != end;
			 it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}

			if (!it->is_regular_file(ec) || ec) {
				ec.clear();
				continue;
			}

			CollectScriptFile(it->path(), entries);
		}
	}

	inline void SortScriptEntries(std::vector<ScriptEntry>& entries)
	{
		std::sort(entries.begin(), entries.end(), [](const ScriptEntry& a, const ScriptEntry& b) {
			if (a.ClassName != b.ClassName) {
				return a.ClassName < b.ClassName;
			}
			if (a.Type != b.Type) {
				return a.Type < b.Type;
			}
			return a.Path.string() < b.Path.string();
		});
	}

	inline void CollectProjectScriptEntries(std::vector<ScriptEntry>& entries)
	{
		if (AxiomProject* project = ProjectManager::GetCurrentProject()) {
			CollectScriptFiles(std::filesystem::path(project->ScriptsDirectory), entries);
			CollectScriptFiles(std::filesystem::path(project->NativeSourceDir), entries);
		}
		else {
			const std::filesystem::path base = std::filesystem::path(Path::ExecutableDir());
			CollectScriptFiles(base / ".." / ".." / ".." / "Axiom-Sandbox" / "Source", entries);
			CollectScriptFiles(base / ".." / ".." / ".." / "Axiom-NativeScripts" / "Source", entries);
		}

		SortScriptEntries(entries);
	}

} // namespace Axiom::EditorScriptDiscovery
