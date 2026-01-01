#include "ESP.h"

#include "../CFG.h"
#include "../SpyCamera/SpyCamera.h"
#include "../VisualUtils/VisualUtils.h"
#include "../Players/Players.h"

constexpr int SPACING_X = 2;
constexpr int SPACING_Y = 2;
constexpr Color_t WHITE = {220, 220, 220, 255};

// TODO: These utility functions should be moved somewhere else

const char* GetBuildingName(C_BaseObject* pBuilding)
{
	switch (pBuilding->GetClassId())
	{
	case ETFClassIds::CObjectSentrygun: return pBuilding->m_bMiniBuilding() ? "Mini Sentrygun" : "Sentrygun";
	case ETFClassIds::CObjectDispenser: return "Dispenser";
	case ETFClassIds::CObjectTeleporter: return pBuilding->m_iObjectMode() == MODE_TELEPORTER_ENTRANCE ? "Teleporter In" : "Teleporter Out";
	default: return "Unknown Building Name";
	}
}

const char* GetProjectileName(C_BaseEntity* pEntity)
{
	switch (pEntity->GetClassId())
	{
	case ETFClassIds::CTFProjectile_Rocket:
	case ETFClassIds::CTFProjectile_SentryRocket: return "Rocket";
	case ETFClassIds::CTFProjectile_Jar: return "Jarate";
	case ETFClassIds::CTFProjectile_JarGas: return "Gas";
	case ETFClassIds::CTFProjectile_JarMilk: return "Milk";
	case ETFClassIds::CTFProjectile_Arrow: return "Arrow";
	case ETFClassIds::CTFProjectile_Flare: return "Flare";
	case ETFClassIds::CTFProjectile_Cleaver: return "Cleaver";
	case ETFClassIds::CTFProjectile_HealingBolt: return "Healing Arrow";
	case ETFClassIds::CTFGrenadePipebombProjectile:
		{
			const auto pPipebomb = pEntity->As<C_TFGrenadePipebombProjectile>();

			return pPipebomb->HasStickyEffects() ? "Sticky" : pPipebomb->m_iType() == TF_GL_MODE_CANNONBALL ? "Cannonball" : "Pipe";
		}

	default: return "Projectile";
	}
}

const char* GetPlayerClassName(C_TFPlayer* pPlayer)
{
	switch (pPlayer->m_iClass())
	{
	case TF_CLASS_SCOUT: return "Scout";
	case TF_CLASS_SOLDIER: return "Soldier";
	case TF_CLASS_PYRO: return "Pyro";
	case TF_CLASS_DEMOMAN: return "Demoman";
	case TF_CLASS_HEAVYWEAPONS: return "Heavy";
	case TF_CLASS_ENGINEER: return "Engineer";
	case TF_CLASS_MEDIC: return "Medic";
	case TF_CLASS_SNIPER: return "Sniper";
	case TF_CLASS_SPY: return "Spy";
	default: return "Unknown Class";
	}
}

bool CESP::GetDrawBounds(C_BaseEntity* pEntity, int& x, int& y, int& w, int& h)
{
	if (!pEntity)
		return false;

	bool bIsPlayer = false;
	Vec3 vMins = {}, vMaxs = {};
	auto& transform = const_cast<matrix3x4_t&>(pEntity->RenderableToWorldTransform()); // TODO: This is bad

	switch (pEntity->GetClassId())
	{
	case ETFClassIds::CTFPlayer:
		{
			const auto pPlayer = pEntity->As<C_TFPlayer>();

			bIsPlayer = true;
			vMins = pPlayer->m_vecMins();
			vMaxs = pPlayer->m_vecMaxs();

			if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
			{
				Vec3 vAngles = I::EngineClient->GetViewAngles();
				vAngles.x = vAngles.z = 0.0f;
				Math::AngleMatrix(vAngles, transform);
				Math::MatrixSetColumn(pPlayer->GetRenderOrigin(), 3, transform);
			}

			break;
		}

	case ETFClassIds::CObjectSentrygun:
	case ETFClassIds::CObjectDispenser:
	case ETFClassIds::CObjectTeleporter:
		{
			vMins = pEntity->m_vecMins();
			vMaxs = pEntity->m_vecMaxs();
			break;
		}

	default:
		{
			pEntity->GetRenderBounds(vMins, vMaxs);
			break;
		}
	}

	const Vec3 vPoints[] =
	{
		Vec3(vMins.x, vMins.y, vMins.z),
		Vec3(vMins.x, vMaxs.y, vMins.z),
		Vec3(vMaxs.x, vMaxs.y, vMins.z),
		Vec3(vMaxs.x, vMins.y, vMins.z),
		Vec3(vMaxs.x, vMaxs.y, vMaxs.z),
		Vec3(vMins.x, vMaxs.y, vMaxs.z),
		Vec3(vMins.x, vMins.y, vMaxs.z),
		Vec3(vMaxs.x, vMins.y, vMaxs.z)
	};

	Vec3 vTransformed[8] = {};

	for (int n = 0; n < 8; n++)
	{
		Math::VectorTransform(vPoints[n], transform, vTransformed[n]);
	}

	Vec3 flb = {}, brt = {}, blb = {}, frt = {}, frb = {}, brb = {}, blt = {}, flt = {};

	if (H::Draw->W2S(vTransformed[3], flb) && H::Draw->W2S(vTransformed[5], brt)
		&& H::Draw->W2S(vTransformed[0], blb) && H::Draw->W2S(vTransformed[4], frt)
		&& H::Draw->W2S(vTransformed[2], frb) && H::Draw->W2S(vTransformed[1], brb)
		&& H::Draw->W2S(vTransformed[6], blt) && H::Draw->W2S(vTransformed[7], flt)
		&& H::Draw->W2S(vTransformed[6], blt) && H::Draw->W2S(vTransformed[7], flt))
	{
		const Vec3 arr[] = {flb, brt, blb, frt, frb, brb, blt, flt};

		float left = flb.x;
		float top = flb.y;
		float righ = flb.x;
		float bottom = flb.y;

		for (int n = 1; n < 8; n++)
		{
			if (left > arr[n].x)
				left = arr[n].x;

			if (top < arr[n].y)
				top = arr[n].y;

			if (righ < arr[n].x)
				righ = arr[n].x;

			if (bottom > arr[n].y)
				bottom = arr[n].y;
		}

		float x_ = left;
		float y_ = bottom;
		float w_ = (righ - left);
		float h_ = (top - bottom);

		if (bIsPlayer)
		{
			x_ += ((righ - left) / 8.0f);
			w_ -= (((righ - left) / 8.0f) * 2.0f);
		}

		x = static_cast<int>(x_);
		y = static_cast<int>(y_);
		w = static_cast<int>(w_);
		h = static_cast<int>(h_);

		return x <= H::Draw->GetScreenW() && (x + w) >= 0 && y <= H::Draw->GetScreenH() && (y + h) >= 0;
	}

	return false;
}

void CESP::DrawBones(C_TFPlayer* pPlayer, Color_t color)
{
	auto MatrixPosition = [](const matrix3x4_t& matrix, Vector& position)
	{
		position[0] = matrix[0][3];
		position[1] = matrix[1][3];
		position[2] = matrix[2][3];
	};

	const model_t* pModel = pPlayer->GetModel();

	if (!pModel)
		return;

	const studiohdr_t* pStudioHdr = I::ModelInfoClient->GetStudiomodel(pModel);

	if (!pStudioHdr)
		return;

	matrix3x4_t boneMatrix[MAXSTUDIOBONES];

	if (!pPlayer->SetupBones(boneMatrix, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		return;

	Vec3 p1 = {}, p2 = {};
	Vec3 p1s = {}, p2s = {};

	for (int n = 0; n < pStudioHdr->numbones; n++)
	{
		const mstudiobone_t* pBone = pStudioHdr->pBone(n);

		if (!pBone || pBone->parent == -1 || !(pBone->flags & BONE_USED_BY_HITBOX))
			continue;

		MatrixPosition(boneMatrix[n], p1);

		if (!H::Draw->W2S(p1, p1s))
			continue;

		MatrixPosition(boneMatrix[pBone->parent], p2);

		if (!H::Draw->W2S(p2, p2s))
			continue;

		H::Draw->Line(static_cast<int>(p1s.x), static_cast<int>(p1s.y), static_cast<int>(p2s.x), static_cast<int>(p2s.y), color);
	}
}

void CESP::Run()
{
	if (!CFG::ESP_Active || I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch() || F::SpyCamera->IsRendering())
		return;

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}
	
	// Early exit if nothing is enabled - massive performance gain
	if (!CFG::ESP_Players_Active && !CFG::ESP_Buildings_Active && !CFG::ESP_World_Active)
		return;

	auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	float flOriginalAlpha = I::MatSystemSurface->DrawGetAlphaMultiplier();

	if (CFG::ESP_Players_Active)
	{
		I::MatSystemSurface->DrawSetAlphaMultiplier(CFG::ESP_Players_Alpha);
		
		// Cache expensive checks
		const int nLocalTeam = pLocal->m_iTeamNum();
		const bool bThirdPerson = I::Input->CAM_IsThirdPerson();

		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
		{
			if (!pEntity)
				continue;

			auto pPlayer = pEntity->As<C_TFPlayer>();

			if (pPlayer->deadflag())
				continue;

			const bool bIsLocal = pPlayer == pLocal;

			if ((CFG::ESP_Players_Ignore_Local && bIsLocal) || (!bThirdPerson && bIsLocal))
				continue;

			if (!bIsLocal)
			{
				// Early invisibility check before expensive operations
				if (CFG::ESP_Players_Ignore_Invisible && pPlayer->m_flInvisibility() >= 1.0f)
					continue;
				
				const bool bIsFriend = pPlayer->IsPlayerOnSteamFriendsList();

				if (CFG::ESP_Players_Ignore_Friends && bIsFriend)
					continue;

				// Check if player is tagged (Cheater/RetardLegit/Ignored)
				bool bIsTagged = false;
				if (!CFG::ESP_Players_Ignore_Tagged)
				{
					PlayerPriority playerPriority = {};
					if (F::Players->GetInfo(pPlayer->entindex(), playerPriority))
					{
						bIsTagged = playerPriority.Cheater || playerPriority.RetardLegit || playerPriority.Ignored;
					}
				}

				// Skip team/enemy filtering if player is tagged (and not ignoring tagged)
				if (!bIsFriend && !bIsTagged)
				{
					const int nPlayerTeam = pPlayer->m_iTeamNum();
					
					if (CFG::ESP_Players_Ignore_Teammates && nPlayerTeam == nLocalTeam)
					{
						if (!CFG::ESP_Players_Show_Teammate_Medics || pPlayer->m_iClass() != TF_CLASS_MEDIC)
							continue;
					}

					if (CFG::ESP_Players_Ignore_Enemies && nPlayerTeam != nLocalTeam)
						continue;
				}
			}

			int x = 0, y = 0, w = 0, h = 0;

			if (!GetDrawBounds(pPlayer, x, y, w, h))
			{
				if (CFG::ESP_Players_Arrows && pPlayer != pLocal && !pLocal->deadflag())
				{
					int nScreenCenterX = H::Draw->GetScreenW() / 2;
					int nScreenCenterY = H::Draw->GetScreenH() / 2;

					Vec3 vScreen = {}, vPlayerWSC = pPlayer->GetCenter();
					H::Draw->ScreenPosition(vPlayerWSC, vScreen);

					if (pLocal->GetCenter().DistTo(vPlayerWSC) > CFG::ESP_Players_Arrows_Max_Distance)
						continue;

					Vec3 vAngle = {};
					Math::VectorAngles({nScreenCenterX - vScreen.x, nScreenCenterY - vScreen.y, 0.0f}, vAngle);

					float flYaw = DEG2RAD(vAngle.y);
					float flRadius = CFG::ESP_Players_Arrows_Radius;
					float flDrawX = nScreenCenterX - flRadius * cosf(flYaw);
					float flDrawY = nScreenCenterY - flRadius * sinf(flYaw);

					std::array<Vec2, 3> vPoints = {
						Vec2(flDrawX + 10.0f, flDrawY + 10.0f),
						Vec2(flDrawX - 8.0f, flDrawY),
						Vec2(flDrawX + 10.0f, flDrawY - 10.0f)
					};

					Math::RotateTriangle(vPoints, vAngle.y);

					H::Draw->FilledTriangle(vPoints, F::VisualUtils->GetEntityColor(pLocal, pPlayer));
				}

				continue;
			}

			auto entColor = F::VisualUtils->GetEntityColor(pLocal, pPlayer);
			auto healthColor = F::VisualUtils->GetHealthColor(pPlayer->m_iHealth(), pPlayer->GetMaxHealth());
			auto textColor = CFG::ESP_Text_Color == 0 ? entColor : CFG::Color_ESP_Text;

			int nTextOffsetY = 0;

			if (CFG::ESP_Players_Tracer)
			{
				if (!bIsLocal)
				{
					auto nFromY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_From)
						{
						case 0: return 0;
						case 1: return H::Draw->GetScreenH() / 2;
						case 2: return H::Draw->GetScreenH();
						default: return 0;
						}
					};

					auto nToY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_To)
						{
						case 0: return y;
						case 1: return y + (h / 2);
						case 2: return y + h;
						default: return 0;
						}
					};

					H::Draw->Line(H::Draw->GetScreenW() / 2, nFromY(), x + (w / 2), nToY(), entColor);
				}
			}

			if (CFG::ESP_Players_Bones)
			{
				DrawBones(pPlayer, CFG::ESP_Players_Bones_Color == 0 ? entColor : WHITE);
			}

			if (CFG::ESP_Players_Name)
			{
				player_info_t PlayerInfo = {};

				if (I::EngineClient->GetPlayerInfo(pPlayer->entindex(), &PlayerInfo))
				{
					// Check for player tags and use appropriate color
					Color_t nameColor = textColor;
					PlayerPriority playerPriority = {};
					if (F::Players->GetInfo(pPlayer->entindex(), playerPriority))
					{
						if (playerPriority.Cheater)
							nameColor = CFG::Color_Cheater;
						else if (playerPriority.RetardLegit)
							nameColor = CFG::Color_RetardLegit;
						else if (playerPriority.Ignored)
							nameColor = CFG::Color_Friend;
					}

					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						(y - (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall - 1)) - SPACING_Y,
						nameColor,
						POS_CENTERX,
						Utils::ConvertUtf8ToWide(PlayerInfo.name).c_str()
					);
				}
			}

			// Draw tags (Friend, Cheater, Retard Legit, Ignored) above the player name
			if (CFG::ESP_Players_Tags)
			{
				int tagOffset = 0;
				int fontTall = H::Fonts->Get(EFonts::ESP_SMALL).m_nTall;
				int baseY = CFG::ESP_Players_Name
					? (y - fontTall - SPACING_Y) - fontTall - SPACING_Y
					: (y - fontTall - SPACING_Y);

				PlayerPriority playerPriority = {};
				F::Players->GetInfo(pPlayer->entindex(), playerPriority);

				// Draw Friend tag
				if (pPlayer->IsPlayerOnSteamFriendsList())
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						baseY - tagOffset,
						CFG::Color_Friend,
						POS_CENTERX,
						"Friend"
					);
					tagOffset += fontTall + SPACING_Y;
				}

				// Draw Cheater tag
				if (playerPriority.Cheater)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						baseY - tagOffset,
						CFG::Color_Cheater,
						POS_CENTERX,
						"Cheater"
					);
					tagOffset += fontTall + SPACING_Y;
				}

				// Draw Retard Legit tag
				if (playerPriority.RetardLegit)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						baseY - tagOffset,
						CFG::Color_RetardLegit,
						POS_CENTERX,
						"Retard Legit"
					);
					tagOffset += fontTall + SPACING_Y;
				}

				// Draw Ignored tag
				if (playerPriority.Ignored)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						baseY - tagOffset,
						CFG::Color_Friend,
						POS_CENTERX,
						"Ignored"
					);
					tagOffset += fontTall + SPACING_Y;
				}
			}

			if (CFG::ESP_Players_Class)
			{


				H::Draw->String(
					H::Fonts->Get(EFonts::ESP_SMALL),
					x + w + SPACING_X,
					y + (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall * nTextOffsetY++),
					textColor,
					POS_DEFAULT,
					"%hs",
					GetPlayerClassName(pPlayer)
				);
			}

			if (CFG::ESP_Players_Class_Icon)
			{
				static constexpr int CLASS_ICON_SIZE = 18;

				H::Draw->Texture(
					x + (w / 2),
					CFG::ESP_Players_Name ? ((y - (H::Fonts->Get(EFonts::ESP).m_nTall - 1)) - SPACING_Y) - CLASS_ICON_SIZE : y - (CLASS_ICON_SIZE + SPACING_Y),
					CLASS_ICON_SIZE,
					CLASS_ICON_SIZE,
					F::VisualUtils->GetClassIcon(pPlayer->m_iClass()),
					POS_CENTERX
				);
			}

			if (CFG::ESP_Players_Health && !pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
			{
				H::Draw->String(
					H::Fonts->Get(EFonts::ESP_SMALL),
					x + w + SPACING_X,
					y + (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall * nTextOffsetY++),
					healthColor,
					POS_DEFAULT,
					"%d",
					pPlayer->m_iHealth()
				);
			}

			if (CFG::ESP_Players_HealthBar && !pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
			{
				auto flHealth = static_cast<float>(pPlayer->m_iHealth());
				auto flMaxHealth = static_cast<float>(pPlayer->GetMaxHealth());

				if (flHealth > 0.f && flMaxHealth > 0.f)
				{
					if (flHealth > flMaxHealth)
						flMaxHealth = flHealth;

					static constexpr int BAR_WIDTH = 2;
					int nBarX = x - ((BAR_WIDTH * 2) + 1);
					int nFillH = static_cast<int>(Math::RemapValClamped(flHealth, 0.0f, flMaxHealth, 0.0f, static_cast<float>(h)));

					H::Draw->OutlinedRect(nBarX - 1, (y + h - nFillH) - 1, BAR_WIDTH + 2, nFillH + 2, CFG::Color_ESP_Outline);
					H::Draw->Rect(nBarX, y + h - nFillH, BAR_WIDTH, nFillH, healthColor);
				}
			}

			if (CFG::ESP_Players_Uber)
			{
				if (pPlayer->m_iClass() == TF_CLASS_MEDIC)
				{
					if (auto pWeapon = pPlayer->GetWeaponFromSlot(1))
					{
						H::Draw->String(
							H::Fonts->Get(EFonts::ESP_SMALL),
							x + w + SPACING_X,
							y + (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall * nTextOffsetY++),
							CFG::Color_Uber,
							POS_DEFAULT,
							"%d%%", static_cast<int>(pWeapon->As<C_WeaponMedigun>()->m_flChargeLevel() * 100.0f)
						);
					}
				}
			}

			if (CFG::ESP_Players_UberBar)
			{
				if (pPlayer->m_iClass() == TF_CLASS_MEDIC)
				{
					if (auto pWeapon = pPlayer->GetWeaponFromSlot(1))
					{
						auto pMedigun = pWeapon->As<C_WeaponMedigun>();

						if (auto flCharge = pMedigun->m_flChargeLevel())
						{
							int nBarH = 2;
							int nDrawY = y + h + nBarH + 1;
							float flFillW = Math::RemapValClamped(flCharge, 0.0f, 1.0f, 0.0f, static_cast<float>(w));

							H::Draw->OutlinedRect(x - 1, nDrawY - 1, static_cast<int>(flFillW) + 2, nBarH + 2, CFG::Color_ESP_Outline);
							H::Draw->Rect(x, nDrawY, static_cast<int>(flFillW), nBarH, CFG::Color_Uber);

							if (pMedigun->m_iItemDefinitionIndex() == Medic_s_TheVaccinator)
							{
								if (flCharge >= 0.25f)
									H::Draw->Rect(x + static_cast<int>(static_cast<float>(w) * 0.25f) - 1, nDrawY, 2, nBarH, CFG::Color_ESP_Outline);

								if (flCharge >= 0.5f)
									H::Draw->Rect(x + static_cast<int>(static_cast<float>(w) * 0.5f) - 1, nDrawY, 2, nBarH, CFG::Color_ESP_Outline);

								if (flCharge >= 0.75f)
									H::Draw->Rect(x + static_cast<int>(static_cast<float>(w) * 0.75f) - 1, nDrawY, 2, nBarH, CFG::Color_ESP_Outline);
							}
						}
					}
				}
			}

			if (CFG::ESP_Players_Box)
			{
				H::Draw->OutlinedRect(x, y, w, h, entColor);
				H::Draw->OutlinedRect(x - 1, y - 1, w + 2, h + 2, CFG::Color_ESP_Outline);
			}

			if (CFG::ESP_Players_Conds)
			{
				if (nTextOffsetY > 0)
					nTextOffsetY += 1;

				int drawX = x + w + SPACING_X;
				int tall = H::Fonts->Get(EFonts::ESP_CONDS).m_nTall;

				Color_t color = CFG::Color_Conds;

				if (pPlayer->IsZoomed())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "ZOOM");

				if (pPlayer->IsInvisible())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "INVIS");

				if (pPlayer->m_bFeignDeathReady())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "DEADRINGER");

				if (pPlayer->IsInvulnerable())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "INVULN");

				if (pPlayer->IsCritBoosted())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "CRIT");

				if (pPlayer->IsMiniCritBoosted())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "MINICRIT");

				if (pPlayer->IsMarked())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "MARKED");

				if (pPlayer->InCond(TF_COND_MAD_MILK))
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "MILK");

				if (pPlayer->InCond(TF_COND_TAUNTING) || (pPlayer == pLocal && G::bStartedFakeTaunt))
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "TAUNT");

				if (pPlayer->InCond(TF_COND_DISGUISED))
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "DISGUISE");

				if (pPlayer->InCond(TF_COND_BURNING) || pPlayer->InCond(TF_COND_BURNING_PYRO))
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "BURNING");

				if (pPlayer->InCond(TF_COND_OFFENSEBUFF))
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "BANNER");

				if (pPlayer->InCond(TF_COND_DEFENSEBUFF))
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "BACKUP");

				if (pPlayer->InCond(TF_COND_REGENONDAMAGEBUFF))
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "CONCH");

				if (!pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST))
				{
					if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST))
						H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "BULLET(RES)");
				}
				else
				{
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "BULLET(UBER)");
				}

				if (!pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
				{
					if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST))
						H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "EXPLOSION(RES)");
				}
				else
				{
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "EXPLOSION(UBER)");
				}

				if (!pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST))
				{
					if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST))
						H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "FIRE(RES)");
				}
				else
				{
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "FIRE(UBER)");
				}
			}

			// F2P and Party tags (positioned to the left of the health bar)
			if (CFG::ESP_Players_Show_F2P || CFG::ESP_Players_Show_Party)
			{
				int nPlayerIndex = pPlayer->entindex();
				// Health bar is at x - ((BAR_WIDTH * 2) + 1) - 1 = x - 6, so position tags further left
				static constexpr int BAR_WIDTH = 2;
				int drawX = x - ((BAR_WIDTH * 2) + 1) - 1 - SPACING_X;
				int tall = H::Fonts->Get(EFonts::ESP_CONDS).m_nTall;
				int nTagOffsetY = 0;

				// Show F2P tag
				if (CFG::ESP_Players_Show_F2P && H::Entities->IsF2P(nPlayerIndex))
				{
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTagOffsetY++), CFG::Color_F2P, POS_LEFT, "F2P");
				}

				// Show Party tag with party-specific color
				if (CFG::ESP_Players_Show_Party)
				{
					int nPartyIndex = H::Entities->GetPartyIndex(nPlayerIndex);
					if (nPartyIndex > 0 && nPartyIndex <= 12)
					{
						// Get party color based on index
						Color_t partyColor;
						switch (nPartyIndex)
						{
						case 1: partyColor = CFG::Color_Party_1; break;
						case 2: partyColor = CFG::Color_Party_2; break;
						case 3: partyColor = CFG::Color_Party_3; break;
						case 4: partyColor = CFG::Color_Party_4; break;
						case 5: partyColor = CFG::Color_Party_5; break;
						case 6: partyColor = CFG::Color_Party_6; break;
						case 7: partyColor = CFG::Color_Party_7; break;
						case 8: partyColor = CFG::Color_Party_8; break;
						case 9: partyColor = CFG::Color_Party_9; break;
						case 10: partyColor = CFG::Color_Party_10; break;
						case 11: partyColor = CFG::Color_Party_11; break;
						case 12: partyColor = CFG::Color_Party_12; break;
						default: partyColor = CFG::Color_Party_1; break;
						}
						H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTagOffsetY++), partyColor, POS_LEFT, "P%d", nPartyIndex);
					}
				}
			}
		}
	}

	if (CFG::ESP_Buildings_Active)
	{
		I::MatSystemSurface->DrawSetAlphaMultiplier(CFG::ESP_Buildings_Alpha);

		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ALL))
		{
			if (!pEntity)
				continue;

			auto pBuilding = pEntity->As<C_BaseObject>();

			if (pBuilding->m_bPlacing())
				continue;

			bool bIsLocal = F::VisualUtils->IsEntityOwnedBy(pBuilding, pLocal);

			if (CFG::ESP_Buildings_Ignore_Local && bIsLocal)
				continue;

			if (!bIsLocal)
			{
				if (CFG::ESP_Buildings_Ignore_Teammates && pBuilding->m_iTeamNum() == pLocal->m_iTeamNum())
				{
					if (CFG::ESP_Buildings_Show_Teammate_Dispensers)
					{
						if (pBuilding->GetClassId() != ETFClassIds::CObjectDispenser)
							continue;
					}
					else
					{
						continue;
					}
				}

				if (CFG::ESP_Buildings_Ignore_Enemies && pBuilding->m_iTeamNum() != pLocal->m_iTeamNum())
					continue;
			}

			int x = 0, y = 0, w = 0, h = 0;

			if (!GetDrawBounds(pBuilding, x, y, w, h))
				continue;

			auto entColor = F::VisualUtils->GetEntityColor(pLocal, pBuilding);
			auto healthColor = F::VisualUtils->GetHealthColor(pBuilding->m_iHealth(), pBuilding->m_iMaxHealth());
			auto textColor = CFG::ESP_Text_Color == 0 ? entColor : CFG::Color_ESP_Text;

			int nTextOffsetY = 0;

			if (CFG::ESP_Buildings_Tracer)
			{
				auto nFromY = [&]() -> int
				{
					switch (CFG::ESP_Tracer_From)
					{
					case 0: return 0;
					case 1: return H::Draw->GetScreenH() / 2;
					case 2: return H::Draw->GetScreenH();
					default: return 0;
					}
				};

				auto nToY = [&]() -> int
				{
					switch (CFG::ESP_Tracer_To)
					{
					case 0: return y;
					case 1: return y + (h / 2);
					case 2: return y + h;
					default: return 0;
					}
				};

				H::Draw->Line(H::Draw->GetScreenW() / 2, nFromY(), x + (w / 2), nToY(), entColor);
			}

			if (CFG::ESP_Buildings_Name)
			{
				H::Draw->String(
					H::Fonts->Get(EFonts::ESP_SMALL),
					x + (w / 2),
					(y - (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall - 1)) - SPACING_Y,
					textColor,
					POS_CENTERX,
					GetBuildingName(pBuilding)
				);
			}

			if (CFG::ESP_Buildings_Health)
			{
				H::Draw->String(
					H::Fonts->Get(EFonts::ESP_SMALL),
					x + w + SPACING_X,
					y + (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall * nTextOffsetY++),
					healthColor,
					POS_DEFAULT,
					"%d",
					pBuilding->m_iHealth()
				);
			}

			if (CFG::ESP_Buildings_HealthBar)
			{
				auto flHealth = static_cast<float>(pBuilding->m_iHealth());
				auto flMaxHealth = static_cast<float>(pBuilding->m_iMaxHealth());

				if (flHealth > 0.f && flMaxHealth > 0.f)
				{
					if (flHealth > flMaxHealth)
						flMaxHealth = flHealth;

					static constexpr int BAR_WIDTH = 2;
					int nBarX = x - ((BAR_WIDTH * 2) + 1);
					int nFillH = static_cast<int>(Math::RemapValClamped(flHealth, 0.0f, flMaxHealth, 0.0f, static_cast<float>(h)));

					H::Draw->OutlinedRect(nBarX - 1, (y + h - nFillH) - 1, BAR_WIDTH + 2, nFillH + 2, CFG::Color_ESP_Outline);
					H::Draw->Rect(nBarX, y + h - nFillH, BAR_WIDTH, nFillH, healthColor);
				}
			}

			if (CFG::ESP_Buildings_Level)
			{
				H::Draw->String(
					H::Fonts->Get(EFonts::ESP_SMALL),
					x + w + SPACING_X,
					y + (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall * nTextOffsetY++),
					textColor,
					POS_DEFAULT,
					"%d",
					pBuilding->m_iUpgradeLevel()
				);
			}

			if (CFG::ESP_Buildings_LevelBar)
			{
				int nBarH = 2;
				int nDrawY = y + h + nBarH + 1;
				auto flLevel = static_cast<float>(pBuilding->m_iUpgradeLevel());
				float flFillW = Math::RemapValClamped(flLevel, 1.0f, 3.0f, w / 3.0f, static_cast<float>(w));

				if (pBuilding->m_bMiniBuilding())
					flFillW = static_cast<float>(w);

				H::Draw->OutlinedRect(x - 1, nDrawY - 1, static_cast<int>(flFillW) + 2, nBarH + 2, CFG::Color_ESP_Outline);
				H::Draw->Rect(x, nDrawY, static_cast<int>(flFillW), nBarH, WHITE);
			}

			if (CFG::ESP_Buildings_Box)
			{
				H::Draw->OutlinedRect(x, y, w, h, entColor);
				H::Draw->OutlinedRect(x - 1, y - 1, w + 2, h + 2, CFG::Color_ESP_Outline);
			}

			if (CFG::ESP_Buildings_Conds)
			{
				if (nTextOffsetY > 0)
					nTextOffsetY += 1;

				int drawX = x + w + SPACING_X;
				int tall = H::Fonts->Get(EFonts::ESP_CONDS).m_nTall;

				Color_t color = CFG::Color_Conds;

				if (pBuilding->m_bBuilding())
				{
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "BUILDING");
				}

				else
				{
					if (pBuilding->GetClassId() == ETFClassIds::CObjectSentrygun && pBuilding->As<C_ObjectSentrygun>()->m_iAmmoShells() == 0)
						H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "NOSHELLS");

					if (!pBuilding->m_bMiniBuilding())
					{
						if (pBuilding->GetClassId() == ETFClassIds::CObjectSentrygun && pBuilding->As<C_ObjectSentrygun>()->m_iAmmoRockets() == 0)
						{
							if (pBuilding->m_iUpgradeLevel() == 3)
								H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "NOROCKETS");
						}
					}
				}

				if (pBuilding->IsDisabled())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "DISABLED");

				if (pBuilding->GetClassId() == ETFClassIds::CObjectSentrygun && pBuilding->As<C_ObjectSentrygun>()->m_bShielded())
					H::Draw->String(H::Fonts->Get(EFonts::ESP_CONDS), drawX, y + (tall * nTextOffsetY++), color, POS_DEFAULT, "WRANGLED");
			}
		}
	}

	if (CFG::ESP_World_Active)
	{
		I::MatSystemSurface->DrawSetAlphaMultiplier(CFG::ESP_World_Alpha);

		int x = 0, y = 0, w = 0, h = 0;

		if (!CFG::ESP_World_Ignore_HealthPacks)
		{
			auto color = CFG::Color_HealthPack;
			auto textColor = CFG::ESP_Text_Color == 0 ? color : CFG::Color_ESP_Text;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HEALTHPACKS))
			{
				if (!pEntity || !GetDrawBounds(pEntity, x, y, w, h))
					continue;

				if (CFG::ESP_World_Tracer)
				{
					auto nFromY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_From)
						{
						case 0: return 0;
						case 1: return H::Draw->GetScreenH() / 2;
						case 2: return H::Draw->GetScreenH();
						default: return 0;
						}
					};

					auto nToY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_To)
						{
						case 0: return y;
						case 1: return y + (h / 2);
						case 2: return y + h;
						default: return 0;
						}
					};

					H::Draw->Line(H::Draw->GetScreenW() / 2, nFromY(), x + (w / 2), nToY(), color);
				}

				if (CFG::ESP_World_Name)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						(y - (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall - 1)) - SPACING_Y,
						textColor,
						POS_CENTERX,
						"health"
					);
				}

				if (CFG::ESP_World_Box)
				{
					H::Draw->OutlinedRect(x, y, w, h, color);
					H::Draw->OutlinedRect(x - 1, y - 1, w + 2, h + 2, CFG::Color_ESP_Outline);
				}
			}
		}

		if (!CFG::ESP_World_Ignore_AmmoPacks)
		{
			auto color = CFG::Color_AmmoPack;
			auto textColor = CFG::ESP_Text_Color == 0 ? color : CFG::Color_ESP_Text;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::AMMOPACKS))
			{
				if (!pEntity || !GetDrawBounds(pEntity, x, y, w, h))
					continue;

				if (CFG::ESP_World_Tracer)
				{
					auto nFromY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_From)
						{
						case 0: return 0;
						case 1: return H::Draw->GetScreenH() / 2;
						case 2: return H::Draw->GetScreenH();
						default: return 0;
						}
					};

					auto nToY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_To)
						{
						case 0: return y;
						case 1: return y + (h / 2);
						case 2: return y + h;
						default: return 0;
						}
					};

					H::Draw->Line(H::Draw->GetScreenW() / 2, nFromY(), x + (w / 2), nToY(), color);
				}

				if (CFG::ESP_World_Name)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						(y - (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall - 1)) - SPACING_Y,
						textColor,
						POS_CENTERX,
						"ammo"
					);
				}

				if (CFG::ESP_World_Box)
				{
					H::Draw->OutlinedRect(x, y, w, h, color);
					H::Draw->OutlinedRect(x - 1, y - 1, w + 2, h + 2, CFG::Color_ESP_Outline);
				}
			}
		}

		bool bIgnoringAllProjectiles = CFG::ESP_World_Ignore_LocalProjectiles
			&& CFG::ESP_World_Ignore_EnemyProjectiles
			&& CFG::ESP_World_Ignore_TeammateProjectiles;

		if (!bIgnoringAllProjectiles)
		{
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ALL))
			{
				if (!pEntity || !pEntity->ShouldDraw())
					continue;

				bool bIsLocal = F::VisualUtils->IsEntityOwnedBy(pEntity, pLocal);

				if (CFG::ESP_World_Ignore_LocalProjectiles && bIsLocal)
					continue;

				if (!bIsLocal)
				{
					if (CFG::ESP_World_Ignore_EnemyProjectiles && pEntity->m_iTeamNum() != pLocal->m_iTeamNum())
						continue;

					if (CFG::ESP_World_Ignore_TeammateProjectiles && pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
						continue;
				}

				if (!GetDrawBounds(pEntity, x, y, w, h))
					continue;

				auto entColor = F::VisualUtils->GetEntityColor(pLocal, pEntity);
				auto textColor = CFG::ESP_Text_Color == 0 ? entColor : CFG::Color_ESP_Text;

				if (CFG::ESP_World_Tracer)
				{
					auto nFromY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_From)
						{
						case 0: return 0;
						case 1: return H::Draw->GetScreenH() / 2;
						case 2: return H::Draw->GetScreenH();
						default: return 0;
						}
					};

					auto nToY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_To)
						{
						case 0: return y;
						case 1: return y + (h / 2);
						case 2: return y + h;
						default: return 0;
						}
					};

					H::Draw->Line(H::Draw->GetScreenW() / 2, nFromY(), x + (w / 2), nToY(), entColor);
				}

				if (CFG::ESP_World_Name)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						(y - (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall - 1)) - SPACING_Y,
						textColor,
						POS_CENTERX,
						GetProjectileName(pEntity)
					);
				}

				if (CFG::ESP_World_Box)
				{
					H::Draw->OutlinedRect(x, y, w, h, entColor);
					H::Draw->OutlinedRect(x - 1, y - 1, w + 2, h + 2, CFG::Color_ESP_Outline);
				}
			}
		}

		if (!CFG::ESP_World_Ignore_Halloween_Gift)
		{
			auto color = CFG::Color_Halloween_Gift;
			auto textColor = CFG::ESP_Text_Color == 0 ? color : CFG::Color_ESP_Text;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HALLOWEEN_GIFT))
			{
				if (!pEntity || !pEntity->ShouldDraw() || !GetDrawBounds(pEntity, x, y, w, h))
					continue;

				if (CFG::ESP_World_Tracer)
				{
					auto nFromY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_From)
						{
						case 0: return 0;
						case 1: return H::Draw->GetScreenH() / 2;
						case 2: return H::Draw->GetScreenH();
						default: return 0;
						}
					};

					auto nToY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_To)
						{
						case 0: return y;
						case 1: return y + (h / 2);
						case 2: return y + h;
						default: return 0;
						}
					};

					H::Draw->Line(H::Draw->GetScreenW() / 2, nFromY(), x + (w / 2), nToY(), color);
				}

				if (CFG::ESP_World_Name)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						(y - (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall - 1)) - SPACING_Y,
						textColor,
						POS_CENTERX,
						"halloween gift"
					);
				}

				if (CFG::ESP_World_Box)
				{
					H::Draw->OutlinedRect(x, y, w, h, color);
					H::Draw->OutlinedRect(x - 1, y - 1, w + 2, h + 2, CFG::Color_ESP_Outline);
				}
			}
		}

		if (!CFG::ESP_World_Ignore_Halloween_Gift)
		{
			auto color = CFG::Color_MVM_Money;
			auto textColor = CFG::ESP_Text_Color == 0 ? color : CFG::Color_ESP_Text;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::MVM_MONEY))
			{
				if (!pEntity || !pEntity->ShouldDraw() || !GetDrawBounds(pEntity, x, y, w, h))
					continue;

				if (CFG::ESP_World_Tracer)
				{
					auto nFromY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_From)
						{
						case 0: return 0;
						case 1: return H::Draw->GetScreenH() / 2;
						case 2: return H::Draw->GetScreenH();
						default: return 0;
						}
					};

					auto nToY = [&]() -> int
					{
						switch (CFG::ESP_Tracer_To)
						{
						case 0: return y;
						case 1: return y + (h / 2);
						case 2: return y + h;
						default: return 0;
						}
					};

					H::Draw->Line(H::Draw->GetScreenW() / 2, nFromY(), x + (w / 2), nToY(), color);
				}

				if (CFG::ESP_World_Name)
				{
					H::Draw->String(
						H::Fonts->Get(EFonts::ESP_SMALL),
						x + (w / 2),
						(y - (H::Fonts->Get(EFonts::ESP_SMALL).m_nTall - 1)) - SPACING_Y,
						textColor,
						POS_CENTERX,
						"money"
					);
				}

				if (CFG::ESP_World_Box)
				{
					H::Draw->OutlinedRect(x, y, w, h, color);
					H::Draw->OutlinedRect(x - 1, y - 1, w + 2, h + 2, CFG::Color_ESP_Outline);
				}
			}
		}
	}

	I::MatSystemSurface->DrawSetAlphaMultiplier(flOriginalAlpha);
	
	// Draw projectile simulation paths
	DrawProjectileSimulation();
}

// Draw projectile simulation paths for debugging
void CESP::DrawProjectileSimulation()
{
	if (!CFG::Visuals_Draw_Movement_Path_Style)
		return;

	// Clean screenshot - don't draw simulation paths
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;
	
	// Draw paths from G::PathStorage
	for (auto it = G::PathStorage.begin(); it != G::PathStorage.end();)
	{
		auto& path = *it;
		
		// Check if path has expired
		if (path.m_flTime > 0 && path.m_flTime < I::GlobalVars->curtime)
		{
			it = G::PathStorage.erase(it);
			continue;
		}
		
		// Draw the path with proper style support
		if (path.m_vPath.size() >= 2)
		{
			int iStyle = path.m_iStyle;
			
			for (size_t i = 1; i < path.m_vPath.size(); i++)
			{
				// For timed paths with negative time (tick-based), skip points beyond the limit
				if (path.m_flTime < 0.f && path.m_vPath.size() - i > static_cast<size_t>(-path.m_flTime))
					continue;
				
				Vec3 vStart2D, vEnd2D;
				bool bStartOnScreen = H::Draw->W2S(path.m_vPath[i - 1], vStart2D);
				bool bEndOnScreen = H::Draw->W2S(path.m_vPath[i], vEnd2D);
				
				if (!bStartOnScreen && !bEndOnScreen)
					continue;
				
				switch (iStyle)
				{
				case G::PathStyle::Line:
				{
					if (bStartOnScreen && bEndOnScreen)
					{
						H::Draw->Line(
							static_cast<int>(vStart2D.x), static_cast<int>(vStart2D.y),
							static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
							path.m_tColor
						);
					}
					break;
				}
				case G::PathStyle::Separators:
				{
					if (bStartOnScreen && bEndOnScreen)
					{
						H::Draw->Line(
							static_cast<int>(vStart2D.x), static_cast<int>(vStart2D.y),
							static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
							path.m_tColor
						);
						
						// Draw separator perpendicular to the line every 4 segments
						if (!(i % 4))
						{
							const Vec3& vStart3D = path.m_vPath[i - 1];
							const Vec3& vEnd3D = path.m_vPath[i];
							
							Vec3 vDir = (vEnd3D - vStart3D);
							vDir.z = 0.f;
							float flLen = vDir.Length();
							if (flLen > 0.01f)
							{
								vDir = vDir / flLen;
								// Rotate 90 degrees for perpendicular separator
								Vec3 vPerp3D = vEnd3D + Vec3(-vDir.y * 12.f, vDir.x * 12.f, 0.f);
								Vec3 vPerp2D;
								if (H::Draw->W2S(vPerp3D, vPerp2D))
								{
									H::Draw->Line(
										static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
										static_cast<int>(vPerp2D.x), static_cast<int>(vPerp2D.y),
										path.m_tColor
									);
								}
							}
						}
					}
					break;
				}
				case G::PathStyle::Spaced: // Dashed
				{
					// Only draw every other segment for dashed effect
					if (!(i % 2) && bStartOnScreen && bEndOnScreen)
					{
						H::Draw->Line(
							static_cast<int>(vStart2D.x), static_cast<int>(vStart2D.y),
							static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
							path.m_tColor
						);
					}
					break;
				}
				case G::PathStyle::Arrows:
				{
					// Draw arrow heads every 3 segments
					if (!(i % 3) && bEndOnScreen)
					{
						const Vec3& vStart3D = path.m_vPath[i - 1];
						const Vec3& vEnd3D = path.m_vPath[i];
						
						Vec3 vDelta = vEnd3D - vStart3D;
						if (vDelta.Length() > 0.01f)
						{
							Vec3 vAngles = Math::CalcAngle(vStart3D, vEnd3D);
							Vec3 vForward, vRight;
							Math::AngleVectors(vAngles, &vForward, &vRight, nullptr);
							
							// Calculate arrow wing positions in 3D, then convert to 2D
							Vec3 vWing1_3D = vEnd3D - vForward * 5.f + vRight * 5.f;
							Vec3 vWing2_3D = vEnd3D - vForward * 5.f - vRight * 5.f;
							
							Vec3 vWing1_2D, vWing2_2D;
							if (H::Draw->W2S(vWing1_3D, vWing1_2D))
							{
								H::Draw->Line(
									static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
									static_cast<int>(vWing1_2D.x), static_cast<int>(vWing1_2D.y),
									path.m_tColor
								);
							}
							if (H::Draw->W2S(vWing2_3D, vWing2_2D))
							{
								H::Draw->Line(
									static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
									static_cast<int>(vWing2_2D.x), static_cast<int>(vWing2_2D.y),
									path.m_tColor
								);
							}
						}
					}
					break;
				}
				case G::PathStyle::Boxes:
				{
					if (bStartOnScreen && bEndOnScreen)
					{
						H::Draw->Line(
							static_cast<int>(vStart2D.x), static_cast<int>(vStart2D.y),
							static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
							path.m_tColor
						);
						
						// Draw a small box marker every 4 segments
						if (!(i % 4))
						{
							H::Draw->OutlinedRect(
								static_cast<int>(vEnd2D.x) - 2,
								static_cast<int>(vEnd2D.y) - 2,
								4, 4,
								path.m_tColor
							);
						}
					}
					break;
				}
				default:
				{
					// Fallback to simple line
					if (bStartOnScreen && bEndOnScreen)
					{
						H::Draw->Line(
							static_cast<int>(vStart2D.x), static_cast<int>(vStart2D.y),
							static_cast<int>(vEnd2D.x), static_cast<int>(vEnd2D.y),
							path.m_tColor
						);
					}
					break;
				}
				}
			}
		}
		
		++it;
	}
	
	// Draw lines from G::LineStorage
	for (auto it = G::LineStorage.begin(); it != G::LineStorage.end();)
	{
		auto& line = *it;
		
		// Check if line has expired
		if (line.m_flTime > 0 && line.m_flTime < I::GlobalVars->curtime)
		{
			it = G::LineStorage.erase(it);
			continue;
		}
		
		Vec3 vStart, vEnd;
		if (H::Draw->W2S(line.m_vStart, vStart) && H::Draw->W2S(line.m_vEnd, vEnd))
		{
			H::Draw->Line(
				static_cast<int>(vStart.x), static_cast<int>(vStart.y),
				static_cast<int>(vEnd.x), static_cast<int>(vEnd.y),
				line.m_tColor
			);
		}
		
		++it;
	}
	
	// Draw boxes from G::BoxStorage
	for (auto it = G::BoxStorage.begin(); it != G::BoxStorage.end();)
	{
		auto& box = *it;
		
		// Check if box has expired
		if (box.m_flTime > 0 && box.m_flTime < I::GlobalVars->curtime)
		{
			it = G::BoxStorage.erase(it);
			continue;
		}
		
		// Draw box edges
		Vec3 vCenter;
		if (H::Draw->W2S(box.m_vOrigin, vCenter))
		{
			// Simple box visualization - draw center point
			H::Draw->Rect(
				static_cast<int>(vCenter.x) - 2, static_cast<int>(vCenter.y) - 2,
				4, 4,
				box.m_tColorEdge
			);
		}
		
		++it;
	}
}
