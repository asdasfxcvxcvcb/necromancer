#include "ChatESP.h"
#include "../CFG.h"
#include "../Players/Players.h"
#include "../VisualUtils/VisualUtils.h"
#include <unordered_map>
#include <cmath>

void CChatESP::OnChatMessage(int iPlayerIndex, const std::string& sMessage)
{
	if (!CFG::Visuals_ChatESP_Active)
		return;

	if (sMessage.empty())
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iPlayerIndex);
	if (!pEntity)
		return;

	// Limit bubbles per player to avoid spam
	int iSamePlayerCount = 0;
	for (auto it = m_vBubbles.begin(); it != m_vBubbles.end();)
	{
		if (it->m_iPlayerIndex == iPlayerIndex)
		{
			iSamePlayerCount++;
			if (iSamePlayerCount >= 3)
			{
				it = m_vBubbles.erase(it);
				continue;
			}
		}
		++it;
	}

	std::string sClean = CleanMessage(sMessage);
	if (sClean.empty())
		return;

	ChatBubble_t tBubble;
	tBubble.m_iPlayerIndex = iPlayerIndex;
	tBubble.m_sMessage = sClean;
	tBubble.m_flCreateTime = I::GlobalVars->curtime;
	tBubble.m_flDuration = CFG::Visuals_ChatESP_Duration;
	tBubble.m_tColor = GetBubbleColor(iPlayerIndex);
	tBubble.m_flAnimProgress = 0.f; // Start animation at 0

	while (m_vBubbles.size() >= m_iMaxBubbles)
		m_vBubbles.pop_front();

	m_vBubbles.push_back(tBubble);
}

void CChatESP::Think()
{
	if (!CFG::Visuals_ChatESP_Active)
	{
		m_vBubbles.clear();
		return;
	}

	float flCurTime = I::GlobalVars->curtime;
	float flDeltaTime = I::GlobalVars->frametime;

	for (auto it = m_vBubbles.begin(); it != m_vBubbles.end();)
	{
		if (flCurTime - it->m_flCreateTime > it->m_flDuration)
			it = m_vBubbles.erase(it);
		else
		{
			// Smooth animation - ease out
			if (it->m_flAnimProgress < 1.f)
			{
				it->m_flAnimProgress += flDeltaTime * 5.f; // Animation speed
				if (it->m_flAnimProgress > 1.f)
					it->m_flAnimProgress = 1.f;
			}
			++it;
		}
	}
}

void CChatESP::OnLevelInit()
{
	m_vBubbles.clear();
}

void CChatESP::OnLevelShutdown()
{
	m_vBubbles.clear();
}


// Ease out cubic function for smooth animation
static float EaseOutCubic(float t)
{
	return 1.f - powf(1.f - t, 3.f);
}

void CChatESP::Draw()
{
	if (!CFG::Visuals_ChatESP_Active || m_vBubbles.empty())
		return;

	auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	std::unordered_map<int, int> mPlayerOffsets;

	for (auto& tBubble : m_vBubbles)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(tBubble.m_iPlayerIndex);
		if (!pEntity)
			continue;

		auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer || !pPlayer->IsAlive() || pPlayer->IsDormant())
			continue;

		Vec3 vHeadPos = GetPlayerHeadPos(tBubble.m_iPlayerIndex);
		if (vHeadPos.IsZero())
			continue;

		// Get distance
		float flDist = pLocal->GetRenderOrigin().DistTo(pPlayer->GetRenderOrigin());
		
		// Check max distance from config
		float flMaxDist = CFG::Visuals_ChatESP_MaxDistance;
		if (flDist > flMaxDist)
			continue;

		// Select font and scale based on distance relative to max distance slider
		// Full size when close, scales down smoothly as you approach max distance
		const CFont* pFont;
		float flDistScale = 1.0f;
		
		float flDistRatio = flDist / flMaxDist; // 0.0 = close, 1.0 = at max distance
		
		if (flDistRatio < 0.33f)
		{
			// Close range: full size
			pFont = &H::Fonts->Get(EFonts::ChatESP_Large);
			flDistScale = 1.0f;
		}
		else if (flDistRatio < 0.66f)
		{
			// Medium range
			pFont = &H::Fonts->Get(EFonts::ChatESP_Medium);
			flDistScale = 0.75f;
		}
		else
		{
			// Far range: smallest
			pFont = &H::Fonts->Get(EFonts::ChatESP_Small);
			flDistScale = 0.5f;
		}

		vHeadPos.z += 45.f;

		int& iOffset = mPlayerOffsets[tBubble.m_iPlayerIndex];
		vHeadPos.z += iOffset * 20.f;
		iOffset++;

		Vec3 vScreen;
		if (!H::Draw->W2S(vHeadPos, vScreen))
			continue;

		// Calculate alpha based on remaining time (fade out)
		float flTimeLeft = tBubble.m_flDuration - (I::GlobalVars->curtime - tBubble.m_flCreateTime);
		float flFadeTime = tBubble.m_flDuration * 0.2f;
		float flAlpha = 1.f;
		if (flTimeLeft < flFadeTime)
			flAlpha = flTimeLeft / flFadeTime;

		// Animation alpha (fade in)
		float flAnimAlpha = EaseOutCubic(tBubble.m_flAnimProgress);
		flAlpha *= flAnimAlpha;

		if (flAlpha <= 0.01f)
			continue;

		// Animation scale for pop-in effect combined with distance scale
		float flAnimScale = 0.5f + 0.5f * EaseOutCubic(tBubble.m_flAnimProgress);
		float flTotalScale = flAnimScale * flDistScale;

		// Truncate long messages
		std::string sDisplay = tBubble.m_sMessage;
		int iMaxChars = CFG::Visuals_ChatESP_MaxLength;
		if (static_cast<int>(sDisplay.length()) > iMaxChars)
			sDisplay = sDisplay.substr(0, iMaxChars - 3) + "...";

		// Get text size using the selected font
		int iTextW = 0, iTextH = 0;
		I::MatSystemSurface->GetTextSize(pFont->m_dwFont, Utils::ConvertUtf8ToWide(sDisplay).c_str(), iTextW, iTextH);

		// Padding scales with distance, text size comes from font
		int iPadX = static_cast<int>(8 * flTotalScale);
		int iPadY = static_cast<int>(4 * flTotalScale);
		int iBubbleW = iTextW + iPadX * 2;
		int iBubbleH = iTextH + iPadY * 2;
		int iBubbleX = static_cast<int>(vScreen.x) - iBubbleW / 2;
		int iBubbleY = static_cast<int>(vScreen.y) - iBubbleH;
		int iRadius = static_cast<int>(4 * flTotalScale);

		// Get entity color for outline (uses Color_Enemy from menu)
		Color_t tOutlineColor = F::VisualUtils->GetEntityColor(pLocal, pPlayer);
		tOutlineColor.a = static_cast<byte>(tOutlineColor.a * flAlpha);

		// Background with rounded corners
		Color_t tBgColor = CFG::Menu_Background;
		tBgColor.a = static_cast<byte>(220 * flAlpha);

		// Outline with entity color (rounded) - draw border first, then fill
		Color_t tBorderColor = tOutlineColor;
		H::Draw->FillRectRounded(iBubbleX - 1, iBubbleY - 1, iBubbleW + 2, iBubbleH + 2, iRadius + 1, tBorderColor);
		H::Draw->FillRectRounded(iBubbleX, iBubbleY, iBubbleW, iBubbleH, iRadius, tBgColor);

		// Pointer triangle
		if (CFG::Visuals_ChatESP_ShowPointer)
		{
			int iTriSize = static_cast<int>(6 * flTotalScale);
			int iTriX = static_cast<int>(vScreen.x);
			int iTriY = iBubbleY + iBubbleH;

			// Draw filled triangle pointer
			std::array<Vec2, 3> triPoints = {
				Vec2(static_cast<float>(iTriX - iTriSize), static_cast<float>(iTriY)),
				Vec2(static_cast<float>(iTriX + iTriSize), static_cast<float>(iTriY)),
				Vec2(static_cast<float>(iTriX), static_cast<float>(iTriY + iTriSize))
			};
			H::Draw->FilledTriangle(triPoints, tBgColor);

			// Triangle outline
			H::Draw->Line(iTriX - iTriSize, iTriY, iTriX, iTriY + iTriSize, tBorderColor);
			H::Draw->Line(iTriX + iTriSize, iTriY, iTriX, iTriY + iTriSize, tBorderColor);
		}

		// Text (centered in bubble)
		Color_t tTextColor = CFG::Menu_Text_Active;
		tTextColor.a = static_cast<byte>(255 * flAlpha);
		
		H::Draw->String(*pFont, iBubbleX + iBubbleW / 2, iBubbleY + iBubbleH / 2, tTextColor, POS_CENTERXY, sDisplay.c_str());
	}
}

std::string CChatESP::CleanMessage(const std::string& sRaw)
{
	std::string sOut;
	sOut.reserve(sRaw.length());

	for (size_t i = 0; i < sRaw.length(); i++)
	{
		unsigned char c = sRaw[i];

		if (c >= 0x01 && c <= 0x06)
			continue;

		if (c == 0x07 && i + 6 < sRaw.length())
		{
			i += 6;
			continue;
		}

		if (c == 0x08 && i + 8 < sRaw.length())
		{
			i += 8;
			continue;
		}

		sOut += c;
	}

	size_t start = sOut.find_first_not_of(" \t\n\r");
	if (start == std::string::npos)
		return "";
	size_t end = sOut.find_last_not_of(" \t\n\r");

	return sOut.substr(start, end - start + 1);
}

Vec3 CChatESP::GetPlayerHeadPos(int iPlayerIndex)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iPlayerIndex);
	if (!pEntity)
		return {};

	auto pPlayer = pEntity->As<C_TFPlayer>();
	if (!pPlayer)
		return {};

	// Get head position from bones
	matrix3x4_t aBones[MAXSTUDIOBONES];
	if (!pPlayer->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		return pPlayer->GetRenderOrigin() + Vec3(0, 0, 72);

	// Get head bone from model
	const model_t* pModel = pPlayer->GetModel();
	if (!pModel)
		return pPlayer->GetRenderOrigin() + Vec3(0, 0, 72);

	const studiohdr_t* pStudioHdr = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pStudioHdr)
		return pPlayer->GetRenderOrigin() + Vec3(0, 0, 72);

	// Find head hitbox
	for (int nSet = 0; nSet < pStudioHdr->numhitboxsets; nSet++)
	{
		mstudiohitboxset_t* pSet = pStudioHdr->pHitboxSet(nSet);
		if (!pSet)
			continue;

		for (int nBox = 0; nBox < pSet->numhitboxes; nBox++)
		{
			mstudiobbox_t* pBox = pSet->pHitbox(nBox);
			if (!pBox || pBox->group != 0) // Group 0 is typically head
				continue;

			Vec3 vHead;
			vHead.x = aBones[pBox->bone][0][3];
			vHead.y = aBones[pBox->bone][1][3];
			vHead.z = aBones[pBox->bone][2][3];
			return vHead;
		}
	}

	// Fallback to origin + height
	return pPlayer->GetRenderOrigin() + Vec3(0, 0, 72);
}

Color_t CChatESP::GetBubbleColor(int iPlayerIndex)
{
	auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return CFG::Menu_Accent_Primary;

	auto pEntity = I::ClientEntityList->GetClientEntity(iPlayerIndex);
	if (!pEntity)
		return CFG::Menu_Accent_Primary;

	auto pPlayer = pEntity->As<C_TFPlayer>();
	if (!pPlayer)
		return CFG::Menu_Accent_Primary;

	// Always use entity color (enemy/teammate/friend colors from menu)
	return F::VisualUtils->GetEntityColor(pLocal, pPlayer);
}
