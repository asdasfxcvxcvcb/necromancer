#include "../../SDK/SDK.h"
#include "../Features/CFG.h"

MAKE_SIGNATURE(CMatchInviteNotification_OnTick, "client.dll", "40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? F7 83", 0x0);

// Amalgam's "Extend queue" timings: the game only keeps a match invite alive
// for 10 seconds before it goes away, we stretch it to the full casual window
constexpr double JOIN_TIME = 10.0;
constexpr double FULL_TIME = 180.0 - JOIN_TIME;

static std::unordered_map<void*, double> s_mLastAutoJoinTime = {};

MAKE_HOOK(CMatchInviteNotification_OnTick, Signatures::CMatchInviteNotification_OnTick.Get(), void, __fastcall,
	void* rcx)
{
	if (CFG::Misc_Extend_Queue)
	{
		const auto dFloatTime = Plat_FloatTime();
		auto& dAutoJoinTime = *reinterpret_cast<double*>(uintptr_t(rcx) + 616);
		auto& dLastAutoJoinTime = s_mLastAutoJoinTime[rcx];

		// +616 is a hardcoded struct offset borrowed from Amalgam. If a TF2
		// update moves the field, writing here corrupts the panel and blows up
		// its paint pass with no cheat frame anywhere near the crash. Only
		// extend while the value still looks like an invite expiry - the game
		// sets now..now+10 on a fresh invite, we set now..now+180. Anything
		// else means the layout drifted: leave the panel alone and the
		// feature quietly does nothing instead of corrupting it.
		const bool bFreshInvite = dAutoJoinTime > dFloatTime - 30.0 && dAutoJoinTime < dFloatTime + JOIN_TIME + 30.0;
		const bool bOurExtend = dAutoJoinTime == dLastAutoJoinTime && dAutoJoinTime > dFloatTime;
		if (bFreshInvite || bOurExtend)
		{
			const double dExtend = dAutoJoinTime != dLastAutoJoinTime ? FULL_TIME : 0.0;
			dLastAutoJoinTime = dAutoJoinTime = std::max(dAutoJoinTime + dExtend, dFloatTime + 1.0);
		}

		for (auto it = s_mLastAutoJoinTime.begin(); it != s_mLastAutoJoinTime.end();)
		{
			if (dFloatTime > it->second)
				it = s_mLastAutoJoinTime.erase(it);
			else
				++it;
		}
	}

	CALL_ORIGINAL(rcx);
}
