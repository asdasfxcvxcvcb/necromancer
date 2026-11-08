#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/Players/Players.h"

// Signature for CProxyAnimatedWeaponSheen::OnBind from client.dll
// Oct 5 2026 TF2 update changed the prologue to the r11 frame style. Derived
// via the class RTTI vtable (slot 1 = OnBind, saves the entity param in rdx).
MAKE_SIGNATURE(CProxyAnimatedWeaponSheen_OnBind, "client.dll",
	"4C 8B DC 49 89 53 10 55 57 41 54 49 8D 6B ? 48 81 EC ? ? ? ? 48 8B 41 08", 0x0);

// From the TF2 Source SDK (c_tf_player.cpp):
//
// CProxyAnimatedWeaponSheen::OnBind sets these material vars:
//   m_pTintVar       -> "$sheenmaptint"   (Vec4: r,g,b,a — 0,0,0,0 = invisible)
//   m_pSheenIndexVar -> "$sheenindex"     (Int: 0 = off, 1-7 = sheen type)
//
// When no killstreak attribute is found, RunNoProxy() sets:
//   tint = (0,0,0) and sheenindex = 0  — effectively disabling the sheen
//
// By hooking OnBind and overriding these values AFTER the original runs,
// we can force-enable sheens on any weapon and set custom colors.
//
// Sheen index values (from g_KillStreakEffectsBase):
//   0 = Off
//   1 = Team Color (Red/Blue)
//   2 = Orange (Hot Rod)
//   3 = Fire (Manndarin)
//   4 = Green (Mean Green)
//   5 = Cyan (Agonizing Emerald)
//   6 = Purple (Villainous Violet)
//   7 = Pink (Harmonious Haberdashery)
//
// Struct layout (x64, from reversed binary — matches forum post):
//   CBaseAnimatedTextureProxy base:
//     +0x00  vtable
//     +0x08  IMaterialVar* m_AnimatedTextureVar
//     +0x10  IMaterialVar* m_AnimatedTextureFrameNumVar
//     +0x18  float m_FrameRate
//     +0x1C  bool  m_WrapAnimation
//   CProxyAnimatedWeaponSheen adds:
//     +0x20  IMaterialVar* m_pSheenIndexVar   ($sheenindex)
//     +0x28  IMaterialVar* m_pTintVar         ($sheenmaptint)
//     +0x30  IMaterialVar* m_pSheenVar        ($sheenmap)
//     +0x38  IMaterialVar* m_pSheenMaskVar    ($sheenmapmask)
//     +0x40  IMaterialVar* m_pScaleXVar
//     +0x48  IMaterialVar* m_pScaleYVar
//     +0x50  IMaterialVar* m_pOffsetXVar
//     +0x58  IMaterialVar* m_pOffsetYVar
//     +0x60  IMaterialVar* m_pDirectionVar
//     +0x68  float m_flNextStartTime
//     +0x6C  float m_flScaleX
//     +0x70  float m_flScaleY
//     +0x74  float m_flSheenOffsetX
//     +0x78  float m_flSheenOffsetY
//     +0x7C  int   m_iSheenDir

// Struct layout verified from IDA decompilation of current TF2 client.dll
// Matches forum post: https://www.unknowncheats.me/forum/team-fortress-2-a/732485
class CProxyAnimatedWeaponSheen {
public:
	char pad_0x00[0x08];                          // vtable
	IMaterialVar* m_AnimatedTextureVar;           // +0x08 ($sheenmap)
	IMaterialVar* m_AnimatedTextureFrameNum;      // +0x10 ($sheenmapframe)
	char pad_0x18[0x08];                          // m_FrameRate + m_WrapAnimation + padding
	IMaterialVar* m_pSheenIndex;                  // +0x20 ($sheenindex)
	IMaterialVar* m_pTint;                        // +0x28 ($sheenmaptint)
	char pad_0x30[0x10];                          // $sheenmap + $sheenmapmask
	IMaterialVar* m_pScaleX;                      // +0x40
	IMaterialVar* m_pScaleY;                      // +0x48
	IMaterialVar* m_pOffsetX;                     // +0x50
	IMaterialVar* m_pOffsetY;                    // +0x58
	IMaterialVar* m_pDirection;                   // +0x60
	float m_flNextStartTime;                      // +0x68
	float m_flScaleX;                             // +0x6C
	float m_flScaleY;                             // +0x70
	float m_flSheenOffsetX;                       // +0x74
	float m_flSheenOffsetY;                       // +0x78
	int m_iSheenDir;                              // +0x7C
};

// Tracks per-proxy animation state for the sheen wrap cycle
static float s_flSheenStartTime = -1.0f; // -1 = not yet initialized
static float s_flLastCurTime = -1.0f;    // Tracks curtime to detect map changes

MAKE_HOOK(CProxyAnimatedWeaponSheen_OnBind, Signatures::CProxyAnimatedWeaponSheen_OnBind.Get(), void, __fastcall,
	CProxyAnimatedWeaponSheen* ecx, void* pEntity)
{
	// Always call original — it handles animation frames, scale, offset, etc.
	// For weapons WITH killstreak: original does full animation + tint + sheenindex.
	// For weapons WITHOUT killstreak: original calls RunNoProxy() (tint=0,0,0 sheenindex=0, no animation).
	CALL_ORIGINAL(ecx, pEntity);

	if (!CFG::Visuals_Sheen_Active)
		return;

	// Determine if this entity's owner matches any enabled target
	// pEntity is the IClientRenderable* passed to OnBind (SDK casts it to C_BaseEntity*)
	if (pEntity)
	{
		auto pRend = reinterpret_cast<IClientRenderable*>(pEntity);
		auto pUnk = pRend->GetIClientUnknown();
		if (pUnk)
		{
			auto pBaseEntity = pUnk->GetBaseEntity();
			if (pBaseEntity)
			{
				auto pLocal = H::Entities->GetLocal();
				if (pLocal)
				{
					// Get local player's active weapon for viewmodel detection
					auto pLocalCombat = pLocal->As<C_BaseCombatCharacter>();
					C_BaseEntity* pLocalActiveWeapon = pLocalCombat ? pLocalCombat->m_hActiveWeapon().Get() : nullptr;

					// Walk the owner chain: weapon -> player, or viewmodel -> weapon -> player
					auto pOwner = pBaseEntity->m_hOwnerEntity().Get();

					// If no direct owner, check if this entity IS the local player or their active weapon
					if (!pOwner)
					{
						if (CFG::Visuals_Sheen_Local)
						{
							if (pBaseEntity == pLocal || pBaseEntity == pLocalActiveWeapon)
								goto should_override;
						}
						return; // Can't determine owner, don't override
					}

					// If owner is the local player, this is a local weapon/viewmodel
					if (pOwner == pLocal && CFG::Visuals_Sheen_Local)
						goto should_override;

					// If owner is the local player's active weapon (viewmodel attachment model case)
					if (CFG::Visuals_Sheen_Local && pLocalActiveWeapon && pOwner == pLocalActiveWeapon)
						goto should_override;

					const bool bIsSameTeam = pOwner->m_iTeamNum() == pLocal->m_iTeamNum();
					auto pOwnerPlayer = pOwner->As<C_TFPlayer>();
					const bool bIsFriend = pOwnerPlayer ? pOwnerPlayer->IsPlayerOnSteamFriendsList() : false;
					bool bIsIgnored = false;
					{
						PlayerPriority info{};
						if (F::Players->GetInfo(pOwner->entindex(), info))
							bIsIgnored = info.Ignored;
					}

					bool bShouldOverride = false;

					if ((bIsFriend || bIsIgnored) && CFG::Visuals_Sheen_Friend)
						bShouldOverride = true;
					if (!bIsFriend && !bIsIgnored && bIsSameTeam && CFG::Visuals_Sheen_Teammates)
						bShouldOverride = true;
					if (!bIsFriend && !bIsIgnored && !bIsSameTeam && CFG::Visuals_Sheen_Enemy)
						bShouldOverride = true;

					if (!bShouldOverride)
						return; // Let original handle this entity naturally
				}
			}
		}
	}

should_override:

	if (!ecx->m_pTint)
		return;

	// Force-enable sheen by setting a non-zero sheen index
	if (ecx->m_pSheenIndex)
		ecx->m_pSheenIndex->SetIntValue(CFG::Visuals_Sheen_Index);

	// Override tint color (Vec4 with alpha — matches SDK's m_pTintVar->SetVecValue(color, 4))
	// SDK sheen color data from g_KillStreakEffectsBase (Red team):
	//   Index 1 (Team):  sheen=200,20,15   color1=255,118,118  color2=255,35,28
	//   Index 2 (Orange): sheen=242,172,10  color1=255,237,138  color2=255,213,65
	//   Index 3 (Fire):   sheen=255,75,5    color1=255,111,5    color2=255,137,31
	//   Index 4 (Green):  sheen=100,255,10  color1=230,255,60   color2=193,255,61
	//   Index 5 (Cyan):   sheen=40,255,70   color1=103,255,121 color2=165,255,193
	//   Index 6 (Purple): sheen=105,20,255  color1=105,20,255  color2=185,145,255
	//   Index 7 (Pink):   sheen=255,30,255  color1=255,120,255 color2=255,176,217
	// The sheen alternates between color1 and color2 based on animation frame.
	static const struct { float r, g, b; } sheenColors[] = {
		{ 0.0f, 0.0f, 0.0f },       // 0 = off
		{ 200/255.f, 20/255.f, 15/255.f },   // 1 = Team Red
		{ 242/255.f, 172/255.f, 10/255.f },  // 2 = Orange
		{ 255/255.f, 75/255.f, 5/255.f },    // 3 = Fire
		{ 100/255.f, 255/255.f, 10/255.f },  // 4 = Green
		{ 40/255.f, 255/255.f, 70/255.f },   // 5 = Cyan
		{ 105/255.f, 20/255.f, 255/255.f },  // 6 = Purple
		{ 255/255.f, 30/255.f, 255/255.f },  // 7 = Pink
	};
	static const struct { float r, g, b; } color1Data[] = {
		{ 0.0f, 0.0f, 0.0f },
		{ 255/255.f, 118/255.f, 118/255.f },
		{ 255/255.f, 237/255.f, 138/255.f },
		{ 255/255.f, 111/255.f, 5/255.f },
		{ 230/255.f, 255/255.f, 60/255.f },
		{ 103/255.f, 255/255.f, 121/255.f },
		{ 105/255.f, 20/255.f, 255/255.f },
		{ 255/255.f, 120/255.f, 255/255.f },
	};
	static const struct { float r, g, b; } color2Data[] = {
		{ 0.0f, 0.0f, 0.0f },
		{ 255/255.f, 35/255.f, 28/255.f },
		{ 255/255.f, 213/255.f, 65/255.f },
		{ 255/255.f, 137/255.f, 31/255.f },
		{ 193/255.f, 255/255.f, 61/255.f },
		{ 165/255.f, 255/255.f, 193/255.f },
		{ 185/255.f, 145/255.f, 255/255.f },
		{ 255/255.f, 176/255.f, 217/255.f },
	};

	float tintR, tintG, tintB;
	if (CFG::Visuals_Sheen_Rainbow)
	{
		const Color_t col = ColorUtils::Rainbow(I::GlobalVars->realtime, CFG::Visuals_Sheen_Rainbow_Rate);
		tintR = ColorUtils::ToFloat(col.r);
		tintG = ColorUtils::ToFloat(col.g);
		tintB = ColorUtils::ToFloat(col.b);
	}
	else if (CFG::Visuals_Sheen_Index == 8)
	{
		// Custom — use the ColorPicker value
		const auto& col = CFG::Color_Sheen_Tint;
		tintR = ColorUtils::ToFloat(col.r);
		tintG = ColorUtils::ToFloat(col.g);
		tintB = ColorUtils::ToFloat(col.b);
	}
	else
	{
		// Use the SDK's sheen color for the selected index
		const int idx = std::clamp(CFG::Visuals_Sheen_Index, 1, 7);
		tintR = sheenColors[idx].r;
		tintG = sheenColors[idx].g;
		tintB = sheenColors[idx].b;
	}

	// Apply intensity multiplier
	const float flIntensity = CFG::Visuals_Sheen_Intensity;
	ecx->m_pTint->SetVecValue(tintR * flIntensity, tintG * flIntensity, tintB * flIntensity, 1.0f);

	// If the original called RunNoProxy(), animation was skipped.
	// Replicate the SDK's animation logic:
	//   frame = tf_sheen_framerate * (curtime - startTime) % numFrames
	//   On wrap: pause for Sheen_Interval, then restart
	if (ecx->m_AnimatedTextureVar && ecx->m_AnimatedTextureFrameNum)
	{
		ITexture* pTex = ecx->m_AnimatedTextureVar->GetTextureValue();
		if (pTex)
		{
			const int numFrames = pTex->GetNumAnimationFrames();
			if (numFrames > 0)
			{
				const float flCurTime = I::GlobalVars->curtime;

				// Detect map change: curtime went backwards or jumped significantly
				if (flCurTime < s_flLastCurTime || (s_flLastCurTime > 0.0f && flCurTime - s_flLastCurTime > 60.0f))
					s_flSheenStartTime = -1.0f; // Force re-initialize

				s_flLastCurTime = flCurTime;

				// Initialize start time on first call or after map change
				if (s_flSheenStartTime < 0.0f)
					s_flSheenStartTime = flCurTime;

				const float flFrameRate = 25.0f; // tf_sheen_framerate default
				const float deltaTime = I::GlobalVars->curtime - s_flSheenStartTime;

				// If we're in the interval (pause) period, keep frame at 0
				if (deltaTime < 0.0f)
				{
					ecx->m_AnimatedTextureFrameNum->SetIntValue(0);
				}
				else
				{
					const float frame = flFrameRate * deltaTime;
					const float prevFrame = flFrameRate * std::max(deltaTime - I::GlobalVars->frametime, 0.0f);

					int intFrame = static_cast<int>(frame) % numFrames;
					const int intPrevFrame = static_cast<int>(prevFrame) % numFrames;

					// Wrap detection — pause for Sheen_Interval, then restart
					if (intPrevFrame > intFrame)
					{
						intFrame = 0;
						s_flSheenStartTime = I::GlobalVars->curtime + CFG::Visuals_Sheen_Interval;
					}

					ecx->m_AnimatedTextureFrameNum->SetIntValue(intFrame);
				}
			}
		}
	}

	// Set scale/offset/direction for the sheen mask
	// If the original OnBind already set these (native killstreak weapon), use those values.
	// Otherwise (RunNoProxy or our custom material), use defaults that look correct for most weapons.
	// Direction 1 = sheen scrolls along Y axis (bottom to top on most weapons)
	if (ecx->m_pScaleX)
		ecx->m_pScaleX->SetFloatValue(ecx->m_flScaleX >= 0.0f ? ecx->m_flScaleX : 30.0f);
	if (ecx->m_pScaleY)
		ecx->m_pScaleY->SetFloatValue(ecx->m_flScaleY >= 0.0f ? ecx->m_flScaleY : 30.0f);
	if (ecx->m_pOffsetX)
		ecx->m_pOffsetX->SetFloatValue(ecx->m_flSheenOffsetX);
	if (ecx->m_pOffsetY)
		ecx->m_pOffsetY->SetFloatValue(ecx->m_flSheenOffsetY);
	if (ecx->m_pDirection)
		ecx->m_pDirection->SetIntValue(ecx->m_iSheenDir >= 0 ? ecx->m_iSheenDir : 1);
}
