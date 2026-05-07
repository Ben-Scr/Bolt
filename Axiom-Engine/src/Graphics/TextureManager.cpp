#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"
#include "TextureManager.hpp"
#include <Serialization/File.hpp>

#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace Axiom {
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

		struct ReferenceProviderEntry {
			uint32_t Token = 0;
			TextureManager::ReferenceProvider Fn;
		};
		std::vector<ReferenceProviderEntry>& GetReferenceProviders() {
			static std::vector<ReferenceProviderEntry> s_Providers;
			return s_Providers;
		}
		uint32_t& NextReferenceProviderToken() {
			static uint32_t s_NextToken = 0;
			return s_NextToken;
		}

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
	std::string TextureManager::s_RootPath = Path::Combine("AxiomAssets", "Textures");

	constexpr uint16_t k_InvalidIndex = std::numeric_limits<uint16_t>::max();

	std::size_t TextureManager::GetTotalTextureMemoryBytes() {
		// 4 bpp (RGBA8) approximation; skips default textures at the front of the table.
		std::size_t total = 0;
		const std::size_t defaultCount = s_DefaultTextures.size();
		for (std::size_t i = 0; i < s_Textures.size(); ++i) {
			const TextureEntry& entry = s_Textures[i];
			if (!entry.IsValid) continue;
			if (i < defaultCount) continue;
			const std::size_t w = static_cast<std::size_t>(entry.Texture.GetWidth());
			const std::size_t h = static_cast<std::size_t>(entry.Texture.GetHeight());
			total += w * h * 4;
		}
		return total;
	}

	void TextureManager::Initialize() {
		if (s_IsInitialized) {
			AIM_CORE_WARN("TextureManager is already initialized");
			return;
		}

		std::string texDir = Path::ResolveAxiomAssets("Textures");
		if (texDir.empty()) {
			AIM_CORE_ERROR("AxiomAssets/Textures not found");
			texDir = Path::Combine(Path::ExecutableDir(), "AxiomAssets", "Textures");
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
			AIM_CORE_WARN("TextureManager isn't initialized");
			return;
		}

		UnloadAll(true);
		s_Textures.clear();
		s_TextureLookup.clear();
		while (!s_FreeIndices.empty()) {
			s_FreeIndices.pop();
		}
		// Drop external reference providers so their captures (often holding
		// scene/registry references) can't outlive the manager.
		GetReferenceProviders().clear();

		s_IsInitialized = false;
	}

	TextureHandle TextureManager::LoadTexture(const std::string_view& path, Filter filter, Wrap u, Wrap v) {
		if (!s_IsInitialized) {
			AIM_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(AxiomErrorCode::NotInitialized));
			return TextureHandle::Invalid();
		}

		const std::string fullpath = ResolveTexturePath(path, s_RootPath);
		if (fullpath.empty()) {
			AIM_CORE_ERROR("[{}] Texture '{}' not found", ErrorCodeToString(AxiomErrorCode::FileNotFound), std::string(path));
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
				AIM_CORE_ERROR("[{}] Failed to load texture with path '{}'", ErrorCodeToString(AxiomErrorCode::LoadFailed), fullpath);
				// Re-queue the slot we popped so it isn't permanently orphaned. Without
				// this, every failed load burns one slot — eventually crossing the
				// k_InvalidIndex (65535) cap and rejecting all new loads. Bump generation
				// so any handle minted against this slot before now can't re-validate
				// against a future occupant.
				entry.IsValid = false;
				if (entry.Generation < std::numeric_limits<uint16_t>::max()) {
					entry.Generation++;
				}
				s_FreeIndices.push(index);
				return TextureHandle::Invalid();
			}

			// Wraparound guard: if the slot has been recycled 65535 times, retiring it
			// rather than wrapping prevents a stale handle (with the wrapped generation)
			// from silently re-validating against a freshly-recycled entry.
			if (entry.Generation == std::numeric_limits<uint16_t>::max()) {
				AIM_CORE_WARN("Texture slot {} hit generation limit; retiring slot", index);
				entry.IsValid = false;
				entry.Texture.Destroy();
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
			if (s_Textures.size() >= TextureHandle::k_InvalidIndex) {
				AIM_CORE_ERROR("[{}] Texture handle table is full", ErrorCodeToString(AxiomErrorCode::OutOfBounds));
				return TextureHandle::Invalid();
			}
			index = static_cast<uint16_t>(s_Textures.size());

			TextureEntry entry;
			entry.Texture = Texture2D(fullpath.c_str(), filter, u, v);

			if (!entry.Texture.IsValid()) {
				AIM_CORE_ERROR("[{}] Failed to load texture with path '{}'", ErrorCodeToString(AxiomErrorCode::LoadFailed), fullpath);
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
		if (assetId == 0) {
			return TextureHandle::Invalid();
		}

		if (!AssetRegistry::IsTexture(assetId)) {
			AssetRegistry::MarkDirty();
			AssetRegistry::Sync();
			if (!AssetRegistry::IsTexture(assetId)) {
				return TextureHandle::Invalid();
			}
		}

		std::string path = AssetRegistry::ResolvePath(assetId);
		if (path.empty()) {
			AssetRegistry::MarkDirty();
			AssetRegistry::Sync();
			path = AssetRegistry::ResolvePath(assetId);
		}
		if (path.empty()) {
			return TextureHandle::Invalid();
		}

		return LoadTexture(path, filter, u, v);
	}

	TextureHandle TextureManager::GetDefaultTexture(DefaultTexture type) {
		if (!s_IsInitialized) {
			AIM_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(AxiomErrorCode::NotInitialized));
			return TextureHandle::Invalid();
		}

		int index = static_cast<int>(type);

		if (index < 0 || index >= static_cast<int>(s_DefaultTextures.size())) {
			AIM_CORE_ERROR("[{}] Invalid DefaultTexture index {}", ErrorCodeToString(AxiomErrorCode::OutOfRange), index);
			return TextureHandle::Invalid();
		}

		if (index >= static_cast<int>(s_Textures.size())) {
			AIM_CORE_ERROR("[{}] Default texture '{}' is unavailable", ErrorCodeToString(AxiomErrorCode::InvalidHandle), s_DefaultTextures[index]);
			return TextureHandle::Invalid();
		}

		const TextureEntry& entry = s_Textures[index];
		if (!entry.IsValid) {
			AIM_CORE_ERROR("[{}] Default texture '{}' failed to load", ErrorCodeToString(AxiomErrorCode::LoadFailed), s_DefaultTextures[index]);
			return TextureHandle::Invalid();
		}

		return TextureHandle(static_cast<uint16_t>(index), entry.Generation);
	}

	void TextureManager::UnloadTexture(TextureHandle handle) {
		if (!s_IsInitialized) {
			AIM_CORE_WARN("TextureManager isn't initialized");
			return;
		}

		if (handle.index >= s_Textures.size()) {
			AIM_CORE_WARN("[{}] Invalid texture handle index: {}", ErrorCodeToString(AxiomErrorCode::InvalidHandle), handle.index);
			return;
		}

		TextureEntry& entry = s_Textures[handle.index];

		if (!entry.IsValid || entry.Generation != handle.generation) {
			AIM_CORE_WARN("[{}] Texture handle is outdated or invalid", ErrorCodeToString(AxiomErrorCode::InvalidHandle));
			return;
		}

		if (handle.index < s_DefaultTextures.size()) {
			AIM_CORE_WARN("Cannot unload default texture");
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

	TextureHandle TextureManager::GetTextureHandle(const std::string& name, Filter filter, Wrap u, Wrap v) {
		if (!s_IsInitialized) {
			AIM_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(AxiomErrorCode::NotInitialized));
			return TextureHandle::Invalid();
		}

		auto handle = FindTextureByPath(name, filter, u, v);

		if (handle.index == k_InvalidIndex) {
			AIM_CORE_WARN("[{}] Texture with name '{}' doesn't exist for the requested sampler settings", ErrorCodeToString(AxiomErrorCode::InvalidArgument), name);
			return TextureHandle::Invalid();
		}

		return handle;
	}

	TextureHandle TextureManager::GetTextureHandle(const std::string& name) {
		if (!s_IsInitialized) {
			AIM_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(AxiomErrorCode::NotInitialized));
			return TextureHandle::Invalid();
		}

		auto handle = FindTextureByPath(name);

		if (handle.index == k_InvalidIndex) {
			AIM_CORE_WARN("[{}] Texture with name '{}' doesn't exist", ErrorCodeToString(AxiomErrorCode::InvalidArgument), name);
			return TextureHandle::Invalid();
		}

		return handle;
	}

	std::vector<TextureHandle> TextureManager::GetLoadedHandles() {
		if (!s_IsInitialized) {
			AIM_CORE_WARN("TextureManager isn't initialized");
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
			AIM_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(AxiomErrorCode::NotInitialized));
			return nullptr;
		}

		if (handle.index >= s_Textures.size()) {
			AIM_CORE_ERROR("[{}] TextureHandle index {} out of range", ErrorCodeToString(AxiomErrorCode::OutOfRange), handle.index);
			return nullptr;
		}

		TextureEntry& entry = s_Textures[handle.index];

		if (!entry.IsValid) {
			AIM_CORE_ERROR("[{}] Texture at index {} is not valid", ErrorCodeToString(AxiomErrorCode::InvalidHandle), handle.index);
			return nullptr;
		}

		if (entry.Generation != handle.generation) {
			AIM_CORE_ERROR("[{}] Invalid texture generation: entry {} != handle {}", ErrorCodeToString(AxiomErrorCode::InvalidHandle), entry.Generation, handle.generation);
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
			AIM_CORE_ERROR("[{}] TextureManager isn't initialized", ErrorCodeToString(AxiomErrorCode::NotInitialized));
			return;
		}

		s_Textures.reserve(s_DefaultTextures.size() + 32);

		for (const auto& texPath : s_DefaultTextures) {
			const std::string resolvedPath = ResolveTexturePath(texPath, s_RootPath);
			TextureEntry entry;
			entry.Texture = Texture2D(resolvedPath.c_str(), Filter::Point, Wrap::Clamp, Wrap::Clamp);

			if (!entry.Texture.IsValid()) {
				AIM_CORE_ERROR("[{}] Failed to load default texture at path: {}", ErrorCodeToString(AxiomErrorCode::LoadFailed), texPath);
				// Keep the slot reserved (occupied with IsValid=false) but DO NOT
				// queue it for reuse. The first 9 indices are sacred: GetDefaultTexture
				// looks them up by enum, PurgeUnreferenced skips them, and UnloadTexture
				// rejects unloads against them. If we recycled this slot, a user
				// LoadTexture call would overwrite a "default" slot — silently swapping
				// the engine's fallback texture for an arbitrary user one, and locking
				// the user texture against future unloads.
				entry.IsValid = false;
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
			AIM_CORE_WARN("TextureManager isn't initialized");
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

	uint32_t TextureManager::AddReferenceProvider(ReferenceProvider provider) {
		if (!provider) return 0;
		auto& providers = GetReferenceProviders();
		auto& nextToken = NextReferenceProviderToken();
		const uint32_t token = ++nextToken;
		providers.push_back({ token, std::move(provider) });
		return token;
	}

	void TextureManager::RemoveReferenceProvider(uint32_t token) {
		if (token == 0) return;
		auto& providers = GetReferenceProviders();
		providers.erase(
			std::remove_if(providers.begin(), providers.end(),
				[token](const ReferenceProviderEntry& e) { return e.Token == token; }),
			providers.end());
	}

	size_t TextureManager::PurgeUnreferenced() {
		if (!s_IsInitialized) {
			AIM_CORE_WARN("TextureManager isn't initialized");
			return 0;
		}

		std::unordered_set<TextureHandle> referenced;
		referenced.reserve(s_Textures.size());

		// External providers (editor caches, package panels) emit their handles; per-provider try/catch.
		const ReferenceEmitter emit = [&referenced](TextureHandle h) {
			if (h.IsValid()) referenced.insert(h);
		};
		for (const auto& entry : GetReferenceProviders()) {
			if (!entry.Fn) continue;
			try {
				entry.Fn(emit);
			} catch (const std::exception& e) {
				AIM_CORE_ERROR_TAG("TextureManager",
					"Reference provider (token {}) threw: {}", entry.Token, e.what());
			} catch (...) {
				AIM_CORE_ERROR_TAG("TextureManager",
					"Reference provider (token {}) threw an unknown exception", entry.Token);
			}
		}

		SceneManager::Get().ForeachLoadedScene([&referenced](Scene& scene) {
			entt::registry& registry = scene.GetRegistry();

			auto sprites = registry.view<SpriteRendererComponent>();
			for (auto entity : sprites) {
				const auto& sprite = sprites.get<SpriteRendererComponent>(entity);
				if (sprite.TextureHandle.IsValid()) {
					referenced.insert(sprite.TextureHandle);
				}
			}

			auto particles = registry.view<ParticleSystem2DComponent>();
			for (auto entity : particles) {
				const auto& particleSystem = particles.get<ParticleSystem2DComponent>(entity);
				const TextureHandle& handle = particleSystem.GetTextureHandle();
				if (handle.IsValid()) {
					referenced.insert(handle);
				}
			}

			auto images = registry.view<ImageComponent>();
			for (auto entity : images) {
				const auto& image = images.get<ImageComponent>(entity);
				if (image.TextureHandle.IsValid()) {
					referenced.insert(image.TextureHandle);
				}
			}
		});

		// Skip default-texture indices — UnloadTexture rejects them and would spam warnings.
		const size_t defaultCount = s_DefaultTextures.size();
		size_t freedCount = 0;
		for (size_t i = defaultCount; i < s_Textures.size(); ++i) {
			const TextureEntry& entry = s_Textures[i];
			if (!entry.IsValid) {
				continue;
			}

			TextureHandle handle(static_cast<uint16_t>(i), entry.Generation);
			if (referenced.find(handle) == referenced.end()) {
				UnloadTexture(handle);
				++freedCount;
			}
		}

		AIM_CORE_INFO_TAG("TextureManager", "Purged {} unreferenced textures", freedCount);
		return freedCount;
	}
}
