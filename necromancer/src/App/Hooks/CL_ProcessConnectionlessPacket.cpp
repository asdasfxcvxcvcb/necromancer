#include "../../SDK/SDK.h"
#include "../Features/CFG.h"
#include "../Features/Misc/IgnoreVAC/IgnoreVAC.h"

MAKE_SIGNATURE(CL_ProcessConnectionlessPacket, "engine.dll", "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 81 EC 30 05 00 00 44 8B 5A 30", 0x0);

MAKE_HOOK(CL_ProcessConnectionlessPacket, Signatures::CL_ProcessConnectionlessPacket.Get(), char, __fastcall,
	void* pHandler, netpacket_s* pPacket)
{
	if (F::IgnoreVAC->ShouldDropOOBPackets())
		return 0;

	return CALL_ORIGINAL(pHandler, pPacket);
}
