#include "pch.hpp"
#include "EditorIcons.hpp"

namespace Axiom {
	std::unordered_map<std::string, EditorIcons::IconEntry> EditorIcons::s_Icons;
	bool EditorIcons::s_Initialized = false;

	// Must mirror the actual sizes shipped under
	// AxiomAssets/Textures/Editor/<group>/<name>/<name>_<size>.png. Listing a size here
	// that isn't on disk causes `Editor icon not found` warnings every frame for
	// any callsite whose snap-up lands on the missing size.
	static constexpr int k_AvailableSizes[] = { 16, 32, 64, 128 };

	static const char* GroupForName(const std::string& name) {
		if (name.rfind("file_", 0) == 0 || name.rfind("folder_", 0) == 0)
			return "FileIcons";
		return "General";
	}

	void EditorIcons::Initialize() {
		if (s_Initialized) return;
		s_Initialized = true;
	}

	void EditorIcons::Shutdown() {
		for (auto& [key, entry] : s_Icons)
			entry.Texture.Destroy();
		s_Icons.clear();
		s_Initialized = false;
	}

	int EditorIcons::SnapSize(int requested) {
		int best = k_AvailableSizes[0];
		for (int s : k_AvailableSizes) {
			best = s;
			if (s >= requested) break;
		}
		return best;
	}

	std::string EditorIcons::MakeKey(const std::string& name, int size) {
		return name + "_" + std::to_string(size);
	}

	unsigned int EditorIcons::Get(const std::string& name, int size) {
		int snapped = SnapSize(size);
		std::string key = MakeKey(name, snapped);

		auto it = s_Icons.find(key);
		if (it != s_Icons.end())
			return it->second.Texture.GetHandle();   // 0 for negative-cache entries; caller already treats 0 as "no icon"

		// Lazy-load from AxiomAssets/Textures/Editor/<group>/<name>/<name>_<size>.png
		std::string editorDir = Path::ResolveAxiomAssets("Textures");
		if (editorDir.empty()) {
			// Cache the failure so subsequent frames don't keep retrying.
			s_Icons[key] = IconEntry{};
			return 0;
		}

		std::string filename = name + "_" + std::to_string(snapped) + ".png";
		std::string fullpath = Path::Combine(editorDir, "Editor", GroupForName(name), name, filename);

		IconEntry entry;
		entry.Texture = Texture2D(fullpath.c_str(), Filter::Bilinear, Wrap::Clamp, Wrap::Clamp);
		entry.Size = snapped;

		if (!entry.Texture.IsValid()) {
			// Negative-cache: insert an empty entry so the next per-frame call doesn't
			// retry the load (which constructs a Texture2D, opens stb_image, hits the
			// missing file, and warn-logs every frame). The cached entry has handle 0
			// and the cache lookup above hands it back unchanged.
			AIM_CORE_WARN("Editor icon not found: {}", fullpath);
			s_Icons[key] = std::move(entry);
			return 0;
		}

		unsigned int handle = entry.Texture.GetHandle();
		s_Icons[key] = std::move(entry);
		return handle;
	}
}
