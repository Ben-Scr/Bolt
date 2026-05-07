#include "pch.hpp"
#include "Packages/GitHubSource.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/Directory.hpp"
#include "Utils/Process.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>

namespace Axiom {

	namespace {
		// XML-escape any character that has meaning in attribute or element
		// content. Without this, GitHub-index strings (which we don't control)
		// could inject MSBuild elements/attributes into the user's .csproj.
		std::string XmlEscape(std::string_view value) {
			std::string out;
			out.reserve(value.size());
			for (char c : value) {
				switch (c) {
					case '&':  out += "&amp;";  break;
					case '<':  out += "&lt;";   break;
					case '>':  out += "&gt;";   break;
					case '"':  out += "&quot;"; break;
					case '\'': out += "&apos;"; break;
					default:   out.push_back(c); break;
				}
			}
			return out;
		}

		// Reject names/versions that would cause path traversal or otherwise
		// escape the project tree when concatenated into a filesystem path. We
		// don't try to canonicalise — we just refuse anything containing path
		// separators, parent-dir tokens, drive markers, control chars, or
		// non-printable bytes. Whitelist: [A-Za-z0-9._-+~]. Empty rejected.
		bool IsSafePathComponent(std::string_view value) {
			if (value.empty()) return false;
			if (value == "." || value == "..") return false;
			for (char c : value) {
				const unsigned char uc = static_cast<unsigned char>(c);
				if (uc < 0x20 || uc == 0x7F) return false;
				if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
				    c == '"' || c == '<' || c == '>' || c == '|') return false;
			}
			// Also forbid embedded ".." substrings even if the whole string
			// isn't exactly "..".
			if (value.find("..") != std::string_view::npos) return false;
			return true;
		}

		// Add a <Reference> to a .csproj file
		static bool AddReferenceToProject(const std::string& csprojPath, const std::string& dllPath,
			const std::string& hintPath) {
			std::ifstream in(csprojPath);
			if (!in.is_open()) return false;
			std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();

			// Check if reference already exists
			if (content.find(hintPath) != std::string::npos)
				return true;

			// Escape every untrusted value before splicing it into XML. The
			// stem is derived from dllPath which itself flows from packageId;
			// hintPath includes packageId/version. Both come from a remote
			// package index — must not be trusted as raw XML.
			const std::string stem = std::filesystem::path(dllPath).stem().string();
			std::string refBlock =
				"  <ItemGroup>\n"
				"    <Reference Include=\"" + XmlEscape(stem) + "\">\n"
				"      <HintPath>" + XmlEscape(hintPath) + "</HintPath>\n"
				"    </Reference>\n"
				"  </ItemGroup>\n";

			// Insert before </Project>
			auto pos = content.rfind("</Project>");
			if (pos == std::string::npos) return false;

			content.insert(pos, refBlock);

			// Atomic write through File::WriteAllText (temp + rename), so a crash
			// mid-write doesn't leave the user's csproj truncated.
			if (!File::WriteAllText(csprojPath, content)) {
				AIM_CORE_ERROR_TAG("AxiomPackages", "Failed to write csproj after adding reference: {}", csprojPath);
				return false;
			}
			return true;
		}

		// Remove a <Reference Include="..."> ELEMENT (and only that element) from a
		// .csproj. The previous implementation erased the entire enclosing <ItemGroup>,
		// which silently deleted any sibling <Reference>s the user had hand-consolidated
		// — losing user data. This version walks from the matched <Reference> to its
		// closing </Reference> (or self-closing /> if it has no body) and erases just
		// that span; the surrounding ItemGroup (and any siblings inside it) are left
		// intact.
		static bool RemoveReferenceFromProject(const std::string& csprojPath, const std::string& assemblyName) {
			std::ifstream in(csprojPath);
			if (!in.is_open()) return false;
			std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();

			// Find the <Reference Include="..."> opening tag. Tolerate either a body
			// + closing tag, or a self-closing form (<Reference Include="X"/>).
			const std::string includeFragment = "Include=\"" + XmlEscape(assemblyName) + "\"";
			size_t scanFrom = 0;
			size_t refStart = std::string::npos;
			while (scanFrom < content.size()) {
				const size_t tagStart = content.find("<Reference ", scanFrom);
				if (tagStart == std::string::npos) break;
				const size_t tagClose = content.find('>', tagStart);
				if (tagClose == std::string::npos) break;
				if (content.find(includeFragment, tagStart) < tagClose) {
					refStart = tagStart;
					break;
				}
				scanFrom = tagClose + 1;
			}

			if (refStart == std::string::npos) return true; // already removed

			// Determine where the element ends: self-closing <Reference .../> ends at
			// the first '/>' inside the opening tag; otherwise look for </Reference>.
			const size_t openTagClose = content.find('>', refStart);
			if (openTagClose == std::string::npos) return false;
			size_t refEnd = std::string::npos;
			if (openTagClose > 0 && content[openTagClose - 1] == '/') {
				refEnd = openTagClose + 1;
			}
			else {
				constexpr std::string_view kClose = "</Reference>";
				const size_t closePos = content.find(kClose, openTagClose);
				if (closePos == std::string::npos) return false;
				refEnd = closePos + kClose.size();
			}

			// Trim the trailing newline if there's one immediately after.
			if (refEnd < content.size() && content[refEnd] == '\n') refEnd++;

			content.erase(refStart, refEnd - refStart);

			// If the <ItemGroup> that contained the reference is now empty (only
			// whitespace between <ItemGroup> and </ItemGroup>), remove that empty
			// container too. Use the earlier (now-relocated) refStart as a search
			// hint into the modified string.
			const size_t itemGroupOpen = content.rfind("<ItemGroup>", refStart);
			if (itemGroupOpen != std::string::npos) {
				const size_t itemGroupClose = content.find("</ItemGroup>", itemGroupOpen);
				if (itemGroupClose != std::string::npos) {
					const size_t bodyStart = itemGroupOpen + std::string_view("<ItemGroup>").size();
					const std::string body = content.substr(bodyStart, itemGroupClose - bodyStart);
					if (std::all_of(body.begin(), body.end(), [](unsigned char c) { return std::isspace(c) != 0; })) {
						size_t eraseEnd = itemGroupClose + std::string_view("</ItemGroup>").size();
						if (eraseEnd < content.size() && content[eraseEnd] == '\n') eraseEnd++;
						content.erase(itemGroupOpen, eraseEnd - itemGroupOpen);
					}
				}
			}

			if (!File::WriteAllText(csprojPath, content)) {
				AIM_CORE_ERROR_TAG("AxiomPackages", "Failed to write csproj after removing reference: {}", csprojPath);
				return false;
			}
			return true;
		}
	}

	GitHubSource::GitHubSource(const std::string& toolExePath, const std::string& indexUrl,
		const std::string& displayName)
		: m_ToolExePath(toolExePath)
		, m_IndexUrl(indexUrl)
		, m_DisplayName(displayName) {
	}

	std::string GitHubSource::RunTool(const std::vector<std::string>& args) const {
		std::vector<std::string> command;
		command.reserve(args.size() + 2);
		if (std::filesystem::path(m_ToolExePath).extension() == ".dll") {
			command.push_back("dotnet");
		}
		command.push_back(m_ToolExePath);
		command.insert(command.end(), args.begin(), args.end());

		Process::Result result = Process::Run(command);
		if (!result.Succeeded()) {
			AIM_CORE_ERROR_TAG("GitHubSource", "Failed to run tool (exit code {})", result.ExitCode);
		}
		return result.Output;
	}

	void GitHubSource::EnsureIndex() {
		if (m_IndexLoaded) return;

		AIM_CORE_INFO_TAG("GitHubSource", "Fetching package index from {}", m_IndexUrl);
		std::string json = RunTool({ "github-index", m_IndexUrl });

		if (json.empty()) {
			AIM_CORE_WARN_TAG("GitHubSource", "Failed to fetch package index");
			m_IndexLoaded = true;
			return;
		}

		m_CachedIndex.clear();
		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
			AIM_CORE_WARN_TAG("GitHubSource", "Failed to parse index: {}", parseError);
			m_IndexLoaded = true;
			return;
		}

		const Json::Value* packagesValue = root.FindMember("packages");
		if (!packagesValue || !packagesValue->IsArray()) {
			AIM_CORE_WARN_TAG("GitHubSource", "Package index has no 'packages' array");
			m_IndexLoaded = true;
			return;
		}

		m_CachedIndex.reserve(packagesValue->GetArray().size());

		for (const Json::Value& item : packagesValue->GetArray()) {
			if (!item.IsObject()) {
				continue;
			}

			PackageInfo info;
			if (const Json::Value* idValue = item.FindMember("id")) info.Id = idValue->AsStringOr();
			if (const Json::Value* versionValue = item.FindMember("version")) info.Version = versionValue->AsStringOr();
			if (const Json::Value* descriptionValue = item.FindMember("description")) info.Description = descriptionValue->AsStringOr();
			if (const Json::Value* authorsValue = item.FindMember("authors")) info.Authors = authorsValue->AsStringOr();
			info.SourceName = m_DisplayName;
			info.SourceType = PackageSourceType::GitHub;

			if (!info.Id.empty())
				m_CachedIndex.push_back(std::move(info));
		}

		m_IndexLoaded = true;
		AIM_CORE_INFO_TAG("GitHubSource", "Loaded {} engine packages", m_CachedIndex.size());
	}

	std::vector<PackageInfo> GitHubSource::Search(const std::string& query, int take) {
		EnsureIndex();

		if (query.empty())
			return m_CachedIndex;

		std::vector<PackageInfo> results;
		std::string lowerQuery = query;
		std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

		for (const auto& pkg : m_CachedIndex) {
			std::string lowerId = pkg.Id;
			std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);
			std::string lowerDesc = pkg.Description;
			std::transform(lowerDesc.begin(), lowerDesc.end(), lowerDesc.begin(), ::tolower);

			if (lowerId.find(lowerQuery) != std::string::npos ||
				lowerDesc.find(lowerQuery) != std::string::npos) {
				results.push_back(pkg);
				if (static_cast<int>(results.size()) >= take) break;
			}
		}

		return results;
	}

	PackageOperationResult GitHubSource::Install(const std::string& packageId,
		const std::string& version, const std::string& csprojPath) {

		EnsureIndex();

		// Find the package in the index
		const PackageInfo* found = nullptr;
		for (const auto& pkg : m_CachedIndex) {
			if (pkg.Id == packageId) {
				found = &pkg;
				break;
			}
		}

		if (!found) {
			return { false, "Package '" + packageId + "' not found in index" };
		}

		// Re-fetch the full index entry to get distributionType and dllUrl
		std::string json = RunTool({ "github-index", m_IndexUrl });
		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
			return { false, "Failed to parse package index" };
		}

		std::string distType, dllUrl, nugetId;
		if (const Json::Value* packagesValue = root.FindMember("packages")) {
			for (const Json::Value& item : packagesValue->GetArray()) {
				if (!item.IsObject()) {
					continue;
				}
				const Json::Value* idValue = item.FindMember("id");
				if (!idValue || idValue->AsStringOr() != packageId) {
					continue;
				}
				if (const Json::Value* distTypeValue = item.FindMember("distributionType")) distType = distTypeValue->AsStringOr();
				if (const Json::Value* dllUrlValue = item.FindMember("dllUrl")) dllUrl = dllUrlValue->AsStringOr();
				if (const Json::Value* nugetIdValue = item.FindMember("nugetPackageId")) nugetId = nugetIdValue->AsStringOr();
				break;
			}
		}

		if (distType == "nuget") {
			// Delegate to dotnet add package
			std::string id = nugetId.empty() ? packageId : nugetId;
			std::vector<std::string> command = {
				"dotnet",
				"add",
				csprojPath,
				"package",
				id
			};
			if (!version.empty()) {
				command.push_back("--version");
				command.push_back(version);
			}

			Process::Result addResult = Process::Run(command);
			if (!addResult.Succeeded())
				return { false, "dotnet add package failed (exit code " + std::to_string(addResult.ExitCode) + ")" };

			return { true, packageId + " installed via NuGet" };
		}
		else if (distType == "dll") {
			if (dllUrl.empty())
				return { false, "Package '" + packageId + "' has no download URL" };

			// Determine local path
			AxiomProject* project = ProjectManager::GetCurrentProject();
			if (!project)
				return { false, "No project loaded" };

			// Reject untrusted strings that could escape the project tree
			// (packageId/version come from the remote index — never trust them
			// as raw filesystem components).
			if (!IsSafePathComponent(packageId)) {
				return { false, "Refusing to install package with unsafe id: '" + packageId + "'" };
			}
			if (!version.empty() && !IsSafePathComponent(version)) {
				return { false, "Refusing to install package with unsafe version: '" + version + "'" };
			}

			std::string packagesDir = Path::Combine(project->RootDirectory, "Packages", packageId, version);
			std::string dllName = packageId + ".dll";
			std::string localDll = Path::Combine(packagesDir, dllName);

			// Download
			AIM_CORE_INFO_TAG("GitHubSource", "Downloading {} to {}", packageId, localDll);
			std::string dlOutput = RunTool({ "github-download", dllUrl, localDll });

			Json::Value downloadResult;
			std::string downloadParseError;
			if (!Json::TryParse(dlOutput, downloadResult, &downloadParseError) || !downloadResult.IsObject()) {
				return { false, "Download failed for " + packageId };
			}

			const Json::Value* successValue = downloadResult.FindMember("success");
			if (!successValue || !successValue->AsBoolOr(false))
				return { false, "Download failed for " + packageId };

			// Add <Reference> to .csproj. MSBuild accepts both '/' and '\' as
			// path separators on every platform; using '/' keeps the file
			// portable across Windows/Linux without affecting Windows builds.
			std::string hintPath = "Packages/" + packageId + "/" + version + "/" + dllName;
			if (!AddReferenceToProject(csprojPath, localDll, hintPath))
				return { false, "Failed to add reference to .csproj" };

			return { true, packageId + " installed (DLL)" };
		}

		return { false, "Unknown distribution type: " + distType };
	}

	PackageOperationResult GitHubSource::Remove(const std::string& packageId,
		const std::string& csprojPath) {

		// Try removing as NuGet package first
		Process::Result removeResult = Process::Run({
			"dotnet",
			"remove",
			csprojPath,
			"package",
			packageId
		});

		if (removeResult.Succeeded())
			return { true, packageId + " removed" };

		// If that fails, try removing as a DLL reference
		if (!RemoveReferenceFromProject(csprojPath, packageId))
			return { false, "Failed to remove " + packageId };

		// Delete local DLL
		AxiomProject* project = ProjectManager::GetCurrentProject();
		if (project) {
			std::string packagesDir = Path::Combine(project->RootDirectory, "Packages", packageId);
			if (std::filesystem::exists(packagesDir)) {
				std::error_code ec;
				std::filesystem::remove_all(packagesDir, ec);
			}
		}

		return { true, packageId + " removed" };
	}

}
