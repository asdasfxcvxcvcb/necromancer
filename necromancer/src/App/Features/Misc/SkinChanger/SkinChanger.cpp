#include "SkinChanger.h"
#include "../../CFG.h"
#include <cstring>

bool CSkinChanger::HasSkinStateChanged()
{
	if (m_LastState.bActive != CFG::SkinChanger_Active)
		return true;
	if (m_LastState.bAustralium != CFG::SkinChanger_Australium)
		return true;
	if (m_LastState.bFestivized != CFG::SkinChanger_Festivized)
		return true;
	if (m_LastState.iKillstreakTier != CFG::SkinChanger_Killstreak_Tier)
		return true;
	if (m_LastState.iKillstreakSheen != CFG::SkinChanger_Killstreak_Sheen)
		return true;
	if (m_LastState.iKillstreakEffect != CFG::SkinChanger_Killstreak_Effect)
		return true;
	if (m_LastState.uLocalPlayer != reinterpret_cast<std::uintptr_t>(H::Entities->GetLocal()))
		return true;

	return false;
}

void CSkinChanger::UpdateSkinStateCache()
{
	m_LastState.bActive = CFG::SkinChanger_Active;
	m_LastState.bAustralium = CFG::SkinChanger_Australium;
	m_LastState.bFestivized = CFG::SkinChanger_Festivized;
	m_LastState.iKillstreakTier = CFG::SkinChanger_Killstreak_Tier;
	m_LastState.iKillstreakSheen = CFG::SkinChanger_Killstreak_Sheen;
	m_LastState.iKillstreakEffect = CFG::SkinChanger_Killstreak_Effect;
	m_LastState.uLocalPlayer = reinterpret_cast<std::uintptr_t>(H::Entities->GetLocal());
}

CAttributeList* CSkinChanger::GetAttributeList(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return nullptr;

	static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
	if (nDefinitionOffset <= 0)
		return nullptr;

	return reinterpret_cast<CAttributeList*>(reinterpret_cast<std::uintptr_t>(pEntity) + nDefinitionOffset + 168);
}

bool CSkinChanger::SetAttribute(C_BaseEntity* pEntity, int iIndex, float flValue)
{
	auto pAttributeList = GetAttributeList(pEntity);
	if (!pAttributeList)
		return false;

	return pAttributeList->SetAttribute(iIndex, flValue);
}

bool CSkinChanger::RemoveAttribute(C_BaseEntity* pEntity, int iIndex)
{
	auto pAttributeList = GetAttributeList(pEntity);
	if (!pAttributeList)
		return false;

	return pAttributeList->RemoveAttribute(iIndex);
}

void CSkinChanger::SetItemStyle(C_BaseEntity* pEntity, int iStyle)
{
	if (!pEntity)
		return;

	static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
	if (nDefinitionOffset <= 0)
		return;

	const auto pStyle = reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(pEntity) + nDefinitionOffset - 72 + 153);
	const auto uStyle = static_cast<std::uint8_t>(iStyle);
	if (*pStyle == uStyle)
		return;

	*pStyle = uStyle;
}

void CSkinChanger::SetItemDefinitionIndex(C_BaseEntity* pEntity, int iDefIndex)
{
	if (!pEntity)
		return;

	static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
	if (nDefinitionOffset <= 0)
		return;

	const auto pIndex = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(pEntity) + nDefinitionOffset);
	const auto uIndex = static_cast<std::uint16_t>(iDefIndex);
	if (*pIndex == uIndex)
		return;

	*pIndex = uIndex;
}

static int GetStockDefFromTwin(int iTwinIndex)
{
	switch (iTwinIndex)
	{
		case 194: return 4;
		case 197: return 7;
		case 200: return 13;
		case 201: return 14;
		case 202: return 15;
		case 203: return 16;
		case 205: return 18;
		case 206: return 19;
		case 207: return 20;
		case 208: return 21;
		case 211: return 29;
		default: return iTwinIndex;
	}
}

void* CSkinChanger::GetItemDefinition(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return nullptr;

	static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
	if (nDefinitionOffset <= 0)
		return nullptr;

	const auto pView = reinterpret_cast<std::uintptr_t>(pEntity) + nDefinitionOffset - 72;
	return GetItemDefinitionByIndex(static_cast<int>(*reinterpret_cast<std::uint16_t*>(pView + 72)));
}

void* CSkinChanger::GetItemDefinitionByIndex(int iDefinitionIndex)
{
	if (!Signatures::CEconItemSchema_GetItemDefinition.Get() || !Signatures::GetItemSystem.Get())
		return nullptr;

	using GetItemSystemFN = void* (__fastcall*)();
	const auto pItemSystem = reinterpret_cast<GetItemSystemFN>(Signatures::GetItemSystem.Get())();
	if (!pItemSystem)
		return nullptr;

	using GetItemDefinitionFN = void* (__fastcall*)(void*, int);
	return reinterpret_cast<GetItemDefinitionFN>(Signatures::CEconItemSchema_GetItemDefinition.Get())(
		reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(pItemSystem) + 8), iDefinitionIndex);
}

static int GetGoldStyleTwinIndex(int iDefinitionIndex)
{
	switch (iDefinitionIndex)
	{
		case 4: return 194;
		case 7: return 197;
		case 13: return 200;
		case 14: return 201;
		case 15: return 202;
		case 16: return 203;
		case 18: return 205;
		case 19: return 206;
		case 20: return 207;
		case 21: return 208;
		case 29: return 211;
		default: return iDefinitionIndex;
	}
}

int CSkinChanger::FindGoldStyleForWeapon(C_BaseEntity* pWeapon, int* piOutDefIndex)
{
	if (piOutDefIndex)
		*piOutDefIndex = -1;

	if (!pWeapon)
		return -1;

	static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
	if (nDefinitionOffset <= 0)
		return -1;

	const int iDefinitionIndex = static_cast<int>(*reinterpret_cast<const std::uint16_t*>(
		reinterpret_cast<std::uintptr_t>(pWeapon) + nDefinitionOffset));

	const int iTwinIndex = GetGoldStyleTwinIndex(iDefinitionIndex);

	int iStyle = -1;

	if (iTwinIndex != iDefinitionIndex)
	{
		iStyle = FindGoldStyleIndex(GetItemDefinitionByIndex(iTwinIndex));
		if (iStyle >= 0)
		{
			if (piOutDefIndex)
				*piOutDefIndex = iTwinIndex;
			return iStyle;
		}
	}

	iStyle = FindGoldStyleIndex(GetItemDefinitionByIndex(iDefinitionIndex));
	if (iStyle >= 0 && piOutDefIndex)
		*piOutDefIndex = iDefinitionIndex;
	return iStyle;
}

int CSkinChanger::FindGoldStyleIndex(void* pDefinition)
{
	if (!pDefinition)
		return -1;

	const auto nDefinition = reinterpret_cast<std::uintptr_t>(pDefinition);
	const auto pStyles = *reinterpret_cast<void**>(nDefinition + 280);
	if (!pStyles)
		return -1;

	const auto nStyles = reinterpret_cast<std::uintptr_t>(pStyles);
	const int nCount = *reinterpret_cast<std::uint8_t*>(nStyles + 608);
	if (nCount < 2)
		return -1;

	const auto nEntries = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(nStyles + 592));
	if (!nEntries)
		return -1;

	const auto nBaseStyle = *reinterpret_cast<std::uintptr_t*>(nEntries);
	if (!nBaseStyle)
		return -1;

	const int iBaseRed = *reinterpret_cast<int*>(nBaseStyle + 16);
	const int iBaseBlu = *reinterpret_cast<int*>(nBaseStyle + 20);

	for (int iStyle = 1; iStyle < nCount; iStyle++)
	{
		const auto nStyle = *reinterpret_cast<std::uintptr_t*>(nEntries + 8 * iStyle);
		if (!nStyle)
			continue;

		const int iRed = *reinterpret_cast<int*>(nStyle + 16);
		const int iBlu = *reinterpret_cast<int*>(nStyle + 20);
		if (iRed == iBaseRed && iBlu == iBaseBlu)
			continue;

		const auto pszImage = *reinterpret_cast<const char**>(nStyle + 72);
		if (pszImage && std::strstr(pszImage, "gold"))
			return iStyle;
	}

	return -1;
}

int CSkinChanger::GetResolvedSkin(C_BaseEntity* pEntity, int iTeam, bool bViewmodel)
{
	if (!pEntity || !Signatures::CEconItemView_GetSkin.Get())
		return -1;

	static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
	if (nDefinitionOffset <= 0)
		return -1;

	const auto pView = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(pEntity) + nDefinitionOffset - 72);

	using GetSkinFN = unsigned int(__fastcall*)(void*, unsigned int, unsigned char);
	return static_cast<int>(reinterpret_cast<GetSkinFN>(Signatures::CEconItemView_GetSkin.Get())(
		pView, static_cast<unsigned int>(iTeam), bViewmodel ? 1 : 0));
}

int CSkinChanger::GetTwinResolvedSkin(int iTwinDefIndex, int iStyle, int iTeam, bool bViewmodel)
{
	const auto pDefinition = GetItemDefinitionByIndex(iTwinDefIndex);
	if (!pDefinition)
		return -1;

	const auto nDefinition = reinterpret_cast<std::uintptr_t>(pDefinition);
	const auto pStyles = *reinterpret_cast<void**>(nDefinition + 280);
	if (!pStyles)
		return -1;

	const auto nStyles = reinterpret_cast<std::uintptr_t>(pStyles);
	if (iStyle < 0 || iStyle >= static_cast<int>(*reinterpret_cast<const std::uint32_t*>(nStyles + 608)))
		return -1;

	const auto nEntries = *reinterpret_cast<std::uintptr_t*>(nStyles + 592);
	if (!nEntries)
		return -1;

	const auto nEntry = *reinterpret_cast<std::uintptr_t*>(nEntries + 8 * iStyle);
	if (!nEntry)
		return -1;

	int iSkin = *reinterpret_cast<const int*>(nEntry + (bViewmodel ? 28 : 8) + 4 * iTeam);
	if (bViewmodel && iSkin == -1)
		iSkin = *reinterpret_cast<const int*>(nEntry + 8 + 4 * iTeam);
	return iSkin;
}

void CSkinChanger::CaptureAttributeSnapshot(C_BaseEntity* pEntity, AttributeSnapshot& outSnapshot)
{
	const auto pList = GetAttributeList(pEntity);
	if (!pList)
	{
		outSnapshot = AttributeSnapshot{};
		return;
	}

	outSnapshot.uTouched = 0;

	for (int i = 0; i < MATTRIB_COUNT; i++)
	{
		outSnapshot.flValue[i] = 0.0f;
		outSnapshot.bPresent[i] = pList->GetAttribute(MANAGED_ATTRIBUTE_IDS[i], outSnapshot.flValue[i]);
	}
}

void CSkinChanger::ApplyManagedAttribute(C_BaseEntity* pEntity, AttributeSnapshot* pSnapshot, int iManagedIndex, bool bSet, float flValue)
{
	if (!pEntity)
		return;

	const auto pList = GetAttributeList(pEntity);
	if (!pList)
		return;

	const int iAttributeId = MANAGED_ATTRIBUTE_IDS[iManagedIndex];
	const unsigned int uTouchMask = 1u << iManagedIndex;
	float flCurrent = 0.0f;
	const bool bPresent = pList->GetAttribute(iAttributeId, flCurrent);

	if (bSet)
	{
		if (bPresent && flCurrent == flValue)
			return;
		const bool bWritten = SetAttribute(pEntity, iAttributeId, flValue);
		if (pSnapshot && bWritten)
			pSnapshot->uTouched |= uTouchMask;
		return;
	}

	if (!pSnapshot || !(pSnapshot->uTouched & uTouchMask))
		return;

	if (pSnapshot->bPresent[iManagedIndex])
	{
		const float flOriginal = pSnapshot->flValue[iManagedIndex];
		if (bPresent && flCurrent == flOriginal)
			return;
		SetAttribute(pEntity, iAttributeId, flOriginal);
		return;
	}

	if (bPresent)
		RemoveAttribute(pEntity, iAttributeId);
}

void CSkinChanger::ApplyAttributes(C_BaseEntity* pEntity, AttributeSnapshot* pSnapshot, int iGoldStyleIndex, bool bApply, bool bKillstreakOnly)
{
	if (!pEntity)
		return;

	const int iTier = bApply ? CFG::SkinChanger_Killstreak_Tier : 0;
	if (iTier > 0)
	{
		ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_TIER, true, static_cast<float>(iTier));

		const int iSheen = (iTier >= 2)
			? (CFG::SkinChanger_Killstreak_Sheen > 0 ? CFG::SkinChanger_Killstreak_Sheen : 1)
			: 0;

		if (iSheen > 0)
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_IDLEEFFECT, true, static_cast<float>(iSheen));
		else
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_IDLEEFFECT, false, 0.0f);

		const int iEffect = (iTier >= 3 && CFG::SkinChanger_Killstreak_Effect > 0)
			? (CFG::SkinChanger_Killstreak_Effect + KILLSTREAKER_PARTICLE_ID_BASE)
			: 0;

		if (iEffect > 0)
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_EFFECT, true, static_cast<float>(iEffect));
		else
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_EFFECT, false, 0.0f);
	}
	else
	{
		ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_TIER, false, 0.0f);
		ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_IDLEEFFECT, false, 0.0f);
		ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_KILLSTREAK_EFFECT, false, 0.0f);
	}

	if (!bKillstreakOnly)
	{
		if (bApply && CFG::SkinChanger_Australium && iGoldStyleIndex >= 0)
		{
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_AUSTRALIUM_ITEM, true, 1.0f);
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_ITEM_STYLE_OVERRIDE, true, static_cast<float>(iGoldStyleIndex));
		}
		else
		{
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_AUSTRALIUM_ITEM, false, 0.0f);
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_ITEM_STYLE_OVERRIDE, false, 0.0f);
		}

		if (bApply && CFG::SkinChanger_Festivized)
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_FESTIVIZED, true, 1.0f);
		else
			ApplyManagedAttribute(pEntity, pSnapshot, MATTRIB_FESTIVIZED, false, 0.0f);
	}
}

void CSkinChanger::TriggerEyeglowRederivation(C_TFPlayer* pLocal)
{
	if (!pLocal)
		return;

	*reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pLocal) + EYEGLOW_WATCHER_OFFSET + EYEGLOW_WATCHER_KILLCOUNT_CACHE_OFFSET) = 0x40000000;
}

void CSkinChanger::ResetEntityTracking()
{
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		m_iLastWeaponHandles[i] = 0;
		m_bGoldApplied[i] = false;
		m_WeaponSnapshots[i] = AttributeSnapshot{};
		m_ExtraWearableSnapshots[i] = EntityAttributeSnapshot{};
	}

	for (int i = 0; i < MAX_TRACKED_WEARABLES; i++)
		m_WearableSnapshots[i] = EntityAttributeSnapshot{};

	m_bViewModelGold = false;
	m_bPendingViewModelRestore = false;
}

void CSkinChanger::ApplySkins(bool bActive)
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	if (m_bWasLevelTransition && !G::bLevelTransition)
		ResetEntityTracking();
	m_bWasLevelTransition = G::bLevelTransition;

	const auto uLocalPlayer = reinterpret_cast<std::uintptr_t>(pLocal);
	if (uLocalPlayer != m_uTrackedLocalPlayer)
	{
		m_uTrackedLocalPlayer = uLocalPlayer;
		ResetEntityTracking();
	}

	static int nSkinOffset = NetVars::GetNetVar("CBaseAnimating", "m_nSkin");
	static int nMyWeaponsOffset = NetVars::GetNetVar("CBaseCombatCharacter", "m_hMyWeapons");
	static int nWearablesOffset = NetVars::GetNetVar("CTFPlayer", "m_hMyWearables");
	static int nViewModelOffset = NetVars::GetNetVar("CBasePlayer", "m_hViewModel[0]");

	if (nSkinOffset <= 0 || nMyWeaponsOffset <= 0 || nViewModelOffset <= 0)
		return;

	const int iTeam = pLocal->m_iTeamNum();

	const bool bKillstreakChanged =
		m_LastState.bActive != bActive
		|| m_LastState.iKillstreakTier != CFG::SkinChanger_Killstreak_Tier
		|| m_LastState.iKillstreakSheen != CFG::SkinChanger_Killstreak_Sheen
		|| m_LastState.iKillstreakEffect != CFG::SkinChanger_Killstreak_Effect;
	const bool bJustActivated = bActive && !m_LastState.bActive;
	const bool bStateChanged = bKillstreakChanged || HasSkinStateChanged();
	if (bStateChanged)
		UpdateSkinStateCache();

	if (bActive)
		m_bPendingViewModelRestore = false;

	const bool bAustralium = bActive && CFG::SkinChanger_Australium;

	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		const int hWeapon = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pLocal) + nMyWeaponsOffset + i * 4);
		const bool bNewWeapon = hWeapon != m_iLastWeaponHandles[i];
		m_iLastWeaponHandles[i] = hWeapon;
		if (bNewWeapon)
			m_bGoldApplied[i] = false;

		const auto pWeapon = reinterpret_cast<C_TFWeaponBase*>(I::ClientEntityList->GetClientEntityFromHandle(hWeapon));
		if (!pWeapon)
			continue;

		if (bNewWeapon || bJustActivated)
			CaptureAttributeSnapshot(pWeapon, m_WeaponSnapshots[i]);

		int iStyleDefIndex = -1;
		const int iGoldStyle = bAustralium ? FindGoldStyleForWeapon(pWeapon, &iStyleDefIndex) : -1;

		if (iGoldStyle >= 0)
		{
			SetItemStyle(pWeapon, iGoldStyle);
			if (iStyleDefIndex >= 0)
				SetItemDefinitionIndex(pWeapon, iStyleDefIndex);
			ApplyAttributes(pWeapon, &m_WeaponSnapshots[i], iGoldStyle, bActive, false);

			static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
			const int iOwnDef = (nDefinitionOffset > 0)
				? static_cast<int>(*reinterpret_cast<const std::uint16_t*>(reinterpret_cast<std::uintptr_t>(pWeapon) + nDefinitionOffset))
				: -1;

			int iWeaponSkin = -1;
			if (iStyleDefIndex >= 0 && iStyleDefIndex != iOwnDef)
				iWeaponSkin = GetTwinResolvedSkin(iStyleDefIndex, iGoldStyle, iTeam, false);
			else
				iWeaponSkin = GetResolvedSkin(pWeapon, iTeam, false);

			const auto pWeaponModel = pWeapon->GetModel();
			const auto pWeaponHDR = pWeaponModel ? I::ModelInfoClient->GetStudiomodel(pWeaponModel) : nullptr;
			const int nSkinFamilies = pWeaponHDR ? pWeaponHDR->numskinfamilies : 0;

			if (iWeaponSkin >= 0 && pWeaponHDR && iWeaponSkin < nSkinFamilies)
			{
				const auto pWeaponSkin = reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pWeapon) + nSkinOffset);
				if (*pWeaponSkin != iWeaponSkin)
					*pWeaponSkin = iWeaponSkin;
			}

			m_bGoldApplied[i] = true;
		}
		else
		{
			ApplyAttributes(pWeapon, &m_WeaponSnapshots[i], -1, bActive, false);

			if (m_bGoldApplied[i])
			{
				m_bGoldApplied[i] = false;
				SetItemStyle(pWeapon, 0xFF);

				static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
				if (nDefinitionOffset > 0)
				{
					const int iCurrentDef = static_cast<int>(*reinterpret_cast<const std::uint16_t*>(
						reinterpret_cast<std::uintptr_t>(pWeapon) + nDefinitionOffset));
					SetItemDefinitionIndex(pWeapon, GetStockDefFromTwin(iCurrentDef));
				}

				const int iWeaponSkin = GetResolvedSkin(pWeapon, iTeam, false);
				if (iWeaponSkin >= 0)
				{
					const auto pWeaponSkin = reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pWeapon) + nSkinOffset);
					if (*pWeaponSkin != iWeaponSkin)
						*pWeaponSkin = iWeaponSkin;
				}
			}
		}

		const auto pExtraWearable = pWeapon->m_hExtraWearable().Get();
		if (pExtraWearable)
		{
			auto &extraSnapshot = m_ExtraWearableSnapshots[i];
			const unsigned int hExtra = pWeapon->m_hExtraWearable().ToInt();
			if (extraSnapshot.nHandle != hExtra || bJustActivated)
			{
				extraSnapshot.nHandle = hExtra;
				CaptureAttributeSnapshot(pExtraWearable, extraSnapshot.Attributes);
			}

			ApplyAttributes(pExtraWearable, &extraSnapshot.Attributes, -1, bActive, true);
		}

		if (bNewWeapon || bStateChanged)
			reinterpret_cast<void(__fastcall*)(void*)>((*reinterpret_cast<void***>(pWeapon))[213])(pWeapon);
	}

	if (nWearablesOffset > 0)
	{
		const auto pWearables = reinterpret_cast<CUtlVector<EHANDLE>*>(reinterpret_cast<std::uintptr_t>(pLocal) + nWearablesOffset);
		const int nWearableCount = pWearables->Count();

		for (int t = 0; t < MAX_TRACKED_WEARABLES; t++)
		{
			const unsigned int hTracked = m_WearableSnapshots[t].nHandle;
			if (hTracked != 0 && !I::ClientEntityList->GetClientEntityFromHandle(static_cast<int>(hTracked)))
				m_WearableSnapshots[t] = EntityAttributeSnapshot{};
		}

		for (int w = 0; w < nWearableCount && w < MAX_TRACKED_WEARABLES; w++)
		{
			const auto pWearable = (*pWearables)[w].Get();
			if (!pWearable)
				continue;

			const unsigned int hWearable = (*pWearables)[w].ToInt();
			int iTracked = -1;
			for (int t = 0; t < MAX_TRACKED_WEARABLES; t++)
			{
				if (m_WearableSnapshots[t].nHandle == hWearable)
				{
					iTracked = t;
					break;
				}
			}

			if (iTracked < 0)
			{
				for (int t = 0; t < MAX_TRACKED_WEARABLES; t++)
				{
					if (m_WearableSnapshots[t].nHandle == 0)
					{
						iTracked = t;
						m_WearableSnapshots[t].nHandle = hWearable;
						CaptureAttributeSnapshot(pWearable, m_WearableSnapshots[t].Attributes);
						break;
					}
				}
			}
			else if (bJustActivated)
			{
				CaptureAttributeSnapshot(pWearable, m_WearableSnapshots[iTracked].Attributes);
			}

			if (iTracked < 0)
				continue;

			ApplyAttributes(pWearable, &m_WearableSnapshots[iTracked].Attributes, -1, bActive, true);
		}
	}

	int iViewModelSkin = -1;
	bool bViewModelGold = false;
	const auto pActiveWeaponEntity = pLocal->m_hActiveWeapon().Get();
	const auto pActiveWeapon = pActiveWeaponEntity ? pActiveWeaponEntity->As<C_TFWeaponBase>() : nullptr;
	if (pActiveWeapon)
	{
		int iActiveStyleDefIndex = -1;
		const int iActiveGoldStyle = bAustralium ? FindGoldStyleForWeapon(pActiveWeapon, &iActiveStyleDefIndex) : -1;
		if (iActiveGoldStyle >= 0)
		{
			bViewModelGold = true;

			static int nDefinitionOffset = NetVars::GetNetVar("CEconEntity", "m_iItemDefinitionIndex");
			const int iActiveOwnDef = (nDefinitionOffset > 0)
				? static_cast<int>(*reinterpret_cast<const std::uint16_t*>(reinterpret_cast<std::uintptr_t>(pActiveWeapon) + nDefinitionOffset))
				: -1;

			if (iActiveStyleDefIndex >= 0 && iActiveStyleDefIndex != iActiveOwnDef)
				iViewModelSkin = GetTwinResolvedSkin(iActiveStyleDefIndex, iActiveGoldStyle, iTeam, true);
			else
				iViewModelSkin = GetResolvedSkin(pActiveWeapon, iTeam, true);
		}
		else if (m_bViewModelGold)
			iViewModelSkin = GetResolvedSkin(pActiveWeapon, iTeam, true);
	}

	if (iViewModelSkin >= 0)
	{
		for (int iViewModel = 0; iViewModel < 2; iViewModel++)
		{
			const int hViewModel = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pLocal) + nViewModelOffset + iViewModel * 4);
			const auto pViewModel = reinterpret_cast<C_BaseAnimating*>(I::ClientEntityList->GetClientEntityFromHandle(hViewModel));
			if (!pViewModel)
				continue;

			const auto pViewModelModel = pViewModel->GetModel();
			const auto pViewModelHDR = pViewModelModel ? I::ModelInfoClient->GetStudiomodel(pViewModelModel) : nullptr;
			if (!pViewModelHDR || iViewModelSkin >= pViewModelHDR->numskinfamilies)
				continue;

			const auto pViewModelSkin = reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(pViewModel) + nSkinOffset);
			if (*pViewModelSkin != iViewModelSkin)
				*pViewModelSkin = iViewModelSkin;
		}
	}

	if (pActiveWeapon)
	{
		m_bViewModelGold = bViewModelGold;
		m_bPendingViewModelRestore = false;
	}
	else if (!bActive)
	{
		bool bAnyItem = false;
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			if (m_iLastWeaponHandles[i] != 0)
			{
				bAnyItem = true;
				break;
			}
		}

		if (!bAnyItem)
			m_bViewModelGold = false;
		m_bPendingViewModelRestore = m_bViewModelGold;
	}

	if (bKillstreakChanged)
		TriggerEyeglowRederivation(pLocal);
}

void CSkinChanger::FrameStageNotify()
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	static bool s_bWasActive = false;
	const bool bActive = CFG::SkinChanger_Active;

	if (!bActive && !s_bWasActive && !m_bPendingViewModelRestore)
		return;

	ApplySkins(bActive);
	s_bWasActive = bActive;

	if (bActive && CFG::SkinChanger_Killstreak_Tier > 0)
	{
		auto pStreaks = reinterpret_cast<int*>(&pLocal->m_nStreaks());
		for (int i = 0; i < 4; i++)
			pStreaks[i] = m_nKillstreakCount;
	}
}

void CSkinChanger::OnKill()
{
	if (CFG::SkinChanger_Active && CFG::SkinChanger_Killstreak_Tier > 0)
		m_nKillstreakCount++;
}

void CSkinChanger::OnDeath()
{
	m_nKillstreakCount = 0;
}

void CSkinChanger::OnLevelShutdown()
{
	m_nKillstreakCount = 0;
}
