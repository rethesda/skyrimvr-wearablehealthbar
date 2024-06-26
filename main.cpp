#include "SKSE/API.h"
#include "SKSE/Impl/Stubs.h"
#include "Windows.h"
#include "external/higgsinterface001.h"
#include "main_plugin.h"  // main source for plugin game logic

#include <spdlog/sinks/basic_file_sink.h>

void    MessageListener(SKSE::MessagingInterface::Message* message);
void    OnPapyrusVRMessage(SKSE::MessagingInterface::Message* message);
void    SetupLog();
uint8_t GetPluginID();

// Interfaces for communicating with other SKSE plugins.
static SKSE::detail::SKSEMessagingInterface* g_messaging;
static SKSE::PluginHandle                    g_pluginHandle = 0xFFFFFFFF;
PapyrusVRAPI*                                g_papyrusvr;
bool                                         g_pluginError = false;

void InitializeHooking()
{
	auto& trampoline = SKSE::GetTrampoline();
	trampoline.create(128);
}

// Main plugin entry point.
SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();

	SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

	g_pluginHandle = skse->GetPluginHandle();
	g_messaging = (SKSE::detail::SKSEMessagingInterface*)skse->QueryInterface(
		SKSE::LoadInterface::kMessaging);
	wearable_plugin::g_task =
		(SKSE::detail::SKSETaskInterface*)(skse->QueryInterface(SKSE::LoadInterface::kTask));

	return true;
}

// Receives messages about the game's state that SKSE broadcasts to all plugins.
void MessageListener(SKSE::MessagingInterface::Message* message)
{
	using namespace SKSE::log;

	switch (message->type)
	{
	case SKSE::MessagingInterface::kPostLoad:
		// trace("kPostLoad: sent to registered plugins once all plugins have been loaded");
		info("Registering for SkyrimVRTools messages");
		g_messaging->RegisterListener(g_pluginHandle, "SkyrimVRTools", OnPapyrusVRMessage);
		break;

	case SKSE::MessagingInterface::kPostPostLoad:
		info("kPostPostLoad: querying higgs interface");
		g_higgsInterface = HiggsPluginAPI::GetHiggsInterface001(g_pluginHandle, g_messaging);
		if (g_higgsInterface)
		{
			info("Got higgs interface");

		} else
		{
			critical("Plugin disabled: HIGGS interface not found");
			//g_pluginError = true;
		}

		info("kPostPostLoad: querying VRIK interface");
		g_vrikInterface = vrikPluginApi::getVrikInterface001(g_pluginHandle, g_messaging);
		if (g_vrikInterface)
		{
			info("Got VRIK interface");
		} else
		{
			critical("Plugin disabled: VRIK interface not found");
			g_pluginError = true;
		}

		break;

	case SKSE::MessagingInterface::kInputLoaded:
		// trace("kInputLoaded: sent right after game input is loaded, right before the main menu initializes");
		break;

	case SKSE::MessagingInterface::kDataLoaded:
		// trace("kDataLoaded: sent after the data handler has loaded all its forms");
		InitializeHooking();
		// Initialize our mod.
		if (!g_pluginError) wearable_plugin::StartMod();
		break;

	case SKSE::MessagingInterface::kPreLoadGame:
		// trace("kPreLoadGame: sent immediately before savegame is read");
		if (!g_pluginError) wearable_plugin::PreGameLoad();
		break;

	case SKSE::MessagingInterface::kPostLoadGame:
		// trace("kPostLoadGame: sent after an attempt to load a saved game has finished");
		if (!g_pluginError) wearable_plugin::GameLoad();
		break;

	case SKSE::MessagingInterface::kSaveGame:
		// trace("kSaveGame");
		break;

	case SKSE::MessagingInterface::kDeleteGame:
		// trace("kDeleteGame: sent right before deleting the .skse cosave and the .ess save");
		break;

	case SKSE::MessagingInterface::kNewGame:
		// trace("kNewGame: sent after a new game is created, before the game has loaded");
		break;

	default:
		// trace("Unknown system message of type: {}", message->type);
		break;
	}
}

// Listener for papyrusvr Messages
void OnPapyrusVRMessage(SKSE::MessagingInterface::Message* message)
{
	if (message)
	{
		if (message->type == kPapyrusVR_Message_Init && message->data)
		{
			SKSE::log::info(
				"SkyrimVRTools Init Message recived with valid data, registering for "
				"callback");
			g_papyrusvr = (PapyrusVRAPI*)message->data;

			wearable_plugin::g_VRManager = g_papyrusvr->GetVRManager();
			wearable_plugin::g_OVRHookManager = g_papyrusvr->GetOpenVRHook();
		}
	}
}

// Initialize logging system.
void SetupLog()
{
	auto logsFolder = SKSE::log::log_directory();
	if (!logsFolder)
	{
		SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
		return;
	}
	auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
	auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
	auto fileLoggerPtr =
		std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
	auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
	spdlog::set_default_logger(std::move(loggerPtr));
	spdlog::set_level(spdlog::level::trace);
	spdlog::flush_on(spdlog::level::trace);
}
