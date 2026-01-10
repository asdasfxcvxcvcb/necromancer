#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_HOOK(CTFPartyClient_RequestQueueForMatch, Signatures::CTFPartyClient_RequestQueueForMatch.Get(), void, __fastcall,
	void* rcx, int eMatchGroup)
{
	// Force ping refresh before queuing if region selector is active
	// This ensures our modified ping values are used for matchmaking
	if (CFG::Exploits_Region_Selector_Active && I::TFGCClientSystem)
	{
		I::TFGCClientSystem->SetPendingPingRefresh(true);
		I::TFGCClientSystem->PingThink();
	}

	CALL_ORIGINAL(rcx, eMatchGroup);
}
