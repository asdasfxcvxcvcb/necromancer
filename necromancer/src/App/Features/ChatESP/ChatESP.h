#pragma once

#include "../../../SDK/SDK.h"
#include <deque>
#include <string>
#include <unordered_map>

struct ChatBubble_t
{
	int m_iPlayerIndex = 0;
	std::string m_sMessage;
	float m_flCreateTime = 0.f;
	float m_flDuration = 5.f;
	Color_t m_tColor;
	float m_flAnimProgress = 0.f; // 0 to 1 for smooth appear animation
};

class CChatESP
{
public:
	void OnChatMessage(int iPlayerIndex, const std::string& sMessage);
	void Draw();
	void Think();
	void OnLevelInit(); // Clear bubbles on map change
	void OnLevelShutdown(); // Clear bubbles when leaving map

private:
	std::deque<ChatBubble_t> m_vBubbles;
	size_t m_iMaxBubbles = 32;

	std::string CleanMessage(const std::string& sRaw);
	Vec3 GetPlayerHeadPos(int iPlayerIndex);
	Color_t GetBubbleColor(int iPlayerIndex);
};

MAKE_SINGLETON_SCOPED(CChatESP, ChatESP, F);
