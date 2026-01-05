#pragma once

#include <algorithm>
#include <map>
#include <vector>

#include "TF2/IMatSystemSurface.h"
#include "TF2/cdll_int.h"
#include "TF2/icliententitylist.h"
#include "TF2/ivmodelinfo.h"
#include "TF2/ivdebugoverlay.h"
#include "TF2/clientmode_shared.h"
#include "TF2/imaterialsystem.h"
#include "TF2/ivmodelrender.h"
#include "TF2/ienginevgui.h"
#include "TF2/ivrenderview.h"
#include "TF2/globalvars_base.h"
#include "TF2/icvar.h"
#include "TF2/prediction.h"
#include "TF2/igamemovement.h"
#include "TF2/netmessages.h"
#include "TF2/iinputsystem.h"
#include "TF2/iinput.h"
#include "TF2/istudiorender.h"
#include "TF2/igameevents.h"
#include "TF2/c_tf_playerresource.h"
#include "TF2/itoolentity.h"
#include "TF2/random.h"
#include "TF2/client.h"
#include "TF2/iviewrender.h"
#include "TF2/iviewrender_beams.h"
#include "TF2/vphysics.h"
#include "TF2/renderutils.h"

#include "TF2/keyvalues.h"
#include "TF2/c_baseobject.h"
#include "TF2/imaterialvar.h"
#include "TF2/itexture.h"
#include "TF2/MD5.h"
#include "TF2/c_tf_player.h"
#include "TF2/CTFPartyClient.h"
#include "TF2/CTFGCClientSystem.h"

#include "Helpers/Draw/Draw.h"
#include "Helpers/Entities/Entities.h"
#include "Helpers/Fonts/Fonts.h"
#include "Helpers/Input/Input.h"
#include "Helpers/AimUtils/AimUtils.h"

#include "Impl/TraceFilters/TraceFilters.h"

#include "Steam/SteamInterfaces.h"

#define PRINT(...) I::CVar->ConsoleColorPrintf({ 20, 220, 55, 255 }, __VA_ARGS__)

//MAKE_INTERFACE_SIGNATURE(int, RandomSeed, "client.dll", "C7 05 ? ? ? ? ? ? ? ? C3 8B 41", 0x2, 1);

MAKE_SIGNATURE(RandomSeed, "client.dll", "0F B6 1D ? ? ? ? 89 9D", 0x0);
MAKE_SIGNATURE(SharedRandomInt, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 8B FA 41 8B D8", 0x0);
MAKE_SIGNATURE(BInEndOfMatch, "client.dll", "48 83 EC ? 48 8B 05 ? ? ? ? 48 85 C0 74 ? 83 78 ? ? 75", 0x0);
MAKE_SIGNATURE(GetClientInterpAmount, "client.dll", "40 53 48 83 EC ? 8B 05 ? ? ? ? A8 ? 75 ? 48 8B 0D ? ? ? ? 48 8D 15", 0x0);
MAKE_SIGNATURE(LookupSequence, "client.dll", "48 89 5C 24 ? 55 48 83 EC ? 48 8B EA 48 8B D9 48 85 C9", 0x0);

namespace SDKUtils
{
	inline BOOL CALLBACK TeamFortressWindow(HWND hwnd, LPARAM lParam) // do not use me
	{
		char windowTitle[1024];
		GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
		if (std::string(windowTitle).find("Team Fortress 2 - ") == 0) // support both dx9 & vulkan
			*reinterpret_cast<HWND*>(lParam) = hwnd;

		return TRUE;
	}

	inline HWND GetTeamFortressWindow()
	{
		static HWND hwWindow = nullptr;
		while (!hwWindow)
		{
			EnumWindows(TeamFortressWindow, reinterpret_cast<LPARAM>(&hwWindow));
			Sleep(100);
		}
		return hwWindow;
	}

	inline bool IsGameWindowInFocus()
	{
		return GetForegroundWindow() == SDKUtils::GetTeamFortressWindow();
	}

	inline int* RandomSeed()
	{
		static auto dest = Memory::RelToAbs(Signatures::RandomSeed.Get());
		return reinterpret_cast<int*>(dest);
	}

	inline void GetProjectileFireSetupRebuilt(C_TFPlayer *player, Vec3 offset, const Vec3 &ang_in, Vec3 &pos_out, Vec3 &ang_out, bool pipes)
	{
		static auto cl_flipviewmodels{ I::CVar->FindVar("cl_flipviewmodels") };

		if (!cl_flipviewmodels)
		{
			return;
		}

		if (cl_flipviewmodels->GetBool())
		{
			offset.y *= -1.0f;
		}

		Vec3 forward{}, right{}, up{};
		Math::AngleVectors(ang_in, &forward, &right, &up);

		auto shoot_pos{ player->GetShootPos() };

		pos_out = shoot_pos + (forward * offset.x) + (right * offset.y) + (up * offset.z);

		if (pipes)
		{
			ang_out = ang_in;
		}
		else
		{
			// Trace to find where the wall is (like Amalgam does)
			// This allows the projectile angle to be adjusted to avoid corners
			// Use CTraceFilterCollideable like Amalgam - traces world/props but skips player checks
			auto end_pos{ shoot_pos + (forward * 2000.0f) };

			CGameTrace trace = {};
			CTraceFilterCollideable filter = {};
			filter.pSkip = player;
			filter.iType = SKIP_CHECK;  // Skip player/entity collision checks, only trace world/props
			
			Ray_t ray;
			ray.Init(shoot_pos, end_pos);
			I::EngineTrace->TraceRay(ray, MASK_SOLID, &filter, &trace);
			
			if (trace.DidHit() && trace.fraction > 0.1f)
				end_pos = trace.endpos;

			Math::VectorAngles(end_pos - pos_out, ang_out);
		}
	}

	static float GetLerp()
	{
		static ConVar *cl_interp = I::CVar->FindVar("cl_interp");
		static ConVar *cl_interp_ratio = I::CVar->FindVar("cl_interp_ratio");
		static ConVar *cl_updaterate = I::CVar->FindVar("cl_updaterate");

		return std::max(cl_interp->GetFloat(), cl_interp_ratio->GetFloat() / cl_updaterate->GetFloat());
	}

	static Vec3 GetHitboxPosFromMatrix(C_BaseAnimating *pAnimating, int nHitbox, matrix3x4_t *pMatrix)
	{
		auto pModel = pAnimating->GetModel();

		if (!pModel)
			return {};
		
		auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);

		if (!pHDR)
			return {};

		auto pSet = pHDR->pHitboxSet(pAnimating->m_nHitboxSet());

		if (!pSet)
			return {};

		auto pBox = pSet->pHitbox(nHitbox);

		if (!pBox)
			return {};

		Vec3 vOut = {};
		Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, pMatrix[pBox->bone], vOut);
		return vOut;
	}

	static void GetHitboxInfoFromMatrix(C_BaseAnimating *pAnimating, int nHitbox, matrix3x4_t *pMatrix, Vec3 *pCenter, Vec3 *pMins, Vec3 *pMaxs)
	{
		auto pModel = pAnimating->GetModel();

		if (!pModel)
			return;

		auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);

		if (!pHDR)
			return;

		auto pSet = pHDR->pHitboxSet(pAnimating->m_nHitboxSet());

		if (!pSet)
			return;

		auto pBox = pSet->pHitbox(nHitbox);

		if (!pBox)
			return;

		if (pMins)
			*pMins = pBox->bbmin;

		if (pMaxs)
			*pMaxs = pBox->bbmax;

		if (pCenter)
			Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, pMatrix[pBox->bone], *pCenter);
	}

	inline float GetLatency()
	{
		if (auto pNet = I::EngineClient->GetNetChannelInfo())
			return pNet->GetLatency(FLOW_INCOMING) + pNet->GetLatency(FLOW_OUTGOING);

		return 0.0f;
	}

	// Get real latency without fake latency (for projectile prediction)
	// Uses GetAvgLatency which gives actual network latency, not affected by fake latency manipulation
	inline float GetRealLatency()
	{
		if (auto pNet = I::EngineClient->GetNetChannelInfo())
		{
			// GetAvgLatency returns the actual network latency (averaged over time)
			// This is NOT affected by the fake latency sequence number manipulation
			return pNet->GetAvgLatency(FLOW_INCOMING) + pNet->GetAvgLatency(FLOW_OUTGOING);
		}
		return 0.0f;
	}

	inline float GetGravity()
	{
		static ConVar *sv_gravity = I::CVar->FindVar("sv_gravity");
		return sv_gravity ? sv_gravity->GetFloat() : 0.0f;
	}

	static float RandomFloat(float min_val, float max_val)
	{
		static auto fn = reinterpret_cast<float(__cdecl *)(float, float)>(GetProcAddress(GetModuleHandleA("vstdlib.dll"), "RandomFloat"));
		return fn(min_val, max_val);
	}

	static int RandomInt(int min_val, int max_val)
	{
		static auto fn = reinterpret_cast<int(__cdecl *)(int, int)>(GetProcAddress(GetModuleHandleA("vstdlib.dll"), "RandomInt"));
		return fn(min_val, max_val);
	}

	static void RandomSeed(unsigned int seed)
	{
		static auto fn = reinterpret_cast<void(__cdecl *)(unsigned int)>(GetProcAddress(GetModuleHandleA("vstdlib.dll"), "RandomSeed"));
		fn(seed);
	}

	static int FindCmdNumWithSeed(int nCommandNumber, int nDesiredSeed)
	{
		int nCmdNum = nCommandNumber, nIter = 0;

		while (nIter++ < 1024)
		{
			int nSeed = MD5_PseudoRandom(++nCmdNum) & std::numeric_limits<int>::max();

			if ((nSeed & 255) == nDesiredSeed)
				return nCmdNum;
		}

		return nCommandNumber;
	}

	static int SharedRandomInt(const char *sharedname, int iMinVal, int iMaxVal, int additionalSeed)
	{
		using fn = int(__fastcall *)(const char *, int, int, int);
		return reinterpret_cast<fn>(Signatures::SharedRandomInt.Get())(sharedname, iMinVal, iMaxVal, additionalSeed);
	}

	static bool BInEndOfMatch() {
		return reinterpret_cast<bool(__cdecl *)()>(Signatures::BInEndOfMatch.Get())();
	}

	static int LookupSequence(CStudioHdr *pstudiohdr, const char *label) {
		return reinterpret_cast<int(__cdecl *)(CStudioHdr *, const char *)>(Signatures::LookupSequence.Get())(pstudiohdr, label);
	};

	static int CreateTextureFromArray(const unsigned char *rgba, int w, int h)
	{
		int nTextureIdOut = I::MatSystemSurface->CreateNewTextureID(true);
		I::MatSystemSurface->DrawSetTextureRGBAEx(nTextureIdOut, rgba, w, h, IMAGE_FORMAT_BGRA8888);
		return nTextureIdOut;
	}

	static void WalkTo(CUserCmd* pCmd, const Vec3& from, const Vec3& to, float scale)
	{
		auto delta = to - from;

		if (delta.Length() == 0.0f)
			return;

		Vec3 deltaDir{};
		Math::VectorAngles({ delta.x, delta.y, 0.0f }, deltaDir);

		const auto yaw{ DEG2RAD(deltaDir.y - pCmd->viewangles.y) };
		pCmd->forwardmove = std::cosf(yaw) * (450.0f * scale);
		pCmd->sidemove = -std::sinf(yaw) * (450.0f * scale);
	}
}

namespace G
{
	inline bool bSilentAngles = false;
	inline bool bPSilentAngles = false;
	inline bool bChoking = false;  // Amalgam: currently choking packets
	inline bool bLevelTransition = false;  // True during level change - prevents entity access
	inline int nTargetIndex = -1;
	inline float flAimbotFOV = 0.0f;
	inline bool bCanPrimaryAttack = false;
	inline bool bCanSecondaryAttack = false;
	inline bool bCanHeadshot = false;
	inline bool bReloading = false;  // Amalgam: true when reloading but have ammo and can't attack yet
	inline int Attacking = 0;  // Amalgam: 0 = not attacking, 1 = attacking this tick
	inline bool Throwing = false;  // Amalgam: true when throwing a projectile (e.g. sandman ball)
	inline int nOldButtons = 0;
	inline Vec3 vUserCmdAngles = {};
	inline CUserCmd* CurrentUserCmd = nullptr;
	inline CUserCmd* LastUserCmd = nullptr;  // Amalgam: previous tick's user cmd
	inline CUserCmd OriginalCmd = {};  // Amalgam: unmodified user cmd for this tick
	
	// Smooth aimbot view angles - used to restore view when AA is active
	inline Vec3 vSmoothAimAngles = {};
	inline bool bUseSmoothAimAngles = false;

	struct VelFixRecord_t
	{
		Vec3 m_vecOrigin = {};
		int m_fFlags = 0;
		float m_flSimulationTime = 0.0f;
	};

	inline std::map<C_BasePlayer *, VelFixRecord_t> mapVelFixRecords = {};

	inline bool bFiring = false;
	inline int nTicksTargetSame = 0;
	inline int nTargetIndexEarly = 0;
	inline int nTicksSinceCanFire = 0;

	inline bool bUpdatingAnims = false;
	inline bool bSimulatingProjectile = false;

	inline bool bStartedFakeTaunt = false;
	inline float flFakeTauntStartYaw = 0.0f;

	// Neckbreaker: flip viewmodels for this shot
	inline bool bNeckbreakerFlip = false;

	// ============================================
	// Amalgam-style path storage for visualization
	// ============================================

	// Path drawing styles (matches Amalgam's Vars::Visuals::Simulation::StyleEnum)
	namespace PathStyle {
		enum {
			Off = 0,
			Line = 1,
			Separators = 2,
			Spaced = 3,
			Arrows = 4,
			Boxes = 5
		};
	}

	// Structure for storing drawable paths
	struct DrawPath_t
	{
		std::vector<Vec3> m_vPath = {};
		float m_flTime = 0.f;           // Time when path expires (or negative for tick-based)
		Color_t m_tColor = { 255, 255, 255, 255 };
		int m_iStyle = PathStyle::Line;
		bool m_bZBuffer = false;        // Whether to use depth testing
	};

	// Structure for storing drawable lines
	struct DrawLine_t
	{
		Vec3 m_vStart = {};
		Vec3 m_vEnd = {};
		float m_flTime = 0.f;
		Color_t m_tColor = { 255, 255, 255, 255 };
		bool m_bZBuffer = false;
	};

	// Structure for storing drawable boxes
	struct DrawBox_t
	{
		Vec3 m_vOrigin = {};
		Vec3 m_vMins = {};
		Vec3 m_vMaxs = {};
		Vec3 m_vAngles = {};
		float m_flTime = 0.f;
		Color_t m_tColorEdge = { 255, 255, 255, 255 };
		Color_t m_tColorFace = { 255, 255, 255, 50 };
		bool m_bZBuffer = false;
	};

	// Global storage for paths/lines/boxes to be rendered
	inline std::vector<DrawPath_t> PathStorage = {};
	inline std::vector<DrawLine_t> LineStorage = {};
	inline std::vector<DrawBox_t> BoxStorage = {};

	// Helper to clear all visual storage
	inline void ClearVisualStorage()
	{
		PathStorage.clear();
		LineStorage.clear();
		BoxStorage.clear();
	}

	// Helper to update timed paths (call from CL_Move or similar)
	inline void UpdatePathStorage()
	{
		// Remove expired paths
		PathStorage.erase(
			std::remove_if(PathStorage.begin(), PathStorage.end(),
				[](const DrawPath_t& path) {
					if (path.m_flTime < 0.f)
						return false; // Tick-based, handled elsewhere
					return path.m_flTime < I::GlobalVars->curtime;
				}),
			PathStorage.end()
		);

		LineStorage.erase(
			std::remove_if(LineStorage.begin(), LineStorage.end(),
				[](const DrawLine_t& line) {
					return line.m_flTime < I::GlobalVars->curtime;
				}),
			LineStorage.end()
		);

		BoxStorage.erase(
			std::remove_if(BoxStorage.begin(), BoxStorage.end(),
				[](const DrawBox_t& box) {
					return box.m_flTime < I::GlobalVars->curtime;
				}),
			BoxStorage.end()
		);
	}
}

namespace Shifting
{
	inline int nAvailableTicks = 0;
	inline bool bRecharging = false;
	inline bool bShifting = false;
	inline bool bRapidFireWantShift = false;
	inline bool bShiftingWarp = false;
	inline bool bShiftingRapidFire = false;  // True when shifting for rapid fire (not warp)
	inline int nCurrentShiftTick = 0;        // Current tick index during rapid fire shift (0-indexed)
	inline int nTotalShiftTicks = 0;         // Total ticks being shifted

	// Saved command state (from Amalgam)
	inline CUserCmd SavedCmd = {};
	inline bool bSavedAngles = false;
	inline bool bHasSavedCmd = false;

	// Shift start tracking for anti-warp (from Amalgam)
	inline Vec3 vShiftStartPos = {};
	inline bool bStartedOnGround = false;

	// Target tracking for rapid fire aimbot
	inline int nRapidFireTargetIndex = -1;
	inline float flRapidFireSimTime = 0.0f;

	// Tick tracking for high ping compensation
	// Tracks server tick acknowledgments to predict optimal shift timing
	inline int nLastServerTick = 0;           // Last acknowledged server tick
	inline int nLastCommandAck = 0;           // Last acknowledged command number
	inline int nTicksAhead = 0;               // How many ticks we're ahead of server
	inline float flLastLatency = 0.0f;        // Last measured latency
	inline int nPredictedServerTick = 0;      // Our prediction of current server tick

	// Update tick tracking - call this every frame
	inline void UpdateTickTracking(int nServerTick, int nCommandAck, float flLatency)
	{
		// Track how many ticks ahead we are
		if (nServerTick > nLastServerTick)
		{
			nTicksAhead = I::GlobalVars->tickcount - nServerTick;
			nLastServerTick = nServerTick;
		}
		
		nLastCommandAck = nCommandAck;
		flLastLatency = flLatency;
		
		// Predict current server tick based on latency
		// Server tick = our tick - (latency in ticks)
		int nLatencyTicks = static_cast<int>(flLatency / TICK_INTERVAL);
		nPredictedServerTick = I::GlobalVars->tickcount - nLatencyTicks;
	}

	// Check if we should delay shift based on tick tracking
	// Returns true if we should wait, false if safe to shift
	inline bool ShouldDelayShift(int nTickTrackingMode)
	{
		// Mode 0 = Disabled - no delay
		if (nTickTrackingMode == 0)
			return false;
		
		// Mode 1 = Linear - simple latency-based delay
		if (nTickTrackingMode == 1)
		{
			// If we're too far ahead of server, wait
			// This prevents commands from arriving too bunched up
			int nMaxAhead = static_cast<int>(flLastLatency / TICK_INTERVAL) + 2;
			return nTicksAhead > nMaxAhead;
		}
		
		return false;
	}

	inline void Reset()
	{
		nAvailableTicks = 0;
		bRecharging = false;
		bShifting = false;
		bRapidFireWantShift = false;
		bShiftingWarp = false;
		bShiftingRapidFire = false;
		nCurrentShiftTick = 0;
		nTotalShiftTicks = 0;
		// Reset saved command state
		bHasSavedCmd = false;
		bSavedAngles = false;
		// Reset shift start tracking
		bStartedOnGround = false;
		// Reset rapid fire target tracking
		nRapidFireTargetIndex = -1;
		flRapidFireSimTime = 0.0f;
		// Reset tick tracking
		nLastServerTick = 0;
		nLastCommandAck = 0;
		nTicksAhead = 0;
		flLastLatency = 0.0f;
		nPredictedServerTick = 0;
	}
}

#define GET_ENT_FROM_USER_ID(userid) I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(userid))

struct ShaderStencilState_t
{
	bool m_bEnable = false;
	StencilOperation_t m_FailOp = {};
	StencilOperation_t m_ZFailOp = {};
	StencilOperation_t m_PassOp = {};
	StencilComparisonFunction_t m_CompareFunc = {};
	int m_nReferenceValue = 0;
	uint32 m_nTestMask = 0;
	uint32 m_nWriteMask = 0;

	ShaderStencilState_t()
	{
		m_bEnable = false;
		m_PassOp = m_FailOp = m_ZFailOp = STENCILOPERATION_KEEP;
		m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
		m_nReferenceValue = 0;
		m_nTestMask = m_nWriteMask = 0xFFFFFFFF;
	}

	void SetStencilState(IMatRenderContext *pRenderContext)
	{
		pRenderContext->SetStencilEnable(m_bEnable);
		pRenderContext->SetStencilFailOperation(m_FailOp);
		pRenderContext->SetStencilZFailOperation(m_ZFailOp);
		pRenderContext->SetStencilPassOperation(m_PassOp);
		pRenderContext->SetStencilCompareFunction(m_CompareFunc);
		pRenderContext->SetStencilReferenceValue(m_nReferenceValue);
		pRenderContext->SetStencilTestMask(m_nTestMask);
		pRenderContext->SetStencilWriteMask(m_nWriteMask);
	}
};

inline float GetClientInterpAmount()
{
	return reinterpret_cast<float(__cdecl *)()>(Signatures::GetClientInterpAmount.Get())();
}

inline double Plat_FloatTime()
{
	static auto fn{ reinterpret_cast<double(__cdecl *)()>(GetProcAddress(GetModuleHandleA("tier0.dll"), "Plat_FloatTime")) };

	return fn();
}