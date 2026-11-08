#pragma once

#include "../Singleton/Singleton.h"
#include <Windows.h>
#include <string>

// Lightweight crash context — updated each frame so we know what was running when it blew up
struct CrashContext_t
{
	const char* m_pszLastFeature = "none";       // Which feature was last running
	const char* m_pszLastSubFeature = "none";    // Sub-operation within the feature (e.g. "SetupBones", "ValidateEntity")
	const char* m_pszLastHook = "none";          // Which hook was active
	int m_nTargetIndex = -1;                     // Aimbot target
	int m_nCommandNumber = 0;                    // Current cmd number
	int m_nEntityIndex = -1;                     // Entity index being processed
	bool m_bInGame = false;                      // Engine says we're in game
	bool m_bLevelTransition = false;             // Level transition in progress
	char m_szMapName[64] = {};                   // Current map name
};

class CCrashHandler
{
private:
    static PVOID s_pVectoredHandle;
    static bool s_Initialized;

public:
    void Initialize();
    void Shutdown();

    static CrashContext_t s_Context;  // Updated by hooks/features each frame

    // Increment this around a __try/__except-guarded region (decrement after,
    // including in the __except path) so the vectored handler skips reporting
    // faults the local __except is meant to swallow silently - otherwise every
    // swallowed fault would still pop a crash dialog and write a minidump.
    // Must be used from functions with no unwinding objects (C2712).
    static inline thread_local int s_iSuppressDepth = 0;
};

MAKE_SINGLETON_SCOPED(CCrashHandler, CrashHandler, F);
