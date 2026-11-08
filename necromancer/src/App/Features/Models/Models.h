#pragma once

#include <set>
#include <string>
#include <vector>

#include "../../../SDK/SDK.h"

class CModels
{
private:
	bool m_bMounted = false;
	bool m_bFlushPending = false;
	bool m_bGlovesApplied = false;
	bool m_bSkyNamesDirty = true;
	std::set<std::string> m_setLooseNames;
	std::vector<std::string> m_vecSkyNames;
	std::vector<std::string> m_vecMountedPaths;

	int ExtractVPKs();
	void UnmountUserFolder();
	int ScanAndMountPacks();
public:
	void Init();
	void OnLevelInit();
	void OnLevelShutdown();
	bool ConsumePendingFlush();
	void MountUserFolder();
	void ApplyGloves();
	void UpdateMaterialFilter();
	void Refresh();
	const std::vector<std::string> &GetSkyNames();
};

MAKE_SINGLETON_SCOPED(CModels, Models, F);
