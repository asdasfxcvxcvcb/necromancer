#include "../../SDK/SDK.h"
#include "../Features/CFG.h"
#include "../Features/Misc/IgnoreVAC/IgnoreVAC.h"

MAKE_SIGNATURE(INetChannel_ProcessPacket, "engine.dll", "44 88 44 24 18 48 89 54 24 10 53 56 41 55 41 56 41 57 48 83 EC 70 4C 8D 7A 20 45 33 ED 48 8B F2 4C 8B F1 45 39 6F 0C", 0x0);

MAKE_HOOK(INetChannel_ProcessPacket, Signatures::INetChannel_ProcessPacket.Get(), void, __fastcall,
	CNetChannel* pNet, netpacket_s* pPacket, bool bHasHeader)
{
	if (F::IgnoreVAC->ShouldDropInboundPackets())
		return;

	CALL_ORIGINAL(pNet, pPacket, bHasHeader);
}
