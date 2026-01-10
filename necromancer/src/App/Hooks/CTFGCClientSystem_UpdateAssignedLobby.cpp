#include "../../SDK/SDK.h"

// F2P Chat Bypass - automatically enables chat for F2P accounts
// This hook runs when the lobby assignment is updated and sets the account as premium

MAKE_HOOK(CTFGCClientSystem_UpdateAssignedLobby, Signatures::CTFGCClientSystem_UpdateAssignedLobby.Get(), bool, __fastcall,
	void* rcx)
{
	bool bReturn = CALL_ORIGINAL(rcx);

	// Always bypass F2P chat restriction (no menu option needed)
	if (rcx && I::TFGCClientSystem)
		I::TFGCClientSystem->SetNonPremiumAccount(false);

	return bReturn;
}
