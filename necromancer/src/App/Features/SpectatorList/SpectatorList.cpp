#include "SpectatorList.h"

#include "../CFG.h"
#include "../Menu/Menu.h"
#include "../VisualUtils/VisualUtils.h"
#include "../Players/Players.h"

#define LIST_WIDTH CFG::Visuals_SpectatorList_Width

bool CSpectatorList::GetSpectators(C_TFPlayer* pTarget)
{
	m_vecSpectators.clear();

	auto pResource = GetTFPlayerResource();
	if (!pResource)
		return false;

	const int iTarget = pTarget->entindex();
	const int iLocalPlayer = I::EngineClient->GetLocalPlayer();

	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(n);
		auto pPlayer = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;
		const bool bLocal = (n == iLocalPlayer);

		// Check for TEAM_SPECTATOR players (like Amalgam)
		if (pResource->IsValid(n) && !pResource->IsFakePlayer(n)
			&& pResource->GetTeam(iLocalPlayer) != TEAM_SPECTATOR && pResource->GetTeam(n) == TEAM_SPECTATOR)
		{
			player_info_t info = {};
			if (I::EngineClient->GetPlayerInfo(n, &info))
			{
				m_vecSpectators.emplace_back(Spectator_t{
					Utils::ConvertUtf8ToWide(info.name),
					"possible",
					n,
					-1.f,
					false,
					static_cast<uint32_t>(info.friendsID)
				});
			}
			continue;
		}

		// Skip conditions (like Amalgam)
		if (pTarget->entindex() == n || pResource->IsFakePlayer(n)
			|| !pPlayer || pEntity->GetClassId() != ETFClassIds::CTFPlayer || !pPlayer->deadflag()
			|| pTarget->IsDormant() != pPlayer->IsDormant()
			|| pResource->GetTeam(iTarget) != pResource->GetTeam(n))
		{
			if (m_mRespawnCache.find(n) != m_mRespawnCache.end())
				m_mRespawnCache.erase(n);
			continue;
		}

		int iObserverTarget = !pPlayer->IsDormant() ? pPlayer->m_hObserverTarget().GetEntryIndex() : iTarget;
		int iObserverMode = pPlayer->m_iObserverMode();

		if (iObserverTarget != iTarget || (bLocal && !I::EngineClient->IsPlayingDemo()))
		{
			if (m_mRespawnCache.find(n) != m_mRespawnCache.end())
				m_mRespawnCache.erase(n);
			continue;
		}

		const char* sMode = "possible";
		if (!pPlayer->IsDormant())
		{
			switch (iObserverMode)
			{
			case OBS_MODE_IN_EYE: sMode = "1st"; break;
			case OBS_MODE_CHASE: sMode = "3rd"; break;
			default: continue;
			}
		}

		float flRespawnTime = 0.f, flRespawnIn = -1.f;
		bool bRespawnTimeIncreased = false;

		// Get respawn time (like Amalgam - always calculate for valid team players)
		const int iPlayerTeam = pResource->GetTeam(n);
		if (iPlayerTeam == TF_TEAM_RED || iPlayerTeam == TF_TEAM_BLUE)
		{
			flRespawnTime = pResource->GetNextRespawnTime(n);
			const float flServerTime = TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick);
			
			// Always calculate respawn time like Amalgam (use floorf)
			flRespawnIn = std::max(floorf(flRespawnTime - flServerTime), 0.f);
			
			if (m_mRespawnCache.find(n) == m_mRespawnCache.end())
				m_mRespawnCache[n] = flRespawnTime;
			else if (m_mRespawnCache[n] + 0.5f < flRespawnTime)
				bRespawnTimeIncreased = true;
		}

		player_info_t info = {};
		if (I::EngineClient->GetPlayerInfo(n, &info))
		{
			m_vecSpectators.emplace_back(Spectator_t{
				Utils::ConvertUtf8ToWide(info.name),
				sMode,
				n,
				flRespawnIn,
				bRespawnTimeIncreased,
				static_cast<uint32_t>(info.friendsID)
			});
		}
	}

	return !m_vecSpectators.empty();
}

void CSpectatorList::Drag()
{
	const int nMouseX = H::Input->GetMouseX();
	const int nMouseY = H::Input->GetMouseY();

	static bool bDragging = false;

	if (!bDragging && F::Menu->IsMenuWindowHovered())
		return;

	static int nDeltaX = 0;
	static int nDeltaY = 0;

	const int nListX = CFG::Visuals_SpectatorList_Pos_X;
	const int nListY = CFG::Visuals_SpectatorList_Pos_Y;

	const bool bHovered = nMouseX > nListX && nMouseX < nListX + LIST_WIDTH && nMouseY > nListY && nMouseY < nListY + CFG::Menu_Drag_Bar_Height;

	if (bHovered && H::Input->IsPressed(VK_LBUTTON))
	{
		nDeltaX = nMouseX - nListX;
		nDeltaY = nMouseY - nListY;
		bDragging = true;
	}

	if (!H::Input->IsPressed(VK_LBUTTON) && !H::Input->IsHeld(VK_LBUTTON))
		bDragging = false;

	if (bDragging)
	{
		CFG::Visuals_SpectatorList_Pos_X = nMouseX - nDeltaX;
		CFG::Visuals_SpectatorList_Pos_Y = nMouseY - nDeltaY;
	}
}

void CSpectatorList::Run()
{
	if (!CFG::Visuals_SpectatorList_Active)
	{
		m_mRespawnCache.clear();
		return;
	}

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;

	if (!F::Menu->IsOpen() && (I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch()))
		return;

	if (F::Menu->IsOpen())
		Drag();

	const auto outlineColor = F::VisualUtils->GetAccentSecondaryWithAlpha(CFG::Visuals_SpectatorList_Outline_Alpha);
	const auto bgColor = F::VisualUtils->GetAlphaColor(CFG::Menu_Background, CFG::Visuals_SpectatorList_Background_Alpha);

	// Background
	H::Draw->Rect(CFG::Visuals_SpectatorList_Pos_X, CFG::Visuals_SpectatorList_Pos_Y, LIST_WIDTH, CFG::Menu_Drag_Bar_Height, bgColor);

	// Title
	H::Draw->String(H::Fonts->Get(EFonts::Menu), CFG::Visuals_SpectatorList_Pos_X + (LIST_WIDTH / 2), CFG::Visuals_SpectatorList_Pos_Y + (CFG::Menu_Drag_Bar_Height / 2), CFG::Menu_Text, POS_CENTERXY, "Spectators");

	// Outline
	H::Draw->OutlinedRect(CFG::Visuals_SpectatorList_Pos_X, CFG::Visuals_SpectatorList_Pos_Y, LIST_WIDTH, CFG::Menu_Drag_Bar_Height, outlineColor);

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	// Get spectate target (like Amalgam)
	auto pTarget = pLocal;
	switch (pLocal->m_iObserverMode())
	{
	case OBS_MODE_IN_EYE:
	case OBS_MODE_CHASE:
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(pLocal->m_hObserverTarget().GetEntryIndex());
		if (pEntity && pEntity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			auto pObserved = pEntity->As<C_TFPlayer>();
			if (pObserved && !pObserved->deadflag())
				pTarget = pObserved;
		}
		break;
	}
	}

	if (!pTarget || !GetSpectators(pTarget))
		return;

	const bool bShowAvatars = CFG::Visuals_SpectatorList_Avatars && I::SteamFriends && I::SteamUtils;
	const int nAvatarSize = CFG::Menu_Drag_Bar_Height - 2;

	for (size_t n = 0; n < m_vecSpectators.size(); n++)
	{
		const auto& spectator = m_vecSpectators[n];
		const int iPos = int(n) + 1;

		// Background
		H::Draw->Rect(CFG::Visuals_SpectatorList_Pos_X, CFG::Visuals_SpectatorList_Pos_Y + (CFG::Menu_Drag_Bar_Height * iPos) - 1, LIST_WIDTH, CFG::Menu_Drag_Bar_Height + 1, bgColor);

		const int nModeX = CFG::Visuals_SpectatorList_Pos_X;
		const int nModeOffsetX = LIST_WIDTH / 8;
		const int nAvatarOffsetX = bShowAvatars ? (nAvatarSize + CFG::Menu_Spacing_X) : 0;
		const int nTextY = CFG::Visuals_SpectatorList_Pos_Y + (CFG::Menu_Drag_Bar_Height * iPos) - 1;
		const int nTextX = nModeX + nModeOffsetX + CFG::Menu_Spacing_X + nAvatarOffsetX;
		const int nY = CFG::Visuals_SpectatorList_Pos_Y + (CFG::Menu_Drag_Bar_Height * iPos) - 1;

		// Divider
		H::Draw->Line(nModeX + nModeOffsetX, nY, nModeX + nModeOffsetX, nY + CFG::Menu_Drag_Bar_Height, outlineColor);

		// Draw avatar
		if (bShowAvatars && spectator.m_nFriendsID != 0)
		{
			const int nAvatarX = nModeX + nModeOffsetX + CFG::Menu_Spacing_X;
			const int nAvatarY = nY + 1;
			H::Draw->Avatar(nAvatarX, nAvatarY, nAvatarSize, nAvatarSize, spectator.m_nFriendsID);
		}

		// Spectator mode
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nModeX + (nModeOffsetX / 2), nTextY + (CFG::Menu_Drag_Bar_Height / 2) + 1, CFG::Menu_Text_Inactive, POS_CENTERXY, spectator.m_sMode);

		I::MatSystemSurface->DisableClipping(false);
		I::MatSystemSurface->SetClippingRect(nModeX, nTextY, (nModeX + LIST_WIDTH) - (CFG::Menu_Spacing_X + 1), nTextY + CFG::Menu_Drag_Bar_Height);

		// Color logic (like Amalgam)
		Color_t nameColor = CFG::Menu_Text_Inactive;
		bool bHasPriorityColor = false;

		PlayerPriority playerPriority = {};
		if (F::Players->GetInfo(spectator.m_nEntIndex, playerPriority))
		{
			if (playerPriority.Cheater)
			{
				nameColor = CFG::Color_Cheater;
				bHasPriorityColor = true;
			}
			else if (playerPriority.RetardLegit)
			{
				nameColor = CFG::Color_RetardLegit;
				bHasPriorityColor = true;
			}
			else if (playerPriority.Ignored)
			{
				nameColor = CFG::Color_Friend;
				bHasPriorityColor = true;
			}
		}

		if (!bHasPriorityColor && spectator.m_bRespawnTimeIncreased)
		{
			nameColor = CFG::Color_Cheater;
			bHasPriorityColor = true;
		}

		// Orange lerp for 1st person (like Amalgam)
		if (!bHasPriorityColor && strcmp(spectator.m_sMode, "1st") == 0)
		{
			constexpr Color_t orangeColor = { 255, 150, 0, 255 };
			constexpr float lerpFactor = 0.5f;
			nameColor.r = static_cast<unsigned char>(nameColor.r + (orangeColor.r - nameColor.r) * lerpFactor);
			nameColor.g = static_cast<unsigned char>(nameColor.g + (orangeColor.g - nameColor.g) * lerpFactor);
			nameColor.b = static_cast<unsigned char>(nameColor.b + (orangeColor.b - nameColor.b) * lerpFactor);
		}

		// Convert name to narrow string for rendering
		std::string narrowName = Utils::ConvertWideToUTF8(spectator.Name);
		
		// Draw player name
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nTextX, nTextY + (CFG::Menu_Drag_Bar_Height / 2) + 1, nameColor, POS_CENTERY, narrowName.c_str());

		I::MatSystemSurface->DisableClipping(true);

		// Draw respawn time on the right side (outside clipping rect)
		if (spectator.m_flRespawnIn >= 0.f)
		{
			char szRespawn[16];
			snprintf(szRespawn, sizeof(szRespawn), "%ds", static_cast<int>(spectator.m_flRespawnIn));
			
			// Position from right edge (fixed offset works for short time strings)
			const int nRespawnX = nModeX + LIST_WIDTH - CFG::Menu_Spacing_X - 25;
			H::Draw->String(H::Fonts->Get(EFonts::Menu), nRespawnX, nTextY + (CFG::Menu_Drag_Bar_Height / 2) + 1, nameColor, POS_CENTERY, szRespawn);
		}

		// Outline
		H::Draw->OutlinedRect(CFG::Visuals_SpectatorList_Pos_X, CFG::Visuals_SpectatorList_Pos_Y + (CFG::Menu_Drag_Bar_Height * iPos) - 1, LIST_WIDTH, CFG::Menu_Drag_Bar_Height + 1, outlineColor);
	}
}
