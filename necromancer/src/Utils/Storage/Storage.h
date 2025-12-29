#pragma once
#include <filesystem>

#include "../Singleton/Singleton.h"

class CStorage
{
	std::filesystem::path m_WorkFolder;
	std::filesystem::path m_ConfigFolder;
	std::filesystem::path m_AutosaveFolder;
	std::filesystem::path m_SettingsFile; // For storing settings like "load autosave on inject"
	std::filesystem::path m_LegacyConfigFolder; // For loading old seonwdde configs
	
	// Delayed autosave load (uses frame counter since Storage doesn't have SDK access)
	bool m_bPendingAutosaveLoad = false;
	int m_nAutosaveLoadFrames = 0; // Count frames until load

public:
	void Init(const std::string& folderName);
	void Update(); // Call every frame to process delayed loads

	const std::filesystem::path& GetWorkFolder()
	{
		return m_WorkFolder;
	}

	const std::filesystem::path& GetConfigFolder()
	{
		return m_ConfigFolder;
	}

	const std::filesystem::path& GetAutosaveFolder()
	{
		return m_AutosaveFolder;
	}

	const std::filesystem::path& GetLegacyConfigFolder()
	{
		return m_LegacyConfigFolder;
	}

	bool HasLegacyConfigs();
	
	// Autosave functionality
	void DoAutosave();
	void LoadAutosave(int slot); // 1-5, where 1 is latest
	
	// Settings file (separate from config, loaded before config)
	bool GetLoadAutosaveOnInject();
	void SetLoadAutosaveOnInject(bool bEnabled);
	void ScheduleAutosaveLoad(); // Schedule load after delay
};

MAKE_SINGLETON_SCOPED(CStorage, Storage, U);