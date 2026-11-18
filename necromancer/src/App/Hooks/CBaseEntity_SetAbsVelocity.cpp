#include "../../SDK/SDK.h"

// Origin compression constant (same as Amalgam)
#define PLAYER_ORIGIN_COMPRESSION 0.125f

// Amalgam's AxisSet - computes velocity for a single axis from old/new position + simtime
class AxisSet
{
public:
	float m_flOldAxisValue = 0.f;
	float m_flOldSimulationTime = 0.f;
	float m_flNewAxisValue = 0.f;
	float m_flNewSimulationTime = 0.f;

	float Get(bool bZ = false) const
	{
		int iDeltaTicks = TIME_TO_TICKS(m_flNewSimulationTime - m_flOldSimulationTime);
		float flGravityCorrection = 0.f;
		if (bZ)
		{
			static auto sv_gravity = I::CVar->FindVar("sv_gravity");
			float flDeltaTicks = float(iDeltaTicks) + 0.5f;
			flGravityCorrection = (powf(flDeltaTicks, 2.f) - flDeltaTicks) / 2.f
				* (sv_gravity ? sv_gravity->GetFloat() : 800.f) * powf(TICK_INTERVAL, 2);
		}
		float flDeltaValue = m_flNewAxisValue - m_flOldAxisValue;
		float flTickVelocity = flDeltaValue + (flDeltaValue ? PLAYER_ORIGIN_COMPRESSION / 2 * sign(m_flNewAxisValue) : 0.f) - flGravityCorrection;
		return flTickVelocity / TICKS_TO_TIME(iDeltaTicks);
	}
};

// Amalgam's AxisInfo - groups 3 AxisSet for XYZ
class AxisInfo
{
public:
	AxisSet x = {}, y = {}, z = {};

	AxisSet& operator[](int i) { return ((AxisSet*)this)[i]; }
	AxisSet operator[](int i) const { return ((AxisSet*)this)[i]; }

	Vec3 Get(bool bGrounded = false) const
	{
		return { x.Get(), y.Get(), z.Get(!bGrounded) };
	}
};

MAKE_SIGNATURE(CBasePlayer_PostDataUpdate_SetAbsVelocityCall, "client.dll", "E8 ? ? ? ? 0F 28 74 24 ? 8B D6", 0x5);

MAKE_HOOK(CBaseEntity_SetAbsVelocity, Signatures::CBaseEntity_SetAbsVelocity.Get(), void, __fastcall,
	C_BasePlayer* ecx, const Vector& vecAbsVelocity)
{
	// Safety check - skip custom logic during level transitions or while not fully in game
	if (G::bLevelTransition || !ecx || !I::EngineClient->IsInGame())
	{
		CALL_ORIGINAL(ecx, vecAbsVelocity);
		return;
	}

	if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == Signatures::CBasePlayer_PostDataUpdate_SetAbsVelocityCall.Get())
	{
		const auto pPlayer = ecx->As<C_TFPlayer>();
		const int nPlayerIndex = pPlayer->entindex();
		if (pPlayer->IsDormant())
		{
			g_EntityMotionExt.SetAvgVelocity(nPlayerIndex, Vec3{});
			return CALL_ORIGINAL(ecx, vecAbsVelocity);
		}

		auto pRecords = g_EntityMotionExt.GetOrigins(nPlayerIndex);
		if (!pRecords || pRecords->empty())
		{
			g_EntityMotionExt.SetAvgVelocity(nPlayerIndex, Vec3{});
			return CALL_ORIGINAL(ecx, vecAbsVelocity);
		}

		auto& tOldRecord = pRecords->front();
		auto tNewRecord = CEntityMotionExt::VelFixOriginRecord{ pPlayer->m_vecOrigin() + Vec3(0, 0, pPlayer->m_vecMaxs().z), pPlayer->m_flSimulationTime() };

		int iDeltaTicks = TIME_TO_TICKS(tNewRecord.m_flSimulationTime - tOldRecord.m_flSimulationTime);
		float flDeltaTime = TICKS_TO_TIME(iDeltaTicks);
		if (iDeltaTicks <= 0)
		{
			g_EntityMotionExt.SetAvgVelocity(nPlayerIndex, Vec3{});
			return CALL_ORIGINAL(ecx, vecAbsVelocity);
		}

		static auto sv_lagcompensation_teleport_dist = I::CVar->FindVar("sv_lagcompensation_teleport_dist");
		float flDist = powf(sv_lagcompensation_teleport_dist ? sv_lagcompensation_teleport_dist->GetFloat() : 64.f, 2.f) * iDeltaTicks;
		if ((tNewRecord.m_vecOrigin - tOldRecord.m_vecOrigin).Length2DSqr() >= flDist)
		{
			pRecords->clear();
			g_EntityMotionExt.SetAvgVelocity(nPlayerIndex, Vec3{});
			return CALL_ORIGINAL(ecx, vecAbsVelocity);
		}

		bool bGrounded = IsOnGround(pPlayer);

		AxisInfo tAxisInfo = {};
		for (int i = 0; i < 3; i++)
		{
			tAxisInfo[i].m_flOldAxisValue = tOldRecord.m_vecOrigin[i];
			tAxisInfo[i].m_flNewAxisValue = tNewRecord.m_vecOrigin[i];
			tAxisInfo[i].m_flOldSimulationTime = tOldRecord.m_flSimulationTime;
			tAxisInfo[i].m_flNewSimulationTime = tNewRecord.m_flSimulationTime;

			if (i == 2 && bGrounded)
				break;

			float flOldPos1 = tOldRecord.m_vecOrigin[i], flOldPos2 = flOldPos1 + PLAYER_ORIGIN_COMPRESSION * sign(flOldPos1);
			float flNewPos1 = tNewRecord.m_vecOrigin[i], flNewPos2 = flNewPos1 + PLAYER_ORIGIN_COMPRESSION * sign(flNewPos1);
			if (!flOldPos1) flOldPos1 = -PLAYER_ORIGIN_COMPRESSION, flOldPos2 = PLAYER_ORIGIN_COMPRESSION;
			if (!flNewPos1) flNewPos1 = -PLAYER_ORIGIN_COMPRESSION, flNewPos2 = PLAYER_ORIGIN_COMPRESSION;

			// Velocity range from all origin compression combinations
			float flDeltas[4] = {
				flNewPos1 - flOldPos1, flNewPos2 - flOldPos1,
				flNewPos1 - flOldPos2, flNewPos2 - flOldPos2
			};
			std::sort(flDeltas, flDeltas + 4, std::less<float>());
			float flVelocityMin = flDeltas[0] / flDeltaTime;
			float flVelocityMax = flDeltas[3] / flDeltaTime;

			for (auto& tRecord : *pRecords)
			{
				if (tAxisInfo[i].m_flOldSimulationTime <= tRecord.m_flSimulationTime)
					continue;

				float flRewind = -ROUND_TO_TICKS(tNewRecord.m_flSimulationTime - tRecord.m_flSimulationTime);
				float flPositionMin = tAxisInfo[i].m_flNewAxisValue + flVelocityMax * flRewind;
				float flPositionMax = tAxisInfo[i].m_flNewAxisValue + flVelocityMin * flRewind;
				if (i == 2)
				{
					static auto sv_gravity = I::CVar->FindVar("sv_gravity");
					float flGravityCorrection = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * powf(flRewind + TICK_INTERVAL / 2, 2.f) / 2;
					flPositionMin -= flGravityCorrection;
					flPositionMax -= flGravityCorrection;
				}
				if (flPositionMin > tRecord.m_vecOrigin[i] || tRecord.m_vecOrigin[i] > flPositionMax)
					break;

				tAxisInfo[i].m_flOldAxisValue = tRecord.m_vecOrigin[i];
				tAxisInfo[i].m_flOldSimulationTime = tRecord.m_flSimulationTime;
			}
		}

		g_EntityMotionExt.SetAvgVelocity(nPlayerIndex, tAxisInfo.Get(bGrounded));
		CALL_ORIGINAL(ecx, (tNewRecord.m_vecOrigin - tOldRecord.m_vecOrigin) / flDeltaTime);
		return;
	}

	CALL_ORIGINAL(ecx, vecAbsVelocity);
}
