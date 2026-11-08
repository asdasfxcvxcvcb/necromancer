#include "AntiBackstab.h"

#include "../CFG.h"
#include "../Players/Players.h"
#include "../amalgam_port/AmalgamCompat.h"

namespace
{
	// Amalgam CTFPlayer.h
	constexpr float PLAYER_ORIGIN_COMPRESSION = 0.125f;

	bool IsIgnoredIndex(int iIndex)
	{
		PlayerPriority priority{};
		return F::Players->GetInfo(iIndex, priority) && priority.Ignored;
	}

	bool IsCheaterIndex(int iIndex)
	{
		PlayerPriority priority{};
		return F::Players->GetInfo(iIndex, priority) && priority.Cheater;
	}

	// Amalgam SDK::MaxSpeed (the crouch branch is not needed here)
	float MaxSpeed(C_TFPlayer* pPlayer)
	{
		float flSpeed = pPlayer->TeamFortress_CalculateMaxSpeed(false);
		if (pPlayer->InCond(TF_COND_SPEED_BOOST) || pPlayer->InCond(TF_COND_HALLOWEEN_SPEED_BOOST))
			flSpeed *= 1.35f;
		return flSpeed;
	}
}

int CAntiBackstab::Run(C_TFPlayer* pLocal, CUserCmd* pCmd, bool* pbSendPacket)
{
	// The pitch modes (2 Pitch, 3 Fake) write the 269/271 trick pitch; yaw mode
	// only turns us toward the spy, which never steers a swing away from a
	// swing aimed at that same spy. Amalgam's only stand-down beyond the basics
	// is G::Attacking == 1 (the attack tick itself), so yaw keeps protecting -
	// and carrying the melee aimbot's in-flight swing - instead of letting the
	// smack land on the user's raw view.
	const bool bPitchMode = CFG::Exploits_AntiBackstab_Mode >= 2;

	if (!CFG::Exploits_AntiBackstab_Mode || G::Attacking == 1 || !pLocal || pLocal->GetMoveType() != MOVETYPE_WALK || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return 0;

	// G::Attacking can be 0 on the tick a swing cmd gets its IN_ATTACK
	// (SDK::IsAttacking requires bCanPrimaryAttack, which engine prediction can
	// have already consumed) - for the pitch modes the button check keeps those
	// swing cmds from being pitched 269/271, otherwise the melee aimbot whiffs
	// every stab vs spies
	if (bPitchMode && pCmd->buttons & IN_ATTACK)
		return 0;

	// A melee smack traces at smack time with the eye angles of whatever cmds
	// the server has processed by then - the 269/271 pitch written here would
	// steer an in-flight swing into the ground and whiff the melee aimbot's
	// hit at the spy behind us. Stand down through the client's swing window
	// plus one latency round trip so the server's own smack window is covered.
	// Pitch modes only (yaw carries a swing at the spy rather than stealing
	// it), and melee weapons only: m_flSmackTime's offset is calibrated for
	// melee, and on other classes the same address holds unrelated state - a
	// charging sniper rifle's m_flChargedDamage lands there, which disarmed
	// the whole feature for as long as the scope was up.
	static int s_iSmackHoldUntil = 0;
	const auto pLocalWpnEnt = pLocal->m_hActiveWeapon().Get();
	const auto pLocalWpn = pLocalWpnEnt ? pLocalWpnEnt->As<C_TFWeaponBase>() : nullptr;
	if (pLocalWpn && bPitchMode && pLocalWpn->GetSlot() == WEAPON_SLOT_MELEE)
	{
		if (pLocalWpn->m_flSmackTime() > 0.f)
			s_iSmackHoldUntil = I::GlobalVars->tickcount
				+ TIME_TO_TICKS(pLocalWpn->GetSmackDelay() + SDKUtils::GetRealLatency());
		if (I::GlobalVars->tickcount < s_iSmackHoldUntil)
			return 0;
	}

	std::vector<std::pair<Vec3, C_BaseEntity*>> vTargets = {};
	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		const auto pPlayer = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;
		if (!pPlayer || pPlayer->IsDormant() || pPlayer->deadflag()
			|| pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE) || pPlayer->InCond(TF_COND_STEALTHED))
			continue;

		const auto pLocalWpnEnt = pLocal->m_hActiveWeapon().Get();
		const auto pLocalWpn = pLocalWpnEnt ? pLocalWpnEnt->As<C_TFWeaponBase>() : nullptr;
		const auto pActiveWpn = pPlayer->m_hActiveWeapon().Get();
		const auto pWeapon = pActiveWpn ? pActiveWpn->As<C_TFWeaponBase>() : nullptr;
		if (!pWeapon
			|| pWeapon->GetWeaponID() != TF_WEAPON_KNIFE
			&& !(H::AimUtils->GetWeaponType(pLocalWpn) == EWeaponType::MELEE && SDK::AttribHookValue(0, "crit_from_behind", pWeapon) > 0)
			&& !(pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER && SDK::AttribHookValue(0, "set_flamethrower_back_crit", pWeapon) == 1)
			|| IsIgnoredIndex(pPlayer->entindex()))
			continue;

		const Vec3 vLocalPos = pLocal->GetCenter();
		const Vec3 vTargetPos1 = pPlayer->GetCenter();
		const Vec3 vTargetPos2 = vTargetPos1 + pPlayer->m_vecVelocity() * SDKUtils::GetRealLatency();
		const float flDistance = std::max(std::max(MaxSpeed(pPlayer), MaxSpeed(pLocal)), pPlayer->m_vecVelocity().Length());
		const float flDistanceSqr = powf(flDistance, 2);
		if ((vLocalPos.DistToSqr(vTargetPos1) > flDistanceSqr || !SDK::VisPosWorld(pLocal, pPlayer, vLocalPos, vTargetPos1, MASK_SHOT | CONTENTS_GRATE))
			&& (vLocalPos.DistToSqr(vTargetPos2) > flDistanceSqr || !SDK::VisPosWorld(pLocal, pPlayer, vLocalPos, vTargetPos2, MASK_SHOT | CONTENTS_GRATE)))
			continue;

		vTargets.emplace_back(vTargetPos2, pEntity);
	}
	if (vTargets.empty())
		return 0;

	std::sort(vTargets.begin(), vTargets.end(), [&](const auto& a, const auto& b) -> bool
	{
		return pLocal->GetCenter().DistToSqr(a.first) < pLocal->GetCenter().DistToSqr(b.first);
	});

	const auto& tTargetPos = vTargets.front();
	switch (CFG::Exploits_AntiBackstab_Mode)
	{
	case 1: // Yaw
	{
		Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), tTargetPos.first);
		vAngleTo.x = pCmd->viewangles.x;
		H::AimUtils->FixMovement(pCmd, pCmd->viewangles, vAngleTo);
		pCmd->viewangles = vAngleTo;

		// force the packet out now - a spy in backstab range must see us
		// facing him this tick, not after the fake-lag/anti-aim choke window
		*pbSendPacket = true;

		return 1;
	}
	case 2: // Pitch
	case 3: // Fake
	{
		const bool bCheater = IsCheaterIndex(tTargetPos.second->entindex());
		// if the closest spy is a cheater, assume auto stab is being used, otherwise don't do anything if target is in front
		if (!bCheater)
		{
			auto fTargetIsBehind = [&]()
			{
				const float flCompDist = PLAYER_ORIGIN_COMPRESSION / 2;
				const float flSqCompDist = 0.0884f;

				Vec3 vToTarget = (pLocal->m_vecOrigin() - tTargetPos.first).To2D();
				const float flDist = vToTarget.Normalize();
				if (flDist < flSqCompDist)
					return true;

				const float flExtra = 2.f * flCompDist / flDist; // account for origin compression
				const float flPosVsTargetViewMinDot = 0.f - 0.0031f - flExtra;

				Vec3 vTargetForward; Math::AngleVectors(pCmd->viewangles, &vTargetForward);
				vTargetForward = vTargetForward.Normalized2D();

				const float flPosVsTargetViewDot = vToTarget.Dot(vTargetForward); // Behind?

				return flPosVsTargetViewDot > flPosVsTargetViewMinDot;
			};

			if (!fTargetIsBehind())
				return 0;
		}

		if (!bCheater || CFG::Exploits_AntiBackstab_Mode == 2)
		{
			pCmd->forwardmove *= -1;
			pCmd->viewangles.x = 269.f;
		}
		else
		{
			pCmd->viewangles.x = 271.f;
		}
		// may slip up some auto backstabs depending on mode, though we are still able to be stabbed

		*pbSendPacket = true; // same as Yaw - get the protective pitch out immediately

		return 2;
	}
	}

	return 0;
}
