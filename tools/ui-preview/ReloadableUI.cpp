#include "ReloadableUI.h"
#include <fstream>

namespace preview {
    ReloadableUI::ReloadableUI() {
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (length && length < std::size(executable))
            directory = std::filesystem::path(executable).parent_path();
    }

    ReloadableUI::~ReloadableUI() {
        Close();
    }

    bool ReloadableUI::Load(HWND window, ID3D11Device* device, ID3D11DeviceContext* context,
                            const std::filesystem::path& source) {
        if (directory.empty())
            return false;
        std::error_code error;
        const auto copies = directory / "live";
        std::filesystem::create_directories(copies, error);
        const auto candidatePath = copies / ("preview-ui-" + std::to_string(GetCurrentProcessId()) + "-" +
                                             std::to_string(++sequence) + ".dll");
        const auto input = source.empty() ? directory / "eldershell-preview-ui.dll" : source;
        if (!std::filesystem::copy_file(input, candidatePath, std::filesystem::copy_options::overwrite_existing,
                                        error)) {
            session.reloadStatus = "UI reload failed; keeping the current menu.";
            return false;
        }
        const auto candidate = LoadLibraryW(candidatePath.c_str());
        const auto query = candidate ? reinterpret_cast<QueryUI>(GetProcAddress(candidate, "Preview_GetUI")) : nullptr;
        const auto* next = query ? query() : nullptr;
        if (!next || next->version != 1 || next->sessionSize != sizeof(Session) ||
            next->menuStateSize != sizeof(elder_shell::MenuSession) || !next->attach || !next->detach || !next->frame ||
            !next->message || !next->smoke) {
            if (candidate)
                FreeLibrary(candidate);
            std::filesystem::remove(candidatePath, error);
            session.reloadStatus = "UI module is incompatible; restart the preview after rebuilding the host.";
            return false;
        }
        // Swap between frames on the window thread. Retain the old DLL until
        // initialization succeeds so a failed candidate can be rolled back.
        if (api)
            api->detach();
        if (!next->attach(window, device, context, &session)) {
            if (api && !api->attach(window, device, context, &session)) {
                api = nullptr;
                session.reloadStatus = "UI reload and recovery failed; restart the preview.";
            } else {
                session.reloadStatus = "UI initialization failed; restored the previous menu.";
            }
            FreeLibrary(candidate);
            std::filesystem::remove(candidatePath, error);
            return false;
        }
        if (module)
            FreeLibrary(module);
        if (!loadedCopy.empty())
            std::filesystem::remove(loadedCopy, error);
        module = candidate;
        api = next;
        loadedCopy = candidatePath;
        session.reloadStatus = sequence > 1 ? "UI reloaded; console session preserved." : "Watching UI sources";
        revision = std::filesystem::last_write_time(directory / "ui-ready", error);
        return true;
    }

    void ReloadableUI::Poll(HWND window, ID3D11Device* device, ID3D11DeviceContext* context) {
        const auto now = std::chrono::steady_clock::now();
        if (now < nextPoll)
            return;
        nextPoll = now + std::chrono::milliseconds(500);
        std::error_code error;
        const auto status = directory / "ui-status.txt";
        const auto statusTime = std::filesystem::last_write_time(status, error);
        if (!error && statusTime != statusRevision) {
            std::ifstream file(status);
            std::getline(file, session.reloadStatus);
            statusRevision = statusTime;
        }
        const auto ready = std::filesystem::last_write_time(directory / "ui-ready", error);
        if (!error && ready != revision) {
            revision = ready;
            Load(window, device, context);
        }
    }

    void ReloadableUI::Close() {
        if (api)
            api->detach();
        api = nullptr;
        if (module)
            FreeLibrary(module);
        module = nullptr;
        if (!loadedCopy.empty()) {
            std::error_code error;
            std::filesystem::remove(loadedCopy, error);
            loadedCopy.clear();
        }
    }
}
