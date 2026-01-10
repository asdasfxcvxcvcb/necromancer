#pragma once
#include "../../../SDK/SDK.h"

// Enemy tag types for fakelag behavior
enum class EFakeLagThreatType
{
	None,       // No threat detected
	NoTag,      // Enemy scoped, no tag - 4-7 ticks, close sightline required
	RetardLegit,// Enemy scoped, retard legit tag - 6-12 ticks, 2x sightline distance
	Cheater     // Enemy scoped, cheater tag - 19-24 ticks, no sightline distance check
};

class CFakeLag
{
private:
	bool IsAllowed(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	EFakeLagThreatType CheckSniperThreat(C_TFPlayer* pLocal, int& outMinTicks, int& outMaxTicks);
	bool IsSniperThreat(C_TFPlayer* pLocal, int& outMinTicks, int& outMaxTicks); // Legacy wrapper
	void PreserveBlastJump(C_TFPlayer* pLocal);
	void Unduck(C_TFPlayer* pLocal, CUserCmd* pCmd);
	void Prediction(C_TFPlayer* pLocal, CUserCmd* pCmd);
	
	// Calculate max allowed fakelag ticks considering stored ticks, anti-aim, and menu settings
	int CalculateMaxAllowedTicks(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);

	Vec3 m_vLastPosition = {};
	int m_iNewEntityUnchokeTicks = 0;
	int m_iCurrentChokeTicks = 0;
	int m_iTargetChokeTicks = 0;
	
	// Amalgam-style flags
	bool m_bPreservingBlast = false;
	bool m_bUnducking = false;
	
	// Current threat type for debugging/display
	EFakeLagThreatType m_eCurrentThreat = EFakeLagThreatType::None;

public:
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket);
	void UpdateDrawChams(); // Call after Run() to update fake model visibility

	int m_iGoal = 0;
	bool m_bEnabled = false;
	
	// Getter for current threat type (for UI/debugging)
	EFakeLagThreatType GetCurrentThreat() const { return m_eCurrentThreat; }
};

MAKE_SINGLETON_SCOPED(CFakeLag, FakeLag, F);
