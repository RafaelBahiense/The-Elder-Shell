#include "ui/Menu.h"
#include "vm/VM.h"
#include "Compatibility.h"
#include "ModInfo.h"
#include "Services.h"
#include "PackageFlags.h"

#include <spdlog/sinks/basic_file_sink.h>

SKSEPluginInfo(.Version = REL::Version{mod::info::VersionMajor, mod::info::VersionMinor, mod::info::VersionPatch, 0},
               .Name = mod::info::BinaryName, .Author = mod::info::Author, .SupportEmail = ""sv,
               .StructCompatibility = SKSE::StructCompatibility::Independent,
               .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
               .MinimumSKSEVersion = REL::Version{0, 0, 0, 0})

    namespace {
    void OnMessage(SKSE::MessagingInterface::Message * message) {
        switch (message->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                elder_shell::RegisterMenu();
                break;
            case SKSE::MessagingInterface::kPreLoadGame:
                elder_shell::RestorePackageFlagOverrides();
                elder_shell::SessionChanged(false);
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
                elder_shell::SessionChanged(message->data != nullptr);
                break;
            case SKSE::MessagingInterface::kNewGame:
                elder_shell::RestorePackageFlagOverrides();
                elder_shell::SessionChanged(true);
                break;
            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    auto directory = SKSE::log::log_directory();
    if (directory) {
        *directory /= mod::info::LogFile;
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(directory->string(), true);
        auto logger = std::make_shared<spdlog::logger>(mod::info::BinaryName, std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }
    if (!elder_shell::IsTestedRuntime())
        SKSE::log::warn("{}", elder_shell::UntestedRuntimeWarning);

    auto* messaging = SKSE::GetMessagingInterface();
    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(elder_shell::RegisterServices))
        return false;
    if (!messaging || !messaging->RegisterListener(OnMessage))
        return false;
    SKSE::log::info("{} loaded", mod::info::DisplayName);
    return true;
}
