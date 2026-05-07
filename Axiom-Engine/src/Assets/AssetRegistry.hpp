#pragma once

#include "Assets/AssetKind.hpp"
#include "Core/UUID.hpp"
#include "Project/AxiomProject.hpp"
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
#include <unordered_set>
#include <vector>

namespace Axiom {

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
			// Clear the known-missing cache: a FileWatcher event (or any
			// other dirtying signal) means the on-disk layout might have
			// changed and previously-missing assets could now exist.
			s_KnownMissingIds.clear();
		}

		// Engine-shipped assets that live under <exeDir>/AxiomAssets/ rather
		// than under the project's own Assets root. They are exposed to the
		// picker / inspector / serializer through the same UUID surface as
		// project assets, but with a fixed, hand-picked GUID instead of a
		// random one — that keeps the GUID stable across installs so a
		// scene saved with `BuiltIn::DefaultFont` still resolves on every
		// machine without checking in the .ttf to every project.
		//
		// Top-byte convention:
		//   0xAB - hand-picked GUIDs (e.g. FontManager's default font).
		//          Stable forever; safe to hard-code in component defaults.
		//   0xAA - path-hash GUIDs from RegisterBuiltInDirectory. Stable
		//          while the relative path is stable; if a file is renamed
		//          or moved within AxiomAssets/, the GUID changes.
		static void RegisterBuiltInAsset(const std::string& absolutePath, uint64_t guid, AssetKind kind) {
			if (absolutePath.empty() || guid == 0) {
				return;
			}
			Record record;
			record.Id = guid;
			record.Path = absolutePath;
			record.Kind = kind;
			s_BuiltInById[guid] = record;
			s_BuiltInPathToId[absolutePath] = guid;
		}

		// Walk `absoluteRoot` recursively and register every file as a
		// built-in asset with a deterministic path-hash GUID. Used by
		// engine startup to expose the contents of AxiomAssets/ (icons,
		// shaders, default audio, etc.) to the inspector's reference
		// picker. The eye toggle in the picker hides these by default
		// when the user wants to see only their project's own assets.
		//
		// Files whose path is already registered via RegisterBuiltInAsset
		// are skipped — this lets hand-picked GUIDs (FontManager's default
		// font) win without producing a duplicate entry. Call AFTER any
		// hand-picked registrations.
		static void RegisterBuiltInDirectory(const std::string& absoluteRoot) {
			if (absoluteRoot.empty()) {
				return;
			}
			std::error_code rootEc;
			if (!std::filesystem::is_directory(absoluteRoot, rootEc) || rootEc) {
				return;
			}

			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(
				 absoluteRoot,
				 std::filesystem::directory_options::skip_permission_denied,
				 ec), end;
				 it != end;
				 it.increment(ec))
			{
				if (ec) { ec.clear(); continue; }
				if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }

				const std::string absPath = NormalizePath(it->path().string());
				if (absPath.empty() || IsMetaFilePath(absPath)) {
					continue;
				}
				if (s_BuiltInPathToId.find(absPath) != s_BuiltInPathToId.end()) {
					continue;  // hand-picked or earlier scan already owns this path
				}

				const AssetKind kind = Classify(absPath);
				if (kind == AssetKind::Unknown) {
					continue;
				}

				// Stable GUID = FNV-1a 64 of the path RELATIVE to the
				// AxiomAssets root, lowercased and forward-slashed so the
				// same file gets the same GUID across Windows / Linux and
				// installs in different exe directories.
				std::error_code relEc;
				std::filesystem::path relPath = std::filesystem::relative(
					std::filesystem::path(absPath),
					std::filesystem::path(absoluteRoot), relEc);
				std::string relStr = relEc ? absPath : relPath.generic_string();
				std::transform(relStr.begin(), relStr.end(), relStr.begin(),
					[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

				uint64_t hash = 0xcbf29ce484222325ULL;
				for (char ch : relStr) {
					hash ^= static_cast<uint8_t>(ch);
					hash *= 0x100000001b3ULL;
				}
				// Top byte 0xAA tags this as an auto-registered built-in
				// (vs 0xAB for hand-picked). Bottom 56 bits stay distinct
				// from FontManager::k_DefaultFontAssetId regardless of
				// hash output.
				uint64_t guid = (hash & 0x00FFFFFFFFFFFFFFULL) | 0xAA00000000000000ULL;

				if (s_BuiltInById.find(guid) != s_BuiltInById.end()) {
					AIM_CORE_WARN_TAG("AssetRegistry",
						"Built-in path-hash collision: '{}' hashed to GUID {} which is already registered — skipping",
						absPath, guid);
					continue;
				}

				Record record;
				record.Id = guid;
				record.Path = absPath;
				record.Kind = kind;
				s_BuiltInById[guid] = record;
				s_BuiltInPathToId[absPath] = guid;
			}
		}

		static void Sync() {
			EnsureUpToDate();
		}

		static uint64_t GetOrCreateAssetUUID(const std::string& path) {
			const std::string normalizedPath = NormalizePath(path);
			if (normalizedPath.empty() || IsMetaFilePath(normalizedPath)) {
				return 0;
			}

			// Built-in (engine-shipped) assets bypass the project-tracked
			// path check — they live under <exeDir>/AxiomAssets/ and have
			// pre-assigned stable GUIDs.
			if (auto it = s_BuiltInPathToId.find(normalizedPath); it != s_BuiltInPathToId.end()) {
				return it->second;
			}

			EnsureUpToDate();
			if (!IsTrackedAsset(normalizedPath)) {
				return 0;
			}

			if (const auto it = s_PathToId.find(normalizedPath); it != s_PathToId.end()) {
				return it->second;
			}

			uint64_t id = ReadMetaId(normalizedPath);
			if (id == 0) {
				id = static_cast<uint64_t>(UUID());
			} else if (auto existing = s_IdToRecord.find(id); existing != s_IdToRecord.end()) {
				// GUID collision: a different asset already owns this GUID.
				// Regenerate so we don't break the existing reference, but
				// surface the collision — silent overwrite breaks references
				// in scene/prefab files that point at the original asset.
				AIM_CORE_WARN_TAG("AssetRegistry",
					"GUID collision while indexing '{}': id {} already owned by '{}' — regenerating new GUID for '{}'",
					normalizedPath, id, existing->second.Path, normalizedPath);
				id = static_cast<uint64_t>(UUID());
			}

			WriteMeta(normalizedPath, id, Classify(normalizedPath));
			Register(normalizedPath, id, Classify(normalizedPath));
			s_KnownMissingIds.erase(id);
			return id;
		}

		static uint64_t GetGUID(const std::string& path) {
			return GetOrCreateAssetUUID(path);
		}

		static std::string ResolvePath(uint64_t assetId) {
			// Built-ins skip dirty/rebuild — their paths don't change at runtime.
			if (auto it = s_BuiltInById.find(assetId); it != s_BuiltInById.end()) {
				if (std::filesystem::exists(it->second.Path)) {
					return it->second.Path;
				}
				return {};
			}

			EnsureUpToDate();
			auto it = s_IdToRecord.find(assetId);
			if (it == s_IdToRecord.end()) {
				return {};
			}

			if (std::filesystem::exists(it->second.Path)) {
				return it->second.Path;
			}

			// A file may have moved outside the editor after the registry
			// was built. Re-scan once to find it by the stable meta GUID,
			// but cache the miss so we don't trigger an O(N) directory
			// scan every frame for a permanently-missing asset (M4).
			// `MarkDirty()` (e.g. on FileWatcher events) clears this cache,
			// allowing genuinely-recovered assets to be picked up again.
			if (s_KnownMissingIds.contains(assetId)) {
				return {};
			}

			s_Dirty = true;
			EnsureUpToDate();
			it = s_IdToRecord.find(assetId);
			if (it == s_IdToRecord.end() || !std::filesystem::exists(it->second.Path)) {
				s_KnownMissingIds.insert(assetId);
				return {};
			}

			return it->second.Path;
		}

		static AssetKind GetKind(uint64_t assetId) {
			if (auto it = s_BuiltInById.find(assetId); it != s_BuiltInById.end()) {
				return it->second.Kind;
			}
			EnsureUpToDate();
			const auto it = s_IdToRecord.find(assetId);
			return it != s_IdToRecord.end() ? it->second.Kind : AssetKind::Unknown;
		}

		static bool Exists(uint64_t assetId) {
			return !ResolvePath(assetId).empty();
		}

		// Built-ins are engine-shipped assets registered via
		// `RegisterBuiltInAsset` — they live under <exeDir>/AxiomAssets/
		// rather than the project's Assets root and have hand-picked stable
		// GUIDs (top byte 0xAB by convention). Used by the inspector's
		// reference picker so the user can hide them from search results
		// when they're cluttering up a project's own asset list.
		static bool IsBuiltIn(uint64_t assetId) {
			return s_BuiltInById.find(assetId) != s_BuiltInById.end();
		}

		static size_t GetBuiltInCount() {
			return s_BuiltInById.size();
		}

		static bool IsTexture(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Texture;
		}

		static bool IsAudio(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Audio;
		}

		static bool IsFont(uint64_t assetId) {
			return GetKind(assetId) == AssetKind::Font;
		}

		static std::vector<Record> GetAssetsByKind(AssetKind kind) {
			EnsureUpToDate();

			std::vector<Record> records;
			records.reserve(s_IdToRecord.size() + s_BuiltInById.size());

			for (const auto& [id, record] : s_IdToRecord) {
				if (kind != AssetKind::Unknown && record.Kind != kind) {
					continue;
				}
				records.push_back(record);
			}
			// Built-ins surface alongside project assets so the picker
			// shows the engine's default font/etc. as selectable entries.
			for (const auto& [id, record] : s_BuiltInById) {
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
			if (AxiomProject* project = ProjectManager::GetCurrentProject()) {
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
			// New scan: previously-missing GUIDs may exist again.
			s_KnownMissingIds.clear();

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
			if (id == 0) {
				// Brand-new asset: mint an id and write its meta.
				id = static_cast<uint64_t>(UUID());
				WriteMeta(assetPath, id, kind);
			} else if (auto existing = s_IdToRecord.find(id); existing != s_IdToRecord.end()) {
				// Two assets claim the same GUID. Surface the collision so
				// the user knows references to one of them will silently
				// drift; the later-indexed asset gets a fresh GUID.
				AIM_CORE_WARN_TAG("AssetRegistry",
					"GUID collision while indexing '{}': id {} already owned by '{}' — regenerating new GUID for '{}'",
					assetPath, id, existing->second.Path, assetPath);
				id = static_cast<uint64_t>(UUID());
				WriteMeta(assetPath, id, kind);
			}
			// Else: meta is valid and unique. Don't rewrite — Record::Kind
			// in memory is always populated by Classify() below, so the
			// meta's `kind` field is purely informational and stale-meta
			// is harmless. Skipping the write avoids spamming the disk
			// (and the FileWatcher) on every rebuild (M5).

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
				if (AxiomProject* project = ProjectManager::GetCurrentProject()) {
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
			if (extension == ".ttf" || extension == ".otf") {
				return AssetKind::Font;
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
			(void)File::WriteAllText(GetMetaPath(assetPath), Json::Stringify(meta, true));
		}

		static std::string ToString(AssetKind kind) {
			switch (kind) {
			case AssetKind::Texture: return "Texture";
			case AssetKind::Audio: return "Audio";
			case AssetKind::Scene: return "Scene";
			case AssetKind::Prefab: return "Prefab";
			case AssetKind::Script: return "Script";
			case AssetKind::Font: return "Font";
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
		// GUIDs where ResolvePath already failed and a Rebuild() didn't
		// recover the path. Cleared on MarkDirty() and Rebuild() so that
		// FileWatcher events let recovered files re-resolve.
		inline static std::unordered_set<uint64_t> s_KnownMissingIds;
		// Engine-shipped assets registered via RegisterBuiltInAsset. Kept
		// separate from s_IdToRecord so Rebuild() doesn't clobber them and
		// .meta-file machinery never touches engine binaries.
		inline static std::unordered_map<uint64_t, Record> s_BuiltInById;
		inline static std::unordered_map<std::string, uint64_t> s_BuiltInPathToId;
	};

}
