#pragma once

#include "../../../SDK/SDK.h"
#include <unordered_map>
#include <cstdint>

class CSpectatorList
{
	struct Spectator_t
	{
		std::wstring Name = {};
		const char* m_sMode = "1st";  // "1st", "3rd", "possible"
		int m_nEntIndex = 0;
		float m_flRespawnIn = -1.f;   // Respawn countdown (-1 = not applicable)
		bool m_bRespawnTimeIncreased = false;  // Flag for respawn manipulation
		uint32_t m_nFriendsID = 0;  // Steam friends ID for avatar
	};

	std::vector<Spectator_t> m_vecSpectators = {};
	std::unordered_map<int, float> m_mRespawnCache = {};  // Cache for respawn time tracking

	bool GetSpectators(C_TFPlayer* pTarget);
	void Drag();

public:
	void Run();
};

MAKE_SINGLETON_SCOPED(CSpectatorList, SpectatorList, F);
