#pragma once

#include "../../../SDK/SDK.h"

// ============================================
// Tickbase Manipulation - Simple wrapper
// ============================================
// This is a simple wrapper around the Shifting namespace
// for compatibility with code that expects F::Ticks->
// ============================================

class CTickbaseManip
{
public:
	// Getters - read from Shifting namespace
	int GetChargedTicks() const { return Shifting::nAvailableTicks; }
	bool IsCharging() const { return Shifting::bRecharging; }
	bool IsShifting() const { return Shifting::bShifting; }
	bool IsShiftingWarp() const { return Shifting::bShiftingWarp; }
	int GetShiftTick() const { return Shifting::nCurrentShiftTick; }
	int GetShiftTotal() const { return Shifting::nTotalShiftTicks; }
	
	bool CanShift(int nTicks) const
	{
		if (Shifting::bShifting || Shifting::bRecharging || Shifting::bShiftingWarp)
			return false;
		return Shifting::nAvailableTicks >= nTicks;
	}
	
	void StartCharging() { Shifting::bRecharging = true; }
	void StopCharging() { Shifting::bRecharging = false; }
	
	void Reset()
	{
		Shifting::Reset();
	}
	
	// ============================================
	// Auto Settings
	// ============================================
	float GetPingMs();
	int GetOptimalRechargeLimit();
	int GetOptimalDTTicks();
	int GetOptimalDelayTicks();
	int GetOptimalMaxCommands();
	int GetOptimalDTCommands();
};

MAKE_SINGLETON_SCOPED(CTickbaseManip, Ticks, F);
