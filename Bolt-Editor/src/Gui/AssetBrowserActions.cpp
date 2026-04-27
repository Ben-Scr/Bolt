#include <pch.hpp>
#include "Gui/AssetBrowser.hpp"

#include "Core/Log.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Project/BoltProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/Path.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef BT_PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace Bolt {
	namespace {
		bool IsNativeScriptSourceExtension(std::string extension) {
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx"
				|| extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx"
				|| extension == ".inl" || extension == ".ipp";
		}

		std::filesystem::path GetNativeSourceDirectory(bool ensureProjectFiles = false) {
			if (BoltProject* project = ProjectManager::GetCurrentProject()) {
				if (ensureProjectFiles) {
					project->EnsureNativeScriptProjectFiles();
				}
				return project->NativeSourceDir;
			}

			return std::filesystem::path(Path::ExecutableDir()) / ".." / ".." / ".." / "Bolt-NativeScripts" / "Source";
		}

		std::filesystem::path ResolveNativeScriptMirrorPath(const std::string& path, bool ensureProjectFiles = false) {
			const std::filesystem::path sourcePath(path);
			if (!IsNativeScriptSourceExtension(sourcePath.extension().string())) {
				return {};
			}

			std::filesystem::path nativeSourceDir = GetNativeSourceDirectory(ensureProjectFiles);
			if (nativeSourceDir.empty()) {
				return {};
			}

			const std::filesystem::path mirrorPath = nativeSourceDir / sourcePath.filename();
			std::error_code ec;
			if (std::filesystem::equivalent(sourcePath, mirrorPath, ec)) {
				return {};
			}

			return mirrorPath;
		}
	}

	void AssetBrowser::BeginRename(const std::string& path, const std::string& currentName) {
		m_IsRenaming = true;
		m_RenamePath = path;
		m_PressedPath.clear();
		m_RenameFrameCounter = 0;
		std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", currentName.c_str());
	}

	void AssetBrowser::CommitRename() {
		std::string newName(m_RenameBuffer);
		std::string oldName = std::filesystem::path(m_RenamePath).filename().string();

		if (m_PendingScriptType != PendingScriptType::None) {
			std::string className = newName;
			std::string ext = (m_PendingScriptType == PendingScriptType::CSharp) ? ".cs" : ".cpp";

			if (!className.empty() && className.find('.') == std::string::npos) {
			}
			else if (className.size() > ext.size()) {
				std::string lowerName = className;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				if (lowerName.substr(lowerName.size() - ext.size()) == ext)
					className = className.substr(0, className.size() - ext.size());
			}

			if (className.empty()) className = "NewScript";

			std::string finalFileName = className + ext;
			std::string finalPath = (std::filesystem::path(m_PendingScriptDir) / finalFileName).string();

			if (std::filesystem::exists(m_RenamePath)) {
				std::error_code ec;
				std::filesystem::remove(m_RenamePath, ec);
			}

			if (m_PendingScriptType == PendingScriptType::CSharp) {
				std::string boilerplate =
					"using Bolt;\n"
					"\n"
					"public class " + className + " : BoltScript\n"
					"{\n"
					"    public void Start()\n"
					"    {\n"
					"    }\n"
					"\n"
					"    public void Update()\n"
					"    {\n"
					"    }\n"
					"}\n";

				std::ofstream file(finalPath);
				if (file.is_open()) { file << boilerplate; file.close(); }

				BoltProject* project = ProjectManager::GetCurrentProject();
				if (!project) {
					auto sandboxDir = std::filesystem::path(Path::ExecutableDir()) / ".." / ".." / ".." / "Bolt-Sandbox" / "Source";
					if (std::filesystem::exists(sandboxDir)) {
						auto dst = sandboxDir / finalFileName;
						if (!std::filesystem::exists(dst)) {
							std::ofstream f(dst); if (f.is_open()) { f << boilerplate; f.close(); }
						}
					}
				}
			}
			else {
				std::string boilerplate =
					"#include <Scripting/NativeScript.hpp>\n"
					"\n"
					"class " + className + " : public Bolt::NativeScript {\n"
					"public:\n"
					"    void Start() override\n"
					"    {\n"
					"    }\n"
					"\n"
					"    void Update(float dt) override\n"
					"    {\n"
					"    }\n"
					"\n"
					"    void OnDestroy() override\n"
					"    {\n"
					"    }\n"
					"};\n"
					"REGISTER_SCRIPT(" + className + ")\n";

				std::ofstream file(finalPath);
				if (file.is_open()) { file << boilerplate; file.close(); }

				std::filesystem::path nativeDir = GetNativeSourceDirectory(true);
				if (!nativeDir.empty()) {
					std::error_code ec;
					std::filesystem::create_directories(nativeDir, ec);
					auto dst = nativeDir / finalFileName;
					if (!std::filesystem::exists(dst)) {
						std::ofstream f(dst); if (f.is_open()) { f << boilerplate; f.close(); }
					}
				}
			}

			m_SelectedPath = finalPath;
			m_PendingScriptType = PendingScriptType::None;
			m_PendingScriptDir.clear();
			m_NeedsRefresh = true;
			CancelRename();
			return;
		}

		if (!newName.empty() && newName != oldName) {
			RenameEntry(m_RenamePath, newName);
		}
		CancelRename();
	}

	void AssetBrowser::CancelRename() {
		if (m_PendingScriptType != PendingScriptType::None) {
			if (!m_RenamePath.empty() && std::filesystem::exists(m_RenamePath)) {
				std::error_code ec;
				std::filesystem::remove(m_RenamePath, ec);
			}
			m_PendingScriptType = PendingScriptType::None;
			m_PendingScriptDir.clear();
			m_NeedsRefresh = true;
		}

		m_IsRenaming = false;
		m_RenamePath.clear();
		m_RenameFrameCounter = 0;
	}

	bool AssetBrowser::IsRenamingEntry(const std::string& path) const {
		return m_IsRenaming && m_RenamePath == path;
	}

	bool AssetBrowser::BeginRenameSelected() {
		if (m_SelectedPath.empty() || m_IsRenaming) {
			return false;
		}

		std::error_code ec;
		if (!std::filesystem::exists(m_SelectedPath, ec) || ec) {
			return false;
		}

		BeginRename(m_SelectedPath, std::filesystem::path(m_SelectedPath).filename().string());
		return true;
	}

	void AssetBrowser::DeleteEntry(const std::string& path) {
		m_Thumbnails.Invalidate(path);
		const std::filesystem::path nativeMirrorPath = ResolveNativeScriptMirrorPath(path);

		if (Directory::Delete(path)) {
			if (!nativeMirrorPath.empty()) {
				std::error_code ec;
				std::filesystem::remove(nativeMirrorPath, ec);
			}

			if (m_SelectedPath == path) {
				m_SelectedPath.clear();
			}
			if (m_PressedPath == path) {
				m_PressedPath.clear();
			}
			CancelRename();
			m_NeedsRefresh = true;
		}
	}

	void AssetBrowser::RenameEntry(const std::string& path, const std::string& newName) {
		m_Thumbnails.Invalidate(path);

		std::string oldExt = std::filesystem::path(path).extension().string();
		std::string oldStem = std::filesystem::path(path).stem().string();

		if (Directory::Rename(path, newName)) {
			std::filesystem::path p(path);
			std::string newPath = (p.parent_path() / newName).string();
			if (m_SelectedPath == path) {
				m_SelectedPath = newPath;
			}

			std::string ext = std::filesystem::path(newName).extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			std::string newStem = std::filesystem::path(newName).stem().string();
			const bool oldIsNativeScript = IsNativeScriptSourceExtension(oldExt);
			const bool newIsNativeScript = IsNativeScriptSourceExtension(ext);

			std::filesystem::path projectSourceDir;
			BoltProject* project = ProjectManager::GetCurrentProject();
			if (oldIsNativeScript || newIsNativeScript)
			{
				projectSourceDir = GetNativeSourceDirectory(newIsNativeScript);
			}
			else if (ext == ".cs")
			{
				if (project)
					projectSourceDir = project->ScriptsDirectory;
				else
					projectSourceDir = std::filesystem::path(Path::ExecutableDir()) / ".." / ".." / ".." / "Bolt-Sandbox" / "Source";
			}

			if (!projectSourceDir.empty() && (std::filesystem::exists(projectSourceDir) || newIsNativeScript))
			{
				if (newIsNativeScript) {
					std::error_code ec;
					std::filesystem::create_directories(projectSourceDir, ec);
				}

				auto oldProjectFile = projectSourceDir / (oldStem + oldExt);
				auto newProjectFile = projectSourceDir / newName;

				if (oldIsNativeScript && !newIsNativeScript)
				{
					std::error_code ec;
					std::filesystem::remove(oldProjectFile, ec);
				}
				else if (newIsNativeScript)
				{
					if (std::filesystem::exists(oldProjectFile))
					{
						std::error_code ec;
						std::filesystem::rename(oldProjectFile, newProjectFile, ec);
					}
					else if (std::filesystem::exists(newPath) && !std::filesystem::exists(newProjectFile))
					{
						std::error_code ec;
						std::filesystem::copy_file(newPath, newProjectFile, std::filesystem::copy_options::none, ec);
					}

					if (std::filesystem::exists(newProjectFile))
					{
						std::ifstream in(newProjectFile);
						std::string content((std::istreambuf_iterator<char>(in)),
							std::istreambuf_iterator<char>());
						in.close();

						size_t pos = 0;
						while ((pos = content.find(oldStem, pos)) != std::string::npos)
						{
							content.replace(pos, oldStem.size(), newStem);
							pos += newStem.size();
						}

						std::ofstream out(newProjectFile);
						out << content;
					}
				}
			}

			m_NeedsRefresh = true;
		}
	}

	void AssetBrowser::CopyPathToClipboard(const std::string& path) {
		ImGui::SetClipboardText(path.c_str());
	}

	void AssetBrowser::CreateFolder(const std::string& parentDir) {
		std::string baseName = "New Folder";
		std::string folderPath = (std::filesystem::path(parentDir) / baseName).string();
		int counter = 1;
		while (Directory::Exists(folderPath)) {
			folderPath = (std::filesystem::path(parentDir) / (baseName + " " + std::to_string(counter))).string();
			counter++;
		}

		Directory::Create(folderPath, false);
		m_NeedsRefresh = true;

		Refresh();

		m_SelectedPath = folderPath;
		std::string name = std::filesystem::path(folderPath).filename().string();
		BeginRename(folderPath, name);
	}

	void AssetBrowser::CreateScript(const std::string& parentDir) {
		std::string baseName = "NewScript";
		std::string ext = ".cs";
		std::string scriptPath = (std::filesystem::path(parentDir) / (baseName + ext)).string();
		int counter = 1;
		while (std::filesystem::exists(scriptPath)) {
			scriptPath = (std::filesystem::path(parentDir) / (baseName + std::to_string(counter) + ext)).string();
			counter++;
		}

		m_PendingScriptType = PendingScriptType::CSharp;
		m_PendingScriptDir = parentDir;

		m_NeedsRefresh = true;
		Refresh();

		m_SelectedPath = scriptPath;
		std::string name = std::filesystem::path(scriptPath).stem().string();
		BeginRename(scriptPath, name);
	}

	void AssetBrowser::CreateNativeScript(const std::string& parentDir) {
		std::string baseName = "NewNativeScript";
		std::string ext = ".cpp";
		std::string scriptPath = (std::filesystem::path(parentDir) / (baseName + ext)).string();
		int counter = 1;
		while (std::filesystem::exists(scriptPath)) {
			scriptPath = (std::filesystem::path(parentDir) / (baseName + std::to_string(counter) + ext)).string();
			counter++;
		}

		m_PendingScriptType = PendingScriptType::Native;
		m_PendingScriptDir = parentDir;

		m_NeedsRefresh = true;
		Refresh();

		m_SelectedPath = scriptPath;
		std::string name = std::filesystem::path(scriptPath).stem().string();
		BeginRename(scriptPath, name);
	}

	void AssetBrowser::CreateScene(const std::string& parentDir) {
		std::string baseName = "NewScene";
		std::string ext = ".scene";

		auto sceneNameTaken = [&](const std::string& name) -> bool {
			try {
				for (auto& entry : std::filesystem::recursive_directory_iterator(
					m_RootDirectory, std::filesystem::directory_options::skip_permission_denied)) {
					if (entry.is_regular_file() && entry.path().extension() == ext
						&& entry.path().stem().string() == name)
						return true;
				}
			}
			catch (...) {}
			return false;
		};

		std::string sceneName = baseName;
		std::string scenePath = (std::filesystem::path(parentDir) / (sceneName + ext)).string();
		int counter = 1;
		while (sceneNameTaken(sceneName)) {
			sceneName = baseName + std::to_string(counter++);
			scenePath = (std::filesystem::path(parentDir) / (sceneName + ext)).string();
		}

		std::string content =
			"{\n"
			"  \"name\": \"" + sceneName + "\",\n"
			"  \"entities\": [\n"
			"    {\n"
			"      \"name\": \"Camera\",\n"
			"      \"Transform2D\": { \"posX\": 0, \"posY\": 0, \"rotation\": 0, \"scaleX\": 1, \"scaleY\": 1 },\n"
			"      \"Camera2D\": { \"orthoSize\": 5, \"zoom\": 1 }\n"
			"    }\n"
			"  ]\n"
			"}\n";

		std::ofstream file(scenePath);
		if (file.is_open()) {
			file << content;
			file.close();
		}

		m_NeedsRefresh = true;
		Refresh();

		m_SelectedPath = scenePath;
		std::string name = std::filesystem::path(scenePath).filename().string();
		BeginRename(scenePath, name);
	}

	void AssetBrowser::OpenAssetExternal(const DirectoryEntry& entry) {
		try
		{
			std::string ext = std::filesystem::path(entry.Path).extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

			if (ext == ".scene")
			{
				m_PendingSceneLoad = entry.Path;
				return;
			}

			if (ExternalEditor::IsScriptExtension(ext))
			{
				std::filesystem::path scriptPath = entry.Path;
				const std::filesystem::path nativeMirrorPath = ResolveNativeScriptMirrorPath(entry.Path);
				if (!nativeMirrorPath.empty() && std::filesystem::exists(nativeMirrorPath)) {
					scriptPath = nativeMirrorPath;
				}
				ExternalEditor::OpenFile(scriptPath.string());
				return;
			}

#ifdef BT_PLATFORM_WINDOWS
			std::wstring wpath = std::filesystem::absolute(entry.Path).wstring();
			std::thread([wpath]() {
				CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				CoUninitialize();
			}).detach();
#endif
		}
		catch (...) {}
	}

	void AssetBrowser::OnExternalFileDrop(const std::vector<std::string>& paths) {
		if (m_CurrentDirectory.empty()) return;

		int imported = 0;
		for (const auto& sourcePath : paths) {
			try {
				std::filesystem::path src(sourcePath);
				if (!std::filesystem::exists(src)) continue;

				std::filesystem::path destDir(m_CurrentDirectory);

				if (std::filesystem::is_directory(src)) {
					std::filesystem::path dest = destDir / src.filename();
					std::filesystem::copy(src, dest,
						std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
					imported++;
				}
				else {
					std::filesystem::path dest = destDir / src.filename();

					if (std::filesystem::exists(dest)) {
						std::string stem = dest.stem().string();
						std::string ext = dest.extension().string();
						int counter = 1;
						while (std::filesystem::exists(dest)) {
							dest = destDir / (stem + " (" + std::to_string(counter) + ")" + ext);
							counter++;
						}
					}

					std::filesystem::copy_file(src, dest);
					imported++;
				}
			}
			catch (const std::exception& e) {
				BT_CORE_WARN_TAG("AssetBrowser", "Failed to import '{}': {}", sourcePath, e.what());
			}
		}

		if (imported > 0) {
			m_NeedsRefresh = true;
			BT_CORE_INFO_TAG("AssetBrowser", "Imported {} file(s) into {}", imported, m_CurrentDirectory);
		}
	}

}
