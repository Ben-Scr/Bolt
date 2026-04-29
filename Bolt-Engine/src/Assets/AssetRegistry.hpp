#pragma once

#include "Assets/AssetKind.hpp"
#include "Core/UUID.hpp"
#include "Project/BoltProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Bolt {

	class AssetRegistry {
	public:
		struct Record {
			uint64_t Id = 0;
			std::string Path;
			AssetKind Kind = AssetKind::Unknown;
		};

		static constexpr std::string_view MetaExtension = ".meta";

		static bool IsMetaFilePath(std::string_view path) {
			const std::string normalized = ToLowerCopy(std::string(path));
			const std::string metaExtension(MetaExtension);
			if (normalized.size() < metaExtension.size()) {
				return false;
			}

			return normalized.compare(normalized.size() - metaExtension.size(), metaExtension.size(), metaExtension) == 0;
		}

		static void MarkDirty() {
			s_Dirty = true;
		}

		static void Sync() {
			EnsureUpToDate();
		}

		static uint64_t GetOrCreateAssetUUID(const std::string& path) {
			const std::string normalizedPath = NormalizePath(path);
			if (normalizedPath.empty() || IsMetaFilePath(normalizedPath)) {
				return 0;
			}

			EnsureUpToDate();
			if (!IsTrackedAsset(normalizedPath)) {
				return 0;
			}

			if (const auto it = s_PathToId.find(normalizedPath); it != s_PathToId.end()) {
				return it->second;
			}

			uint64_t id = ReadMetaId(normalizedPath);
			if (id == 0 || s_IdToRecord.contains(id)) {
				id = static_cast<uint64_t>(UUID());
			}

			WriteMeta(normalizedPath, id, Classify(normalizedPath));
			Register(normalizedPath, id, Classify(normalizedPath));
			return id;
		}

		static uint64_t GetGUID(const std::string& path) {
			return GetOrCreateAssetUUID(path);
		}

		static std::string ResolvePath(uint64_t assetId) {
			EnsureUpToDate();
			auto it = s_IdToRecord.find(assetId);
			if (it == s_IdToRecord.end()) {
				return {};
			}

			if (std::filesystem::exists(it->second.Path)) {
				return it->second.Path;
			}

			// A file may have moved outside the editor after the registry was
			// built. Re-scan the project once and resolve by the stable meta GUID.
			s_Dirty = true;
			EnsureUpToDate();
			it = s_IdToRecord.find(assetId);
			if (it == s_IdToRecord.end() || !std::filesystem::exists(it->second.Path)) {
				return {};
			}

			return it->second.Path;
		}

		static AssetKind GetKind(uint64_t assetId) {
			EnsureUpToDate();
			const auto it = s_IdToRecord.find(assetId);
			return it != s_IdToRecord.end() ? it->second.Kind : AssetKind::Unknown;
		}

		static bool Exists(uint64_t assetId) {
			return !ResolvePath(assetId).empty();
		}

		static bool IsTexture(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Texture;
		}

		static bool IsAudio(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Audio;
		}

		static std::vector<Record> GetAssetsByKind(AssetKind kind) {
			EnsureUpToDate();

			std::vector<Record> records;
			records.reserve(s_IdToRecord.size());

			for (const auto& [id, record] : s_IdToRecord) {
				if (kind != AssetKind::Unknown && record.Kind != kind) {
					continue;
				}

				records.push_back(record);
			}

			std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
				const std::string aName = std::filesystem::path(a.Path).filename().string();
				const std::string bName = std::filesystem::path(b.Path).filename().string();
				if (aName == bName) {
					return a.Path < b.Path;
				}

				return aName < bName;
			});

			return records;
		}

		static std::vector<Record> FindAll(AssetKind kind, const std::string& pathPrefix = {}) {
			EnsureUpToDate();

			std::string normalizedPrefix;
			if (!pathPrefix.empty()) {
				std::filesystem::path prefixPath(pathPrefix);
				if (!prefixPath.is_absolute()) {
					const std::string root = GetAssetsRoot();
					if (!root.empty()) {
						prefixPath = std::filesystem::path(root) / prefixPath;
					}
				}
				normalizedPrefix = NormalizePath(prefixPath.string());
			}

			std::vector<Record> records;
			for (const auto& [id, record] : s_IdToRecord) {
				(void)id;
				if (kind != AssetKind::Unknown && record.Kind != kind) {
					continue;
				}
				if (!normalizedPrefix.empty()
					&& (record.Path.size() < normalizedPrefix.size()
						|| record.Path.compare(0, normalizedPrefix.size(), normalizedPrefix) != 0)) {
					continue;
				}
				records.push_back(record);
			}

			std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
				return a.Path < b.Path;
			});
			return records;
		}

		static std::string GetDisplayName(uint64_t assetId) {
			const std::string path = ResolvePath(assetId);
			if (path.empty()) {
				return {};
			}

			return std::filesystem::path(path).filename().string();
		}

		static std::string GetMetaPath(const std::string& assetPath) {
			if (assetPath.empty()) {
				return {};
			}

			return assetPath + std::string(MetaExtension);
		}

		static void DeleteCompanionMetadata(const std::string& assetPath) {
			const std::string normalizedPath = NormalizePath(assetPath);
			if (normalizedPath.empty() || IsMetaFilePath(normalizedPath)) {
				return;
			}

			const std::string metaPath = GetMetaPath(normalizedPath);
			std::error_code ec;
			std::filesystem::remove(metaPath, ec);
			MarkDirty();
		}

		static void MoveCompanionMetadata(const std::string& from, const std::string& to) {
			const std::string normalizedFrom = NormalizePath(from);
			const std::string normalizedTo = NormalizePath(to);
			if (normalizedFrom.empty() || normalizedTo.empty() || IsMetaFilePath(normalizedFrom) || IsMetaFilePath(normalizedTo)) {
				return;
			}

			const std::string metaFrom = GetMetaPath(normalizedFrom);
			if (!File::Exists(metaFrom)) {
				MarkDirty();
				return;
			}

			const std::string metaTo = GetMetaPath(normalizedTo);
			std::error_code ec;
			const std::filesystem::path target(metaTo);
			if (target.has_parent_path()) {
				std::filesystem::create_directories(target.parent_path(), ec);
				ec.clear();
			}

			std::filesystem::rename(metaFrom, metaTo, ec);
			if (ec) {
				ec.clear();
				std::filesystem::copy_file(metaFrom, metaTo, std::filesystem::copy_options::overwrite_existing, ec);
				if (!ec) {
					ec.clear();
					std::filesystem::remove(metaFrom, ec);
				}
			}

			MarkDirty();
		}

	private:
		static std::string GetAssetsRoot() {
			if (BoltProject* project = ProjectManager::GetCurrentProject()) {
				return NormalizePath(project->AssetsDirectory);
			}

			const std::string fallback = Path::Combine(Path::ExecutableDir(), "Assets");
			if (!std::filesystem::exists(fallback)) {
				return {};
			}

			return NormalizePath(fallback);
		}

		static void EnsureUpToDate() {
			const std::string root = GetAssetsRoot();
			if (root != s_TrackedRoot) {
				s_TrackedRoot = root;
				s_Dirty = true;
			}

			if (!s_Dirty) {
				return;
			}

			Rebuild();
		}

		static void Rebuild() {
			s_IdToRecord.clear();
			s_PathToId.clear();

			if (s_TrackedRoot.empty() || !std::filesystem::exists(s_TrackedRoot)) {
				s_Dirty = false;
				return;
			}

			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(
				 s_TrackedRoot,
				 std::filesystem::directory_options::skip_permission_denied,
				 ec), end;
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

				const std::string assetPath = NormalizePath(it->path().string());
				if (assetPath.empty() || IsMetaFilePath(assetPath)) {
					continue;
				}

				IndexAsset(assetPath);
			}

			s_Dirty = false;
		}

		static void IndexAsset(const std::string& assetPath) {
			if (!IsTrackedAsset(assetPath)) {
				return;
			}

			AssetKind kind = Classify(assetPath);
			uint64_t id = ReadMetaId(assetPath);
			if (id == 0 || s_IdToRecord.contains(id)) {
				id = static_cast<uint64_t>(UUID());
				WriteMeta(assetPath, id, kind);
			}
			else {
				WriteMeta(assetPath, id, kind);
			}

			Register(assetPath, id, kind);
		}

		static void Register(const std::string& assetPath, uint64_t id, AssetKind kind) {
			Record record;
			record.Id = id;
			record.Path = assetPath;
			record.Kind = kind;

			s_PathToId[assetPath] = id;
			s_IdToRecord[id] = std::move(record);
		}

		static std::string NormalizePath(const std::string& path) {
			if (path.empty()) {
				return {};
			}

			std::filesystem::path fsPath(path);
			if (!fsPath.is_absolute()) {
				if (BoltProject* project = ProjectManager::GetCurrentProject()) {
					const std::filesystem::path candidate = std::filesystem::path(project->AssetsDirectory) / fsPath;
					if (std::filesystem::exists(candidate)) {
						fsPath = candidate;
					}
					else {
						fsPath = std::filesystem::absolute(fsPath);
					}
				}
				else {
					const std::filesystem::path fallback = std::filesystem::path(Path::ExecutableDir()) / "Assets" / fsPath;
					if (std::filesystem::exists(fallback)) {
						fsPath = fallback;
					}
					else {
						fsPath = std::filesystem::absolute(fsPath);
					}
				}
			}

			std::error_code ec;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(fsPath, ec);
			if (ec) {
				normalized = fsPath.lexically_normal();
			}

			return normalized.make_preferred().string();
		}

		static bool IsTrackedAsset(const std::string& assetPath) {
			if (assetPath.empty() || s_TrackedRoot.empty()) {
				return false;
			}

			if (!std::filesystem::exists(assetPath) || !std::filesystem::is_regular_file(assetPath)) {
				return false;
			}

			if (assetPath.size() < s_TrackedRoot.size() || assetPath.compare(0, s_TrackedRoot.size(), s_TrackedRoot) != 0) {
				return false;
			}

			if (assetPath.size() == s_TrackedRoot.size()) {
				return false;
			}

			const char separator = assetPath[s_TrackedRoot.size()];
			return separator == '\\' || separator == '/';
		}

		static AssetKind Classify(const std::string& assetPath) {
			const std::string extension = ToLowerCopy(std::filesystem::path(assetPath).extension().string());
			if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga") {
				return AssetKind::Texture;
			}
			if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac") {
				return AssetKind::Audio;
			}
			if (extension == ".scene") {
				return AssetKind::Scene;
			}
			if (extension == ".prefab") {
				return AssetKind::Prefab;
			}
			if (extension == ".cs" || extension == ".cpp" || extension == ".c" || extension == ".hpp" || extension == ".h") {
				return AssetKind::Script;
			}
			if (!extension.empty()) {
				return AssetKind::Other;
			}

			return AssetKind::Unknown;
		}

		static uint64_t ReadMetaId(const std::string& assetPath) {
			const std::string metaPath = GetMetaPath(assetPath);
			if (!File::Exists(metaPath)) {
				return 0;
			}

			Json::Value meta;
			std::string parseError;
			if (!Json::TryParse(File::ReadAllText(metaPath), meta, &parseError) || !meta.IsObject()) {
				return 0;
			}

			const Json::Value* uuidValue = meta.FindMember("AssetGUID");
			if (!uuidValue) {
				uuidValue = meta.FindMember("uuid");
			}
			if (!uuidValue) {
				return 0;
			}

			if (uuidValue->IsString()) {
				try {
					return static_cast<uint64_t>(std::stoull(uuidValue->AsStringOr()));
				}
				catch (...) {
					return 0;
				}
			}

			return uuidValue->AsUInt64Or(0);
		}

		static void WriteMeta(const std::string& assetPath, uint64_t id, AssetKind kind) {
			Json::Value meta = Json::Value::MakeObject();
			meta.AddMember("AssetGUID", Json::Value(std::to_string(id)));
			meta.AddMember("uuid", Json::Value(std::to_string(id)));
			meta.AddMember("kind", Json::Value(ToString(kind)));
			File::WriteAllText(GetMetaPath(assetPath), Json::Stringify(meta, true));
		}

		static std::string ToString(AssetKind kind) {
			switch (kind) {
			case AssetKind::Texture: return "Texture";
			case AssetKind::Audio: return "Audio";
			case AssetKind::Scene: return "Scene";
			case AssetKind::Prefab: return "Prefab";
			case AssetKind::Script: return "Script";
			case AssetKind::Other: return "Other";
			case AssetKind::Unknown:
			default:
				return "Unknown";
			}
		}

		static std::string ToLowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return value;
		}

	private:
		inline static bool s_Dirty = true;
		inline static std::string s_TrackedRoot;
		inline static std::unordered_map<uint64_t, Record> s_IdToRecord;
		inline static std::unordered_map<std::string, uint64_t> s_PathToId;
	};

}
