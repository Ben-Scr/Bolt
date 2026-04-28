#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "TextureManager.hpp"
#include <Serialization/File.hpp>

namespace Bolt {
	namespace {
		struct TextureLookupKey {
			std::string Path;
			Filter SamplerFilter = Filter::Point;
			Wrap WrapU = Wrap::Clamp;
			Wrap WrapV = Wrap::Clamp;

			bool operator==(const TextureLookupKey& other) const {
				return Path == other.Path
					&& SamplerFilter == other.SamplerFilter
					&& WrapU == other.WrapU
					&& WrapV == other.WrapV;
			}
		};

		struct TextureLookupKeyHash {
			size_t operator()(const TextureLookupKey& key) const {
				size_t seed = std::hash<std::string>{}(key.Path);
				seed ^= static_cast<size_t>(key.SamplerFilter) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
				seed ^= static_cast<size_t>(key.WrapU) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
				seed ^= static_cast<size_t>(key.WrapV) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
				return seed;
			}
		};

		std::unordered_map<TextureLookupKey, TextureHandle, TextureLookupKeyHash> s_TextureLookup;

		std::string NormalizeTexturePath(const std::filesystem::path& path) {
			std::error_code ec;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
			if (ec) {
				normalized = path.lexically_normal();
			}

			return normalized.make_preferred().string();
		}

		std::string ResolveTexturePath(std::string_view rawPath, const std::string& rootPath) {
			if (rawPath.empty()) {
				return {};
			}

			const std::filesystem::path requestedPath(rawPath);
			if (File::Exists(requestedPath.string())) {
				return NormalizeTexturePath(requestedPath);
			}

			const std::filesystem::path enginePath = std::filesystem::path(rootPath) / requestedPath;
			if (File::Exists(enginePath.string())) {
				return NormalizeTexturePath(enginePath);
			}

			const std::filesystem::path userPath =
				std::filesystem::path(Path::ExecutableDir()) / "Assets" / "Textures" / requestedPath;
			if (File::Exists(userPath.string())) {
				return NormalizeTexturePath(userPath);
			}

			return {};
		}

		TextureLookupKey MakeTextureLookupKey(const std::string& path, Filter filter, Wrap u, Wrap v) {
			return TextureLookupKey{ path, filter, u, v };
		}
	}

	std::array<std::string, 9> TextureManager::s_DefaultTextures = {
		   "Default/Square.png",
		   "Default/Pixel.png",
		   "Default/circle.png",
		   "Default/Capsule.png",
		   "Default/IsometricDiamond.png",
		   "Default/HexagonFlatTop.png",
		   "Default/HexagonPointedTop.png",
		   "Default/9Sliced.png",
		   "Default/Invisible.png"
	};

	std::vector<TextureEntry> TextureManager::s_Textures = {};
	std::queue<uint16_t> TextureManager::s_FreeIndices = {};

	bool TextureManager::s_IsInitialized = false;
	std::string TextureManager::s_RootPath = Path::Combine("BoltAssets", "Textures");

	constexpr uint16_t k_InvalidIndex = std::numeric_limits<uint16_t>::max();

	void TextureManager::Initialize() {
		if (s_IsInitialized) {
			BT_CORE_WARN("TextureManager is already initialized");
			return;
		}

		std::string texDir = Path::ResolveBoltAssets("Textures");
		if (texDir.empty()) {
			BT_CORE_ERROR("BoltAssets/Textures not found");
			texDir = Path::Combine(Path::ExecutableDir(), "BoltAssets", "Textures");
		}
		s_RootPath = texDir;

		s_Textures.clear();
		s_TextureLookup.clear();
		while (!s_FreeIndices.empty()) {
			s_FreeIndices.pop();
		}

		s_IsInitialized = true;
		LoadDefaultTextures();
	}

	void TextureManager::Shutdown() {
		if (!s_IsInitialized) {
			BT_CORE_WARN("TextureManager isn't initialized");
			return;
		}

		UnloadAll(true);
		s_Textures.clear();
		s_TextureLookup.clear();
		while (!s_FreeIndices.empty()) {
			s_FreeIndices.pop();
		}

		s_IsInitialized = false;
	}

	TextureHandle TextureManager::LoadTexture(const std::string_view& path, Filter filter, Wrap u, Wrap v) {
		if (!s_IsInitialized) {
			BT_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(BoltErrorCode::NotInitialized));
			return TextureHandle::Invalid();
		}

		const std::string fullpath = ResolveTexturePath(path, s_RootPath);
		if (fullpath.empty()) {
			BT_CORE_ERROR("[{}] Texture '{}' not found", ErrorCodeToString(BoltErrorCode::FileNotFound), std::string(path));
			return TextureHandle::Invalid();
		}

		auto existingHandle = FindTextureByPath(fullpath, filter, u, v);
		if (existingHandle.index != k_InvalidIndex) {
			return existingHandle;
		}

		uint16_t index = k_InvalidIndex;
		if (!s_FreeIndices.empty()) {
			index = s_FreeIndices.front();
			s_FreeIndices.pop();

			auto& entry = s_Textures[index];
			entry.Texture.Destroy();
			entry.Texture = Texture2D(fullpath.c_str(), filter, u, v);

			if (!entry.Texture.IsValid()) {
				BT_CORE_ERROR("[{}] Failed to load texture with path '{}'", ErrorCodeToString(BoltErrorCode::LoadFailed), fullpath);
				return TextureHandle::Invalid();
			}

			entry.Generation++;
			entry.IsValid = true;
			entry.Name = fullpath;
			entry.SamplerFilter = filter;
			entry.WrapU = u;
			entry.WrapV = v;
		}
		else {
			index = static_cast<uint16_t>(s_Textures.size());

			TextureEntry entry;
			entry.Texture = Texture2D(fullpath.c_str(), filter, u, v);

			if (!entry.Texture.IsValid()) {
				BT_CORE_ERROR("[{}] Failed to load texture with path '{}'", ErrorCodeToString(BoltErrorCode::LoadFailed), fullpath);
				return TextureHandle::Invalid();
			}

			entry.Generation = 0;
			entry.IsValid = true;
			entry.Name = fullpath;
			entry.SamplerFilter = filter;
			entry.WrapU = u;
			entry.WrapV = v;

			s_Textures.push_back(std::move(entry));
		}

		s_TextureLookup[MakeTextureLookupKey(fullpath, filter, u, v)] = TextureHandle(index, s_Textures[index].Generation);
		return { index, s_Textures[index].Generation };
	}

	TextureHandle TextureManager::LoadTextureByUUID(uint64_t assetId, Filter filter, Wrap u, Wrap v) {
		if (assetId == 0 || !AssetRegistry::IsTexture(assetId)) {
			return TextureHandle::Invalid();
		}

		const std::string path = AssetRegistry::ResolvePath(assetId);
		if (path.empty()) {
			return TextureHandle::Invalid();
		}

		return LoadTexture(path, filter, u, v);
	}

	TextureHandle TextureManager::GetDefaultTexture(DefaultTexture type) {
		if (!s_IsInitialized) {
			BT_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(BoltErrorCode::NotInitialized));
			return TextureHandle::Invalid();
		}

		int index = static_cast<int>(type);

		if (index < 0 || index >= static_cast<int>(s_DefaultTextures.size())) {
			BT_CORE_ERROR("[{}] Invalid DefaultTexture index {}", ErrorCodeToString(BoltErrorCode::OutOfRange), index);
			return TextureHandle::Invalid();
		}

		if (index >= static_cast<int>(s_Textures.size())) {
			BT_CORE_ERROR("[{}] Default texture '{}' is unavailable", ErrorCodeToString(BoltErrorCode::InvalidHandle), s_DefaultTextures[index]);
			return TextureHandle::Invalid();
		}

		const TextureEntry& entry = s_Textures[index];
		if (!entry.IsValid) {
			BT_CORE_ERROR("[{}] Default texture '{}' failed to load", ErrorCodeToString(BoltErrorCode::LoadFailed), s_DefaultTextures[index]);
			return TextureHandle::Invalid();
		}

		return TextureHandle(static_cast<uint16_t>(index), entry.Generation);
	}

	void TextureManager::UnloadTexture(TextureHandle handle) {
		if (!s_IsInitialized) {
			BT_CORE_WARN("TextureManager isn't initialized");
			return;
		}

		if (handle.index >= s_Textures.size()) {
			BT_CORE_WARN("[{}] Invalid texture handle index: {}", ErrorCodeToString(BoltErrorCode::InvalidHandle), handle.index);
			return;
		}

		TextureEntry& entry = s_Textures[handle.index];

		if (!entry.IsValid || entry.Generation != handle.generation) {
			BT_CORE_WARN("[{}] Texture handle is outdated or invalid", ErrorCodeToString(BoltErrorCode::InvalidHandle));
			return;
		}

		if (handle.index < s_DefaultTextures.size()) {
			BT_CORE_WARN("Cannot unload default texture");
			return;
		}

		s_TextureLookup.erase(MakeTextureLookupKey(entry.Name, entry.SamplerFilter, entry.WrapU, entry.WrapV));
		entry.Texture.Destroy();
		entry.IsValid = false;
		entry.Name.clear();
		entry.SamplerFilter = Filter::Point;
		entry.WrapU = Wrap::Clamp;
		entry.WrapV = Wrap::Clamp;
		s_FreeIndices.push(handle.index);
	}

	TextureHandle TextureManager::GetTextureHandle(const std::string& name) {
		if (!s_IsInitialized) {
			BT_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(BoltErrorCode::NotInitialized));
			return TextureHandle::Invalid();
		}

		auto handle = FindTextureByPath(name);

		if (handle.index == k_InvalidIndex) {
			BT_CORE_WARN("[{}] Texture with name '{}' doesn't exist", ErrorCodeToString(BoltErrorCode::InvalidArgument), name);
			return TextureHandle::Invalid();
		}

		return handle;
	}

	std::vector<TextureHandle> TextureManager::GetLoadedHandles() {
		if (!s_IsInitialized) {
			BT_CORE_WARN("TextureManager isn't initialized");
			return {};
		}

		std::vector<TextureHandle> handles;
		handles.reserve(s_Textures.size());

		for (size_t i = 0; i < s_Textures.size(); i++) {
			if (s_Textures[i].IsValid) {
				handles.emplace_back(static_cast<uint16_t>(i), s_Textures[i].Generation);
			}
		}

		return handles;
	}

	Texture2D* TextureManager::GetTexture(TextureHandle handle) {
		if (!s_IsInitialized) {
			BT_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(BoltErrorCode::NotInitialized));
			return nullptr;
		}

		if (handle.index >= s_Textures.size()) {
			BT_CORE_ERROR("[{}] TextureHandle index {} out of range", ErrorCodeToString(BoltErrorCode::OutOfRange), handle.index);
			return nullptr;
		}

		TextureEntry& entry = s_Textures[handle.index];

		if (!entry.IsValid) {
			BT_CORE_ERROR("[{}] Texture at index {} is not valid", ErrorCodeToString(BoltErrorCode::InvalidHandle), handle.index);
			return nullptr;
		}

		if (entry.Generation != handle.generation) {
			BT_CORE_ERROR("[{}] Invalid texture generation: entry {} != handle {}", ErrorCodeToString(BoltErrorCode::InvalidHandle), entry.Generation, handle.generation);
			return nullptr;
		}

		return &entry.Texture;
	}

	uint64_t TextureManager::GetTextureAssetUUID(TextureHandle handle) {
		if (!IsValid(handle)) {
			return 0;
		}

		return AssetRegistry::GetOrCreateAssetUUID(s_Textures[handle.index].Name);
	}

	void TextureManager::LoadDefaultTextures() {
		if (!s_IsInitialized) {
			BT_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(BoltErrorCode::NotInitialized));
			return;
		}

		s_Textures.reserve(s_DefaultTextures.size() + 32);

		for (const auto& texPath : s_DefaultTextures) {
			const std::string resolvedPath = ResolveTexturePath(texPath, s_RootPath);
			TextureEntry entry;
			entry.Texture = Texture2D(resolvedPath.c_str(), Filter::Point, Wrap::Clamp, Wrap::Clamp);

			if (!entry.Texture.IsValid()) {
				BT_CORE_ERROR("[{}] Failed to load default texture at path: {}", ErrorCodeToString(BoltErrorCode::LoadFailed), texPath);
				s_Textures.push_back(std::move(entry));
				continue;
			}

			entry.Generation = 0;
			entry.IsValid = true;
			entry.Name = resolvedPath;
			entry.SamplerFilter = Filter::Point;
			entry.WrapU = Wrap::Clamp;
			entry.WrapV = Wrap::Clamp;

			s_Textures.push_back(std::move(entry));
			s_TextureLookup[MakeTextureLookupKey(s_Textures.back().Name, Filter::Point, Wrap::Clamp, Wrap::Clamp)] =
				TextureHandle(static_cast<uint16_t>(s_Textures.size() - 1), s_Textures.back().Generation);
		}
	}

	void TextureManager::UnloadAll(bool defaultTextures) {
		if (!s_IsInitialized) {
			BT_CORE_WARN("TextureManager isn't initialized");
			return;
		}

		size_t startOffset = defaultTextures ? 0 : s_DefaultTextures.size();
		for (size_t i = startOffset; i < s_Textures.size(); i++) {
			if (s_Textures[i].IsValid) {
				s_TextureLookup.erase(MakeTextureLookupKey(
					s_Textures[i].Name,
					s_Textures[i].SamplerFilter,
					s_Textures[i].WrapU,
					s_Textures[i].WrapV));
				s_Textures[i].Texture.Destroy();
				s_Textures[i].IsValid = false;
				s_Textures[i].Name.clear();
				s_Textures[i].SamplerFilter = Filter::Point;
				s_Textures[i].WrapU = Wrap::Clamp;
				s_Textures[i].WrapV = Wrap::Clamp;
				if (i >= s_DefaultTextures.size()) {
					s_FreeIndices.push(static_cast<uint16_t>(i));
				}
			}
		}

		if (defaultTextures) {
			s_Textures.clear();
			s_TextureLookup.clear();
			while (!s_FreeIndices.empty()) {
				s_FreeIndices.pop();
			}
		}
	}

	TextureHandle TextureManager::FindTextureByPath(const std::string& path, Filter filter, Wrap u, Wrap v) {
		if (!s_IsInitialized) {
			return TextureHandle::Invalid();
		}

		const std::string normalizedPath = ResolveTexturePath(path, s_RootPath);
		const std::string& lookupPath = normalizedPath.empty() ? path : normalizedPath;

		auto it = s_TextureLookup.find(MakeTextureLookupKey(lookupPath, filter, u, v));
		if (it != s_TextureLookup.end()) {
			const TextureHandle handle = it->second;
			if (IsValid(handle)) {
				return handle;
			}

			s_TextureLookup.erase(it);
		}

		return { k_InvalidIndex, 0 };
	}

	TextureHandle TextureManager::FindTextureByPath(const std::string& path) {
		if (!s_IsInitialized) {
			return TextureHandle::Invalid();
		}

		const std::string normalizedPath = ResolveTexturePath(path, s_RootPath);
		const std::string& lookupPath = normalizedPath.empty() ? path : normalizedPath;

		for (size_t i = 0; i < s_Textures.size(); i++) {
			const TextureEntry& entry = s_Textures[i];
			if (entry.IsValid && entry.Name == lookupPath) {
				return TextureHandle(static_cast<uint16_t>(i), entry.Generation);
			}
		}

		return { k_InvalidIndex, 0 };
	}
}
