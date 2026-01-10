#include "Storage.h"
#include <Windows.h>
#include <shlobj.h>
#include <fstream>

#include "../Assert/Assert.h"
#include "../Config/Config.h"

bool AssureDirectory(const std::filesystem::path& path)
{
	if (!exists(path))
	{
		return create_directories(path);
	}

	return true;
}

void CStorage::Init(const std::string& folderName)
{
	// Necromancer saves to C:\necromancer_tf2
	m_WorkFolder = "C:\\necromancer_tf2";
	Assert(AssureDirectory(m_WorkFolder))

	m_ConfigFolder = m_WorkFolder / "Configs";
	Assert(AssureDirectory(m_ConfigFolder))

	m_AutosaveFolder = m_WorkFolder / "Autosave";
	Assert(AssureDirectory(m_AutosaveFolder))

	// Settings file for storing options like "load autosave on inject"
	m_SettingsFile = m_WorkFolder / "settings.txt";

	// Legacy seonwdde config folder for loading old configs
	m_LegacyConfigFolder = std::filesystem::current_path() / "SEOwnedDE" / "Configs";
	
	// Schedule autosave load if enabled (will load after 1.5 second delay)
	if (GetLoadAutosaveOnInject())
	{
		ScheduleAutosaveLoad();
	}
}

void CStorage::Update()
{
	// Process delayed autosave load
	if (m_bPendingAutosaveLoad)
	{
		m_nAutosaveLoadFrames++;
		
		// Wait ~100 frames (~1.5 seconds at 66 tick) before loading
		if (m_nAutosaveLoadFrames >= 100)
		{
			m_bPendingAutosaveLoad = false;
			
			// Load the autosave
			auto autosavePath = m_AutosaveFolder / "autosave_1.json";
			if (std::filesystem::exists(autosavePath))
			{
				Config::Load(autosavePath);
			}
		}
	}
}

void CStorage::ScheduleAutosaveLoad()
{
	m_bPendingAutosaveLoad = true;
	m_nAutosaveLoadFrames = 0;
}

bool CStorage::HasLegacyConfigs()
{
	if (!std::filesystem::exists(m_LegacyConfigFolder))
		return false;

	for (const auto& entry : std::filesystem::directory_iterator(m_LegacyConfigFolder))
	{
		if (entry.path().extension() == ".json")
			return true;
	}
	return false;
}

void CStorage::DoAutosave()
{
	// Rotate autosaves: 4->5, 3->4, 2->3, 1->2, then save new as 1
	for (int i = 4; i >= 1; i--)
	{
		std::string srcFile = "autosave_" + std::to_string(i) + ".json";
		std::string dstFile = "autosave_" + std::to_string(i + 1) + ".json";
		
		auto srcPath = m_AutosaveFolder / srcFile;
		auto dstPath = m_AutosaveFolder / dstFile;
		
		if (std::filesystem::exists(srcPath))
		{
			std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
		}
	}
	
	// Save current config as autosave_1
	Config::Save(m_AutosaveFolder / "autosave_1.json");
}

void CStorage::LoadAutosave(int slot)
{
	if (slot < 1 || slot > 5)
		return;
	
	std::string fileName = "autosave_" + std::to_string(slot) + ".json";
	auto path = m_AutosaveFolder / fileName;
	
	if (std::filesystem::exists(path))
	{
		Config::Load(path);
	}
}

bool CStorage::GetLoadAutosaveOnInject()
{
	if (!std::filesystem::exists(m_SettingsFile))
		return false;
	
	std::ifstream file(m_SettingsFile);
	if (!file.is_open())
		return false;
	
	std::string line;
	while (std::getline(file, line))
	{
		if (line.find("load_autosave_on_inject=") == 0)
		{
			std::string value = line.substr(24); // Length of "load_autosave_on_inject="
			file.close();
			return value == "1" || value == "true";
		}
	}
	
	file.close();
	return false;
}

void CStorage::SetLoadAutosaveOnInject(bool bEnabled)
{
	std::ofstream file(m_SettingsFile);
	if (!file.is_open())
		return;
	
	file << "load_autosave_on_inject=" << (bEnabled ? "1" : "0") << std::endl;
	file.close();
}
