#include "../../../SDK/SDK.h"
#include "../CFG.h"
#include "../Players/Players.h"

#include <fstream>
#include <vector>
#include <random>
#include <filesystem>
#include <Windows.h>
#include <ShlObj.h>

// Chat folder path
static std::filesystem::path g_ChatFolder = "C:\\necromancer\\chat";
static std::filesystem::path g_ChatSpammerFile = g_ChatFolder / "chatspammer.txt";
static std::filesystem::path g_KillsayFile = g_ChatFolder / "killsay.txt";

// Cached messages
static std::vector<std::string> g_vecChatSpammerMessages;
static std::vector<std::string> g_vecKillsayMessages;
static bool g_bFilesInitialized = false;

// Chat Spammer
static float g_flLastSpamTime = 0.0f;
static size_t g_nSpamIndex = 0;

// Random number generator
static std::random_device g_rd;
static std::mt19937 g_rng(g_rd());

// Initialize chat folder and files
void InitChatFiles()
{
	if (g_bFilesInitialized)
		return;

	g_bFilesInitialized = true;

	// Create folder if it doesn't exist
	if (!std::filesystem::exists(g_ChatFolder))
	{
		std::filesystem::create_directories(g_ChatFolder);
	}

	// Create default chatspammer.txt if it doesn't exist
	if (!std::filesystem::exists(g_ChatSpammerFile))
	{
		std::ofstream file(g_ChatSpammerFile);
		if (file.is_open())
		{
			file << "loser lost\n";
			file << "i win hahahaha\n";
			file << "you are noob\n";
			file.close();
		}
	}

	// Create default killsay.txt if it doesn't exist
	if (!std::filesystem::exists(g_KillsayFile))
	{
		std::ofstream file(g_KillsayFile);
		if (file.is_open())
		{
			file << "loser lost\n";
			file << "i win hahahaha\n";
			file << "you are noob\n";
			file.close();
		}
	}
}

// Load messages from a text file
std::vector<std::string> LoadMessagesFromFile(const std::filesystem::path& path)
{
	std::vector<std::string> messages;
	
	std::ifstream file(path);
	if (!file.is_open())
		return messages;

	std::string line;
	while (std::getline(file, line))
	{
		// Skip empty lines
		if (!line.empty())
		{
			messages.push_back(line);
		}
	}

	file.close();
	return messages;
}

// Reload chat spammer messages
void ReloadChatSpammerMessages()
{
	InitChatFiles();
	g_vecChatSpammerMessages = LoadMessagesFromFile(g_ChatSpammerFile);
	g_nSpamIndex = 0;
}

// Reload killsay messages
void ReloadKillsayMessages()
{
	InitChatFiles();
	g_vecKillsayMessages = LoadMessagesFromFile(g_KillsayFile);
}

// Open text files folder on desktop
void OpenChatTextFiles()
{
	InitChatFiles();
	
	// Open the chat folder in explorer
	ShellExecuteW(NULL, L"explore", g_ChatFolder.wstring().c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void RunChatSpammer()
{
	if (!CFG::Misc_Chat_Spammer_Active)
		return;

	if (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame())
		return;

	// Load messages if not loaded
	if (g_vecChatSpammerMessages.empty())
	{
		ReloadChatSpammerMessages();
		if (g_vecChatSpammerMessages.empty())
			return;
	}

	float flCurrentTime = I::GlobalVars->realtime;
	if (flCurrentTime - g_flLastSpamTime < CFG::Misc_Chat_Spammer_Interval)
		return;

	g_flLastSpamTime = flCurrentTime;

	// Get random message
	std::uniform_int_distribution<size_t> dist(0, g_vecChatSpammerMessages.size() - 1);
	const std::string& message = g_vecChatSpammerMessages[dist(g_rng)];

	std::string cmd = "say \"" + message + "\"";
	I::EngineClient->ClientCmd_Unrestricted(cmd.c_str());
}

// Killsay - called when we kill someone
void OnKill(const char* victimName, int victimEntIndex)
{
	if (!CFG::Misc_Chat_Killsay_Active)
		return;

	// Check if tagged only is enabled
	if (CFG::Misc_Chat_Killsay_Tagged_Only && victimEntIndex > 0)
	{
		PlayerPriority priority = {};
		if (!F::Players->GetInfo(victimEntIndex, priority))
			return; // Not tagged, skip

		// Check if player has any tag (Cheater, RetardLegit, or Ignored)
		if (!priority.Cheater && !priority.RetardLegit && !priority.Ignored)
			return; // Not tagged, skip
	}

	// Load messages if not loaded
	if (g_vecKillsayMessages.empty())
	{
		ReloadKillsayMessages();
		if (g_vecKillsayMessages.empty())
			return;
	}

	// Get random message
	std::uniform_int_distribution<size_t> dist(0, g_vecKillsayMessages.size() - 1);
	std::string message = g_vecKillsayMessages[dist(g_rng)];
	
	// Replace {name} with victim's name if present
	size_t pos = message.find("{name}");
	if (pos != std::string::npos && victimName)
	{
		message.replace(pos, 6, victimName);
	}

	std::string cmd = "say \"" + message + "\"";
	I::EngineClient->ClientCmd_Unrestricted(cmd.c_str());
}
