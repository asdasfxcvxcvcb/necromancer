#pragma once
#include "../../../SDK/SDK.h"

class CNetworking {
public:
	void CL_Move(float AccumulatedExtraSamples, bool FinalTick);
	bool bSendPacket = false;
	void CL_Sendmove();
	int get_latest_command_number();

};

MAKE_SINGLETON_SCOPED(CNetworking, Networking, F);
