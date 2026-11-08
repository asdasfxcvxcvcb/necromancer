#pragma once
#include "../../../../SDK/SDK.h"

#include <string>

class CIgnoreVAC
{
public:
	void Run();
	void OnTicketSent();
	bool ShouldDropOOBPackets();
	bool ShouldDropInboundPackets();

private:
	void SetResendOverride(bool bOn);

	bool m_bWasEnabled = false;
	float m_flBlackoutUntil = 0.0f;
	float m_flInGameSince = 0.0f;
};

MAKE_SINGLETON_SCOPED(CIgnoreVAC, IgnoreVAC, F);
