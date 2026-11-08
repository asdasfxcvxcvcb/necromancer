#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

// Engine-level sound start - the final chokepoint every client entity sound
// passes through: client.dll resolves a soundscript to a wave and calls
// IEngineSoundClient003 (CEngineSoundClient::EmitSoundInternal), which builds
// a StartSoundParams_t and calls S_StartSound. Blocking here checks the
// actual wave name (CSfxTable::getname), which is what the block lists
// target: the frying pan's script names ("FryingPan.HitFlesh"/"HitWorld")
// contain no "pan_", but its resolved waves
// ("weapons/pan/melee_frying_pan_01.wav", "weapons/pan/pan_impact_world1.wav")
// do - which is why the frying pan block only works at this level.
//
// Signature verified against the Oct 5 2026 engine.dll: unique match at the
// function reached from CEngineSoundClient::EmitSoundInternal (direct call),
// pSfx null-check at params+0x10, StartSoundParams_t field layout confirmed
// at the call sites, CSfxTable::getname confirmed as vtable slot 0.
//
// The Oct 5 update did NOT move this function. An earlier rewrite pointed
// this hook at a "6-arg split" signature that matches a real but unrelated
// internal - its only caller is S_StartDynamicSound, a wrapper path entity
// sounds never take - so every entity sound bypassed the wave-level block.
// Footsteps kept working off the client emitter's script names
// ("Footstep.Run"), while the frying pan didn't: its script names
// ("FryingPan.HitFlesh"/"HitWorld") contain no "pan_" - only the resolved
// waves do, and they never got checked.
MAKE_SIGNATURE(S_StartSound, "engine.dll", "40 57 48 81 EC ? ? ? ? 48 83 79", 0x0);

// Type definitions for engine sound system (ported from Amalgam)
class CAudioSource;
class CSfxTable
{
public:
	virtual const char	*getname();
	const char			*GetFileName();
	void*				GetFileNameHandle();
	void				SetNamePoolIndex(int index);
	bool				IsPrecachedSound();
	void				OnNameChanged(const char *pName);
	int					m_namePoolIndex;
	CAudioSource		*pSource;
	bool				m_bUseErrorFilename : 1;
	bool				m_bIsUISound : 1;
	bool				m_bIsLateLoad : 1;
	bool				m_bMixGroupsCached : 1;
	byte				m_mixGroupCount;
	byte				m_mixGroupList[8];
};

struct StartSoundParams_t
{
	bool			staticsound;
	int				userdata;
	int				soundsource;
	int				entchannel;
	CSfxTable*		pSfx;
	Vec3			origin;
	Vec3			direction;
	bool			bUpdatePositions;
	float			fvol;
	soundlevel_t	soundlevel;
	int				flags;
	int				pitch;
	int				specialdsp;
	bool			fromserver;
	float			delay;
	int				speakerentity;
	bool			suppressrecording;
	int				initialStreamPosition;
};

// ShouldBlockSound is defined in CSoundEmitterSystem_EmitSound.cpp
extern bool ShouldBlockSound(const char* pSound);

// Manual init
namespace Hooks
{
	namespace S_StartSound
	{
		void Init();
		inline CHook Hook(Init);
		using fn = int(__fastcall*)(StartSoundParams_t& params);
		int __fastcall Func(StartSoundParams_t& params);
	}
}

void Hooks::S_StartSound::Init()
{
	auto addr = Signatures::S_StartSound.Get();
	if (addr)
	{
		Hook.Create(reinterpret_cast<void*>(addr), Func);
		MH_EnableHook(reinterpret_cast<void*>(addr));
	}
}

int __fastcall Hooks::S_StartSound::Func(StartSoundParams_t& params)
{
	if (params.pSfx && ShouldBlockSound(params.pSfx->getname()))
		return 0; // no handle - nothing to play

	return Hook.Original<fn>()(params);
}
