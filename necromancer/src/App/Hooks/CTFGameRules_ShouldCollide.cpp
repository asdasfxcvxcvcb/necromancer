#include "CTFGameRules_ShouldCollide.h"

#include "../../SDK/SDK.h"
#include "../Features/CFG.h"

// CTFGameRules::ShouldCollide (tf_gamerules.cpp:17370 in the leak) is the last
// stop of the movement collision rules: every CTraceFilterSimple (the filter
// game movement's TracePlayerBBox builds, util_shared.cpp:305) consults it
// with (moving group, other group). Players block movement through the
// (COLLISION_GROUP_PLAYER=5, COLLISION_GROUP_PLAYER_MOVEMENT=8) pair resolved
// by the base CGameRules::ShouldCollide - flipping exactly that pair to false
// makes players stop blocking each other's movement while every other rule
// (rockets, grenades, weapons, respawn rooms) stays intact.
//
// Both copies of the function are byte-identical, verified against the live
// binaries: client.dll RVA 0x51C110 (prediction) and server.dll RVA 0x5838F0
// (listen-server authority). The client copy alone only moves prediction - it
// needs the server copy to agree, so the flip engages only once the server
// hook lands (server.dll is loaded only on listen/local servers; on dedicated
// servers movement authority is unreachable and the option stays inert).

constexpr const char* SZ_SHOULDCOLLIDE_SIG = "41 3B D0 45 8B C8 44 0F 4E CA 41 0F 4E D0 41 83 F9 08 75 19 8D 42 F5 A9 FD FF FF FF 74 70 83 FA 18 74 6B 83 FA 1B 74 66 83 FA 14";

MAKE_SIGNATURE(CTFGameRules_ShouldCollide, "client.dll", SZ_SHOULDCOLLIDE_SIG, 0x0);

static bool IsPlayerMovementPlayerPair(int& g0, int& g1)
{
	if (g0 > g1)
	{
		const int nTmp = g0;
		g0 = g1;
		g1 = nTmp;
	}

	return g0 == COLLISION_GROUP_PLAYER && g1 == COLLISION_GROUP_PLAYER_MOVEMENT;
}

MAKE_HOOK(CTFGameRules_ShouldCollide, Signatures::CTFGameRules_ShouldCollide.Get(), bool, __fastcall,
	void* ecx, int collisionGroup0, int collisionGroup1)
{
	if (CFG::Misc_Ignore_Enemy_Collision && Hooks::CTFGameRules_ShouldCollide_Server::bServerHookActive
		&& IsPlayerMovementPlayerPair(collisionGroup0, collisionGroup1))
		return false;

	return CALL_ORIGINAL(ecx, collisionGroup0, collisionGroup1);
}

namespace Hooks::CTFGameRules_ShouldCollide_Server
{
	bool bServerHookActive = false;

	void Init()
	{
	}

	inline CHook Hook(Init);
	using fn = bool(__fastcall*)(void*, int, int);
	bool Func(void* ecx, int collisionGroup0, int collisionGroup1);

	bool EnsureHook()
	{
		static bool bHooked = false;
		if (bHooked)
			return true;

		static float flLastTry = -5.0f;
		const float flNow = static_cast<float>(Plat_FloatTime());
		if (flNow - flLastTry < 5.0f)
			return false;
		flLastTry = flNow;

		if (!GetModuleHandleA("server.dll"))
			return false;

		const auto dwAddr = Memory::FindSignature("server.dll", SZ_SHOULDCOLLIDE_SIG);
		if (!dwAddr)
			return false;

		Hook.Create(reinterpret_cast<void*>(dwAddr), reinterpret_cast<void*>(&Func));
		MH_EnableHook(reinterpret_cast<void*>(dwAddr));
		bServerHookActive = true;
		bHooked = true;
		return true;
	}

	bool Func(void* ecx, int collisionGroup0, int collisionGroup1)
	{
		if (CFG::Misc_Ignore_Enemy_Collision && IsPlayerMovementPlayerPair(collisionGroup0, collisionGroup1))
			return false;

		return Hook.Original<fn>()(ecx, collisionGroup0, collisionGroup1);
	}
}
