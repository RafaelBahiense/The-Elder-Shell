#pragma once
#include "UIAPI.h"
#include <chrono>
#include <filesystem>

namespace preview {
    class ReloadableUI {
    public:
        ReloadableUI();
        ~ReloadableUI();
        ReloadableUI(const ReloadableUI&) = delete;
        ReloadableUI& operator=(const ReloadableUI&) = delete;
        bool Load(HWND window, ID3D11Device* device, ID3D11DeviceContext* context,
                  const std::filesystem::path& source = {});
        void Poll(HWND window, ID3D11Device* device, ID3D11DeviceContext* context);
        void Close();

        const UIAPI* API() const {
            return api;
        }

    private:
        Session session;
        HMODULE module = nullptr;
        const UIAPI* api = nullptr;
        std::filesystem::path directory, loadedCopy;
        std::filesystem::file_time_type revision{}, statusRevision{};
        std::chrono::steady_clock::time_point nextPoll{};
        unsigned int sequence = 0;
    };
}
