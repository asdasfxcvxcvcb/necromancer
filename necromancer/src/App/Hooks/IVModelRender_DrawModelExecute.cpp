#include "../../SDK/SDK.h"

#include "../Features/Materials/Materials.h"
#include "../Features/Outlines/Outlines.h"
#include "../Features/FakeAngle/FakeAngle.h"

#include "../Features/CFG.h"
#include "../Features/SpyCamera/SpyCamera.h"
#include "../Features/Players/Players.h"

MAKE_SIGNATURE(CBaseAnimating_DrawModel, "client.dll", "4C 8B DC 49 89 5B ? 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC ? 48 8B 05 ? ? ? ? 48 8D 3D", 0x0);
MAKE_SIGNATURE(ViewmodelAttachment_DrawModel, "client.dll", "41 8B D5 FF 50 ? 8B 97", 0x6);

MAKE_HOOK(IVModelRender_DrawModelExecute, Memory::GetVFunc(I::ModelRender, 19), void, __fastcall,
	IVModelRender* ecx, const DrawModelState_t& state, ModelRenderInfo_t& pInfo, matrix3x4_t* pCustomBoneToWorld)
{
	// Safety check - skip custom logic during level transitions
	if (G::bLevelTransition)
	{
		CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);
		return;
	}

	// Provide the original DrawModelExecute function pointer to Materials
	// so it can call it directly for lag record rendering (Amalgam approach)
	if (!F::Materials->m_fnDrawModelExecute)
		F::Materials->SetDrawModelExecuteFn(Hook.Original<IVModelRender::DrawModelExecuteFn>());

	if (CFG::Visuals_Sheen_Active)
		F::Materials->ValidateSheen();

	if (!F::SpyCamera->IsRendering())
	{
		const auto pClientEntity = I::ClientEntityList->GetClientEntity(pInfo.entity_index);

		if (pClientEntity)
		{
			// Safety: validate entity has a valid client class before calling GetClassId
			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (!pNetworkable)
			{
				CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);
				return;
			}

			auto pClientClass = pNetworkable->GetClientClass();
			if (!pClientClass)
			{
				CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);
				return;
			}

			const int nClassId = pClientClass->m_ClassID;

			if (CFG::Visuals_Disable_Dropped_Weapons && nClassId == static_cast<int>(ETFClassIds::CTFDroppedWeapon))
				return;

			const bool clean_ss = CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot();

			if (!clean_ss && nClassId == static_cast<int>(ETFClassIds::CTFViewModel))
			{
				if (const auto pLocal = H::Entities->GetLocal())
				{
					if (!pLocal->IsUbered() && !pLocal->deadflag())
					{
						// --- Pass 1: Viewmodel material override (if enabled) ---
						if (CFG::Materials_ViewModel_Active)
						{
							auto getMaterial = [&](int nIndex) -> IMaterial* {
								switch (nIndex)
								{
									case 0: return nullptr;
									case 1: return F::Materials->m_pFlat;
									case 2: return F::Materials->m_pShaded;
									case 3: return F::Materials->m_pGlossy;
									case 4: return F::Materials->m_pGlow;
									case 5: return F::Materials->m_pPlastic;
									default: return nullptr;
								}
							};

							const auto mat = getMaterial(CFG::Materials_ViewModel_Hands_Material);

							if (mat)
							{
								I::ModelRender->ForcedMaterialOverride(mat);
							}

							if (CFG::Materials_ViewModel_Hands_Alpha < 1.0f)
							{
								I::RenderView->SetBlend(CFG::Materials_ViewModel_Hands_Alpha);
							}

							if (mat)
							{
								const auto& base = CFG::Color_Hands;

								if (mat != F::Materials->m_pGlow)
								{
									I::RenderView->SetColorModulation(base);
								}

								else
								{
									if (F::Materials->m_pGlowEnvmapTint && F::Materials->m_pGlowSelfillumTint)
									{
										const auto& sheen = CFG::Color_Hands_Sheen;

										I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

										F::Materials->m_pGlowSelfillumTint
										            ->SetVecValue(ColorUtils::ToFloat(base.r), ColorUtils::ToFloat(base.g), ColorUtils::ToFloat(base.b));

										F::Materials->m_pGlowEnvmapTint
										            ->SetVecValue(ColorUtils::ToFloat(sheen.r), ColorUtils::ToFloat(sheen.g), ColorUtils::ToFloat(sheen.b));
									}
								}
							}

							CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);

							I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

							if (CFG::Materials_ViewModel_Hands_Alpha < 1.0f)
							{
								I::RenderView->SetBlend(1.0f);
							}

							if (mat)
							{
								I::ModelRender->ForcedMaterialOverride(nullptr);
							}
						}

						// If pass 1 didn't handle the draw, draw normally
						if (!CFG::Materials_ViewModel_Active)
						{
							CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);
						}

						return;
					}
				}
			}

			if (CFG::Visuals_Disable_Wearables && nClassId == static_cast<int>(ETFClassIds::CTFWearable))
				return;

			if (nClassId == static_cast<int>(ETFClassIds::CDynamicProp))
			{
				if (CFG::Visuals_World_Modulation_Mode == 0)
				{
					if (const auto flNightMode = CFG::Visuals_Night_Mode)
					{
						const auto col = static_cast<byte>(Math::RemapValClamped(flNightMode, 0.0f, 100.0f, 255.0f, 50.0f));

						I::RenderView->SetColorModulation({ col, col, col, static_cast<byte>(255) });
					}
				}

				else
				{
					I::RenderView->SetColorModulation(CFG::Color_Props);
				}

				CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);

				I::RenderView->SetColorModulation({ 255, 255, 255, 255 });

				return;
			}

			if (!I::EngineClient->IsTakingScreenshot())
			{
				const auto pEntity = pClientEntity->As<C_BaseEntity>();

				// Don't skip drawing if using TF2 native glow (it needs entities drawn normally)
				const bool bUsingTF2Glow = CFG::Outlines_Active && CFG::Outlines_Style == 4;
				
				// Skip drawing the real model if:
				// - We're not using TF2 Glow AND
				// - We're not currently rendering Materials/Outlines AND
				// - The entity was already drawn by Materials or Outlines
				if (!bUsingTF2Glow && !F::Materials->IsRendering() && !F::Outlines->IsRendering() && (F::Outlines->HasDrawn(pEntity) || F::Materials->HasDrawn(pEntity)))
					return;
			}
		}
	}

	if (CFG::Visuals_Simple_Models)
		*const_cast<int*>(&state.m_lod) = 5;

	// Provide the original DrawModelExecute function pointer to Outlines
	if (!F::Outlines->m_fnDrawModelExecute && F::Materials->m_fnDrawModelExecute)
		F::Outlines->SetDrawModelExecuteFn(F::Materials->m_fnDrawModelExecute);

	{
		if (F::Materials->IsRendering() && pInfo.entity_index > 0)
		{
			const auto pRenderedEntity = I::ClientEntityList->GetClientEntity(pInfo.entity_index);
			if (pRenderedEntity)
			{
				auto pNet = pRenderedEntity->GetClientNetworkable();
				if (pNet && pNet->GetClientClass() && pNet->GetClientClass()->m_ClassID == static_cast<int>(ETFClassIds::CTFPlayer))
				{
					CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);

					const auto pPlayer = pRenderedEntity->As<C_TFPlayer>();
					if (pPlayer && F::Materials->ShouldRenderLagRecords(pPlayer))
					{
						F::Materials->RenderLagRecords(pPlayer, state, pInfo);
					}
					return;
				}
			}
		}
	}

	// When Outlines is rendering an enemy player, capture the state for the glow pass
	// lag record rendering (Outlines renders in two phases: model draw then glow pass)
	if (F::Outlines->IsRendering() && pInfo.entity_index > 0)
	{
		const auto pRenderedEntity = I::ClientEntityList->GetClientEntity(pInfo.entity_index);
		if (pRenderedEntity)
		{
			auto pNet = pRenderedEntity->GetClientNetworkable();
			if (pNet && pNet->GetClientClass() && pNet->GetClientClass()->m_ClassID == static_cast<int>(ETFClassIds::CTFPlayer))
			{
				const auto pPlayer = pRenderedEntity->As<C_TFPlayer>();
				if (pPlayer && F::Outlines->ShouldRenderLagRecords(pPlayer))
				{
					F::Outlines->CaptureRenderState(state, pInfo);
				}
			}
		}
	}

	// Sheen overlay for Friend/Teammates/Enemy — applied to their weapon entities
	if (CFG::Visuals_Sheen_Active
		&& (CFG::Visuals_Sheen_Friend || CFG::Visuals_Sheen_Teammates || CFG::Visuals_Sheen_Enemy)
		&& !(CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		&& !F::Materials->IsRendering()
		&& !F::Outlines->IsRendering()
		&& F::Materials->m_pSheen
		&& pInfo.entity_index > 0
		&& pInfo.entity_index <= I::ClientEntityList->GetMaxEntities())
	{
		const auto pClientEntity = I::ClientEntityList->GetClientEntity(pInfo.entity_index);
		if (pClientEntity)
		{
			auto pNetworkable = pClientEntity->GetClientNetworkable();
			if (pNetworkable)
			{
				auto pClientClass = pNetworkable->GetClientClass();
				if (pClientClass)
				{
					const int nClassId = pClientClass->m_ClassID;
					// Only apply to weapon/wearable entities
					if (nClassId == static_cast<int>(ETFClassIds::CTFWeaponBase)
						|| nClassId == static_cast<int>(ETFClassIds::CTFWearable))
					{
						auto pWeapon = pClientEntity->As<C_BaseEntity>();
						auto pOwner = pWeapon->m_hOwnerEntity().Get();
						if (pOwner)
						{
							auto pLocal = H::Entities->GetLocal();
							if (pLocal && pOwner != pLocal)
							{
								auto pOwnerPlayer = pOwner->As<C_TFPlayer>();
								const bool bIsFriend = pOwnerPlayer ? pOwnerPlayer->IsPlayerOnSteamFriendsList() : false;
								bool bIsIgnored = false;
								{
									PlayerPriority info{};
									if (F::Players->GetInfo(pOwner->entindex(), info))
										bIsIgnored = info.Ignored;
								}

								const bool bIsSameTeam = pOwner->m_iTeamNum() == pLocal->m_iTeamNum();
								bool bShouldApply = false;

								if ((bIsFriend || bIsIgnored) && CFG::Visuals_Sheen_Friend)
									bShouldApply = true;
								if (!bIsFriend && !bIsIgnored && bIsSameTeam && CFG::Visuals_Sheen_Teammates)
									bShouldApply = true;
								if (!bIsFriend && !bIsIgnored && !bIsSameTeam && CFG::Visuals_Sheen_Enemy)
									bShouldApply = true;

								if (bShouldApply)
								{
									// Draw weapon normally first
									CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);

									// Then draw additive sheen overlay
									if (F::Materials->m_pSheenIndex)
										F::Materials->m_pSheenIndex->SetIntValue(CFG::Visuals_Sheen_Index);

									I::ModelRender->ForcedMaterialOverride(F::Materials->m_pSheen);
									I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);
									CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);
									I::ModelRender->ForcedMaterialOverride(nullptr);
									return;
								}
							}
						}
					}
				}
			}
		}
	}

	CALL_ORIGINAL(ecx, state, pInfo, pCustomBoneToWorld);
}

MAKE_HOOK(CBaseAnimating_DrawModel, Signatures::CBaseAnimating_DrawModel.Get(), int, __fastcall,
	void *ecx, int flags)
{
	const bool clean_ss = CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot();

	if (CFG::Visuals_Sheen_Active)
		F::Materials->ValidateSheen();

	if (CFG::Materials_ViewModel_Active
		&& !clean_ss
		&& (flags & STUDIO_RENDER)
		&& reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == Signatures::ViewmodelAttachment_DrawModel.Get())
	{
		if (const auto pLocal = H::Entities->GetLocal())
		{
			if (!pLocal->IsUbered() && !pLocal->deadflag())
			{
				// --- Weapon material override ---
				auto getMaterial = [&](int nIndex) -> IMaterial* {
					switch (nIndex)
					{
						case 0: return nullptr;
						case 1: return F::Materials->m_pFlat;
						case 2: return F::Materials->m_pShaded;
						case 3: return F::Materials->m_pGlossy;
						case 4: return F::Materials->m_pGlow;
						case 5: return F::Materials->m_pPlastic;
						default: return nullptr;
					}
				};

				const auto mat = getMaterial(CFG::Materials_ViewModel_Weapon_Material);

				if (mat)
				{
					I::ModelRender->ForcedMaterialOverride(mat);
				}

				if (CFG::Materials_ViewModel_Weapon_Alpha < 1.0f)
				{
					I::RenderView->SetBlend(CFG::Materials_ViewModel_Weapon_Alpha);
				}

				if (mat)
				{
					const auto& base = CFG::Color_Weapon;

					if (mat != F::Materials->m_pGlow)
					{
						I::RenderView->SetColorModulation(base);
					}

					else
					{
						if (F::Materials->m_pGlowEnvmapTint && F::Materials->m_pGlowSelfillumTint)
						{
							const auto& sheen = CFG::Color_Weapon_Sheen;

							I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

							F::Materials->m_pGlowSelfillumTint
							            ->SetVecValue(ColorUtils::ToFloat(base.r), ColorUtils::ToFloat(base.g), ColorUtils::ToFloat(base.b));

							F::Materials->m_pGlowEnvmapTint
							            ->SetVecValue(ColorUtils::ToFloat(sheen.r), ColorUtils::ToFloat(sheen.g), ColorUtils::ToFloat(sheen.b));
						}
					}
				}

				const auto result = CALL_ORIGINAL(ecx, flags);

				I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

				if (CFG::Materials_ViewModel_Weapon_Alpha < 1.0f)
				{
					I::RenderView->SetBlend(1.0f);
				}

				if (mat)
				{
					I::ModelRender->ForcedMaterialOverride(nullptr);
				}

				// --- Killstreak sheen additive overlay (local player weapon only) ---
				if (CFG::Visuals_Sheen_Active && CFG::Visuals_Sheen_Local && F::Materials->m_pSheen)
				{
					// OnBind hook handles tint, intensity, and animation.
					// Just set the sheen index and draw the overlay pass.
					if (F::Materials->m_pSheenIndex)
						F::Materials->m_pSheenIndex->SetIntValue(CFG::Visuals_Sheen_Index);

					I::ModelRender->ForcedMaterialOverride(F::Materials->m_pSheen);
					I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);
					CALL_ORIGINAL(ecx, flags);
					I::ModelRender->ForcedMaterialOverride(nullptr);
				}

				return result;
			}
		}
	}

	// Sheen overlay when viewmodel material override is OFF (local player weapon only)
	if (CFG::Visuals_Sheen_Active
		&& CFG::Visuals_Sheen_Local
		&& !CFG::Materials_ViewModel_Active
		&& !clean_ss
		&& (flags & STUDIO_RENDER)
		&& reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == Signatures::ViewmodelAttachment_DrawModel.Get())
	{
		if (const auto pLocal = H::Entities->GetLocal())
		{
			if (!pLocal->IsUbered() && !pLocal->deadflag() && F::Materials->m_pSheen)
			{
				// Draw weapon normally first
				CALL_ORIGINAL(ecx, flags);

				// Then draw additive sheen overlay
				if (F::Materials->m_pSheenIndex)
					F::Materials->m_pSheenIndex->SetIntValue(CFG::Visuals_Sheen_Index);

				I::ModelRender->ForcedMaterialOverride(F::Materials->m_pSheen);
				I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);
				CALL_ORIGINAL(ecx, flags);
				I::ModelRender->ForcedMaterialOverride(nullptr);
				return 1;
			}
		}
	}

	return CALL_ORIGINAL(ecx, flags);
}
