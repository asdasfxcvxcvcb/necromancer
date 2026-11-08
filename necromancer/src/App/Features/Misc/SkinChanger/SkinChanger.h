#pragma once

#include "../../../../SDK/SDK.h"

MAKE_SIGNATURE(GetItemSystem, "client.dll", "48 83 EC ? 48 8B 05 ? ? ? ? 48 85 C0 75 ? B9 ? ? ? ? 48 89 5C 24 ? E8 ? ? ? ? 48 8B D8", 0x0);
MAKE_SIGNATURE(CEconItemSchema_GetAttributeDefinition, "client.dll", "85 D2 78 16 3B 91 E8 02 00 00 7D 0E 48 8B 81 D8 02 00 00 8B D2 48 8B 04 D0 C3", 0x0);
MAKE_SIGNATURE(CAttributeList_SetRuntimeAttributeValue, "client.dll", "48 89 5C 24 ? 55 56 57 48 8B EC 48 83 EC ? 44 8B 49", 0x0);
MAKE_SIGNATURE(CEconItemSchema_GetItemDefinition, "client.dll", "89 54 24 ? 53 48 83 EC ? 48 8B D9 48 8D 54 24 ? 48 81 C1 ? ? ? ? E8 ? ? ? ? 85 C0", 0x0);
MAKE_SIGNATURE(CEconItemView_GetSkin, "client.dll", "48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 63 FA", 0x0);

enum AttributeIndex
{
	ATTRIB_ITEM_STYLE_OVERRIDE = 542,
	ATTRIB_KILLSTREAK_EFFECT = 2013,
	ATTRIB_KILLSTREAK_IDLEEFFECT = 2014,
	ATTRIB_KILLSTREAK_TIER = 2025,
	ATTRIB_AUSTRALIUM_ITEM = 2027,
	ATTRIB_FESTIVIZED = 2053
};

enum ManagedAttributeIndex
{
	MATTRIB_ITEM_STYLE_OVERRIDE,
	MATTRIB_KILLSTREAK_EFFECT,
	MATTRIB_KILLSTREAK_IDLEEFFECT,
	MATTRIB_KILLSTREAK_TIER,
	MATTRIB_AUSTRALIUM_ITEM,
	MATTRIB_FESTIVIZED,
	MATTRIB_COUNT
};

constexpr int MANAGED_ATTRIBUTE_IDS[MATTRIB_COUNT] =
{
	ATTRIB_ITEM_STYLE_OVERRIDE,
	ATTRIB_KILLSTREAK_EFFECT,
	ATTRIB_KILLSTREAK_IDLEEFFECT,
	ATTRIB_KILLSTREAK_TIER,
	ATTRIB_AUSTRALIUM_ITEM,
	ATTRIB_FESTIVIZED
};

constexpr int KILLSTREAKER_PARTICLE_ID_BASE = 2001;
constexpr int EYEGLOW_WATCHER_OFFSET = 7816;
constexpr int EYEGLOW_WATCHER_KILLCOUNT_CACHE_OFFSET = 1016;
constexpr int MAX_TRACKED_WEARABLES = 16;

struct CEconItemAttribute
{
	void* pad = 0;
	unsigned int m_iAttributeDefinitionIndex;
	union {
		int m_iRawValue32;
		float m_flValue;
	};
	int m_nRefundableCurrency = 0;

	inline CEconItemAttribute(uint16_t iAttributeDefinitionIndex, float flValue)
	{
		m_iAttributeDefinitionIndex = iAttributeDefinitionIndex;
		m_flValue = flValue;
	}
};

class CAttributeList
{
public:
	void* pad;
	CUtlVector<CEconItemAttribute> m_Attributes;
	void* m_pManager;

	using GetItemSystemFN = void* (__fastcall*)();
	using GetAttributeDefinitionFN = void* (__fastcall*)(void*, int);
	using SetRuntimeAttributeValueFN = void(__fastcall*)(CAttributeList*, void*, float);

	void* GetAttributeDefinition(int index)
	{
		const auto dwGetAttributeDefinition = Signatures::CEconItemSchema_GetAttributeDefinition.Get();
		if (!dwGetAttributeDefinition)
			return nullptr;

		const auto dwGetItemSystem = Signatures::GetItemSystem.Get();
		if (!dwGetItemSystem)
			return nullptr;

		auto pItemSystem = reinterpret_cast<GetItemSystemFN>(dwGetItemSystem)();
		if (!pItemSystem)
			return nullptr;

		auto schema = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(pItemSystem) + 8);

		return reinterpret_cast<GetAttributeDefinitionFN>(dwGetAttributeDefinition)(schema, index);
	}

	bool GetAttribute(int index, float &outValue) const
	{
		auto pElements = m_Attributes.Base();
		const int nCount = m_Attributes.Count();
		for (int i = 0; i < nCount; i++)
		{
			if (static_cast<unsigned short>(pElements[i].m_iAttributeDefinitionIndex) == static_cast<unsigned short>(index))
			{
				outValue = pElements[i].m_flValue;
				return true;
			}
		}
		return false;
	}

	bool SetAttribute(int index, float value)
	{
		auto attributeDefinition = GetAttributeDefinition(index);
		if (!attributeDefinition)
			return false;

		reinterpret_cast<SetRuntimeAttributeValueFN>(
			Signatures::CAttributeList_SetRuntimeAttributeValue.Get())(this, attributeDefinition, value);
		return true;
	}

	void NotifyAttributeManager()
	{
		const auto pManager = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(this) + 40);
		if (!pManager)
			return;

		reinterpret_cast<void(__fastcall*)(void*)>(Memory::GetVFunc(pManager, 15))(pManager);
	}

	bool RemoveAttribute(int index)
	{
		auto pElements = m_Attributes.Base();
		const int nCount = m_Attributes.Count();
		for (int i = 0; i < nCount; i++)
		{
			if (static_cast<unsigned short>(pElements[i].m_iAttributeDefinitionIndex) == static_cast<unsigned short>(index))
			{
				m_Attributes.Remove(i);
				NotifyAttributeManager();
				return true;
			}
		}
		return false;
	}
};

struct AttributeSnapshot
{
	bool bPresent[MATTRIB_COUNT] = {};
	float flValue[MATTRIB_COUNT] = {};
	unsigned int uTouched = 0;
};

struct EntityAttributeSnapshot
{
	unsigned int nHandle = 0;
	AttributeSnapshot Attributes;
};

struct SkinState
{
	bool bActive = false;
	bool bAustralium = false;
	bool bFestivized = false;
	int iKillstreakTier = 0;
	int iKillstreakSheen = 0;
	int iKillstreakEffect = 0;
	std::uintptr_t uLocalPlayer = 0;
};

class CSkinChanger
{
private:
	SkinState m_LastState;
	int m_iLastWeaponHandles[MAX_WEAPONS] = {};
	bool m_bGoldApplied[MAX_WEAPONS] = {};
	AttributeSnapshot m_WeaponSnapshots[MAX_WEAPONS] = {};
	EntityAttributeSnapshot m_ExtraWearableSnapshots[MAX_WEAPONS] = {};
	EntityAttributeSnapshot m_WearableSnapshots[MAX_TRACKED_WEARABLES] = {};
	std::uintptr_t m_uTrackedLocalPlayer = 0;
	bool m_bWasLevelTransition = false;
	bool m_bViewModelGold = false;
	bool m_bPendingViewModelRestore = false;

	bool HasSkinStateChanged();
	void UpdateSkinStateCache();

	CAttributeList* GetAttributeList(C_BaseEntity* pEntity);
	bool SetAttribute(C_BaseEntity* pEntity, int iIndex, float flValue);
	bool RemoveAttribute(C_BaseEntity* pEntity, int iIndex);
	void SetItemStyle(C_BaseEntity* pEntity, int iStyle);
	void SetItemDefinitionIndex(C_BaseEntity* pEntity, int iDefIndex);
	void* GetItemDefinition(C_BaseEntity* pEntity);
	void* GetItemDefinitionByIndex(int iDefinitionIndex);
	int FindGoldStyleIndex(void* pDefinition);
	int FindGoldStyleForWeapon(C_BaseEntity* pWeapon, int* piOutDefIndex);
	int GetResolvedSkin(C_BaseEntity* pEntity, int iTeam, bool bViewmodel);
	int GetTwinResolvedSkin(int iTwinDefIndex, int iStyle, int iTeam, bool bViewmodel);
	void CaptureAttributeSnapshot(C_BaseEntity* pEntity, AttributeSnapshot& outSnapshot);
	void ApplyManagedAttribute(C_BaseEntity* pEntity, AttributeSnapshot* pSnapshot, int iManagedIndex, bool bSet, float flValue);
	void ApplyAttributes(C_BaseEntity* pEntity, AttributeSnapshot* pSnapshot, int iGoldStyleIndex, bool bApply, bool bKillstreakOnly);
	void TriggerEyeglowRederivation(C_TFPlayer* pLocal);
	void ResetEntityTracking();
	void ApplySkins(bool bActive);

	int m_nKillstreakCount = 0;

public:
	void FrameStageNotify();

	void OnKill();
	void OnDeath();
	void OnLevelShutdown();
};

MAKE_SINGLETON_SCOPED(CSkinChanger, SkinChanger, F);
