#include "ui/Menu.h"
#include "vm/VM.h"
#include "Compatibility.h"

#include <spdlog/sinks/basic_file_sink.h>

SKSEPluginInfo(.Version = REL::Version{0, 1, 0, 0}, .Name = "PapyrusConsole"sv, .Author = "Rafael Bahiense"sv,
               .SupportEmail = ""sv, .StructCompatibility = SKSE::StructCompatibility::Independent,
               .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
               .MinimumSKSEVersion = REL::Version{0, 0, 0, 0})

    namespace {
    void OnMessage(SKSE::MessagingInterface::Message * message) {
        switch (message->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                papyrus_console::RegisterMenu();
                break;
            case SKSE::MessagingInterface::kPreLoadGame:
                papyrus_console::SessionChanged(false);
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
                papyrus_console::SessionChanged(message->data != nullptr);
                break;
            case SKSE::MessagingInterface::kNewGame:
                papyrus_console::SessionChanged(true);
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
        *directory /= "PapyrusConsole.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(directory->string(), true);
        auto logger = std::make_shared<spdlog::logger>("PapyrusConsole", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }
    if (!papyrus_console::IsTestedRuntime())
        SKSE::log::warn("{}", papyrus_console::UntestedRuntimeWarning);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage))
        return false;
    SKSE::log::info("Papyrus Console loaded");
    return true;
}
