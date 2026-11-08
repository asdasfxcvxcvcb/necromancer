#include "../../SDK/SDK.h"
#include "../Features/CFG.h"
#include "../Features/Misc/IgnoreVAC/IgnoreVAC.h"

MAKE_SIGNATURE(CL_SendConnectPacket, "engine.dll", "48 89 5C 24 18 48 89 74 24 20 55 41 56 41 57 48 8D AC 24 70 F2 FF FF 48 81 EC 90 0E 00 00 48 8B", 0x0);

MAKE_HOOK(CL_SendConnectPacket, Signatures::CL_SendConnectPacket.Get(), __int64, __fastcall,
	void* a1, unsigned int a2, unsigned int a3, __int64 a4, char a5)
{
	const __int64 iResult = CALL_ORIGINAL(a1, a2, a3, a4, a5);

	F::IgnoreVAC->OnTicketSent();

	return iResult;
}
