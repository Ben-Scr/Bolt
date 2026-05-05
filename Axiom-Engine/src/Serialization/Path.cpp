#include <pch.hpp>
#include "Path.hpp"

#ifdef AIM_PLATFORM_WINDOWS
#include <windows.h>
#include <shlobj.h>
#else
#include <cstdlib>
#include <unistd.h>
#include <limits.h>
#endif

namespace Axiom {
    std::string Path::GetSpecialFolderPath(SpecialFolder folder)
    {
#ifdef AIM_PLATFORM_WINDOWS
        PWSTR path = nullptr;
        HRESULT hr = E_FAIL;

        switch (folder) {
        case SpecialFolder::User: hr = SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path); break;
        case SpecialFolder::Desktop: hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path); break;
        case SpecialFolder::Documents: hr = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path); break;
        case SpecialFolder::Downloads: hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path); break;
        case SpecialFolder::Music: hr = SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &path); break;
        case SpecialFolder::Pictures: hr = SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path); break;
        case SpecialFolder::Videos: hr = SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &path); break;
        case SpecialFolder::AppDataRoaming: hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path); break;
        case SpecialFolder::LocalAppData: hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path); break;
        case SpecialFolder::ProgramData: hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &path); break;
        default: AIM_THROW(AxiomErrorCode::InvalidArgument, "Unknown SpecialFolder");
        }

        AIM_ASSERT(!FAILED(hr), AxiomErrorCode::Undefined, "Failed to get folder path");

        const int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &result[0], size, nullptr, nullptr);

        CoTaskMemFree(path);
        return result;
#else
        const char* home = std::getenv("HOME");
        if (!home)
            AIM_THROW(AxiomErrorCode::Undefined, "HOME environment variable is not set");

        switch (folder) {
        case SpecialFolder::User: return home;
        case SpecialFolder::Desktop: return Combine(home, "Desktop");
        case SpecialFolder::Documents: return Combine(home, "Documents");
        case SpecialFolder::Downloads: return Combine(home, "Downloads");
        case SpecialFolder::Music: return Combine(home, "Music");
        case SpecialFolder::Pictures: return Combine(home, "Pictures");
        case SpecialFolder::Videos: return Combine(home, "Videos");
        case SpecialFolder::AppDataRoaming:
        case SpecialFolder::LocalAppData: {
            const char* xdg = std::getenv("XDG_CONFIG_HOME");
            return (xdg && *xdg) ? std::string(xdg) : Combine(home, ".config");
        }
        case SpecialFolder::ProgramData:
            return "/usr/share";
        default:
            AIM_THROW(AxiomErrorCode::InvalidArgument, "Unknown SpecialFolder");
        }
#endif
    }

    std::string Path::ExecutableDir()
    {
#ifdef AIM_PLATFORM_WINDOWS
        // Grow the buffer until the path fits — silent truncation at MAX_PATH would
        // produce a wrong directory on long-installation paths (the registry-key value
        // is `LongPathsEnabled = 1` in modern Windows, paths of 1000+ chars are real).
        std::vector<wchar_t> buf(MAX_PATH);
        for (;;) {
            DWORD copied = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (copied == 0) {
                AIM_THROW(AxiomErrorCode::Undefined, "GetModuleFileNameW failed");
            }
            if (copied < buf.size()) break; // fit
            // Buffer was too small — Windows null-terminates and returns size on truncation.
            buf.resize(buf.size() * 2);
            if (buf.size() > 65536) {
                AIM_THROW(AxiomErrorCode::Undefined, "Executable path > 64KB; refusing");
            }
        }
        return std::filesystem::path(buf.data()).parent_path().string();
#else
        char buf[PATH_MAX]{};
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0)
            AIM_THROW(AxiomErrorCode::Undefined, "Failed to resolve executable path");
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path().string();
#endif
    }

    std::string Path::ResolveAxiomAssets(const std::string& subdirectory) {
        std::string exeDir = ExecutableDir();

        // 1. Packaged build: AxiomAssets next to the executable
        std::string packed = Combine(exeDir, "AxiomAssets", subdirectory);
        if (std::filesystem::exists(packed))
            return packed;

        // 2. Dev layout: shared AxiomAssets one level up (bin/<config>/AxiomAssets/)
        std::string dev = Combine(exeDir, "..", "AxiomAssets", subdirectory);
        if (std::filesystem::exists(dev))
            return std::filesystem::canonical(dev).string();

        return {};
    }
}