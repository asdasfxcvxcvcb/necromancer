#pragma once

#include "../../../SDK/SDK.h"

// Port of Amalgam's AntiBackstab (Misc::Automation). Watches enemies that can
// backstab or back-crit us and overrides our angles when one gets in range:
// Yaw   - face the closest spy
// Pitch - look straight up (269) so the server's 2D backstab angle check fails
// Fake  - pitch 271, only engaging spies tagged as cheaters
// Unlike Amalgam this is not limited to send ticks: when it engages it also
// force-unchokes pbSendPacket (RapidFire-style) so the protective angle goes
// out immediately instead of waiting out the fake-lag/anti-aim choke window.
// Returns 0 when nothing engaged, 1 for a yaw override, 2 for a pitch override;
// FakeAngle::Run consumes the return like Amalgam's AntiAim::Run does.
class CAntiBackstab
{
public:
	int Run(C_TFPlayer* pLocal, CUserCmd* pCmd, bool* pbSendPacket);
};

MAKE_SINGLETON_SCOPED(CAntiBackstab, AntiBackstab, F);
