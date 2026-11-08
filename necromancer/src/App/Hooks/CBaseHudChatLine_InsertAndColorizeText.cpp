#include "../../SDK/SDK.h"
#include "../Features/StreamerMode/StreamerMode.h"
#include "../Features/CFG.h"
#include "../Features/Players/Players.h"
#include "../Features/Chat/AutoMathSolver.h"
#include "../Features/Chat/AutoVoteLifted.h"
#include <algorithm>

MAKE_SIGNATURE(CBaseHudChatLine_InsertAndColorizeText, "client.dll", "44 89 44 24 ? 55 53 56 57", 0x0);

// Convert wide string to UTF8
static std::string WideToUTF8(const std::wstring& wstr)
{
	if (wstr.empty()) return "";
	int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string result(size - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), size, nullptr, nullptr);
	return result;
}

// Convert UTF8 to wide string
static std::wstring UTF8ToWide(const std::string& str)
{
	if (str.empty()) return L"";
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	std::wstring result(size - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), size);
	return result;
}

MAKE_HOOK(CBaseHudChatLine_InsertAndColorizeText, Signatures::CBaseHudChatLine_InsertAndColorizeText.Get(), void, __fastcall,
	void* ecx, wchar_t* buf, int clientIndex)
{
	// Process chat for math problems
	if (CFG::Misc_Chat_AutoMath_Active)
		ProcessChatForMath(buf);

	// Process chat for vote cooldown lifted
	if (CFG::Misc_Chat_VoteBanOnLifted || CFG::Misc_Chat_VoteMuteOnLifted)
		ProcessChatForVoteLifted(buf);

	auto pResource = GetTFPlayerResource();
	if (!pResource || !pResource->IsValid(clientIndex))
	{
		CALL_ORIGINAL(ecx, buf, clientIndex);
		return;
	}

	// Convert to UTF8 for easier string manipulation
	std::string sMessage = WideToUTF8(buf);

	// Streamer Mode: Replace ALL player names in the message
	if (CFG::Misc_Streamer_Mode > 0)
	{
		std::vector<std::pair<std::string, std::string>> vReplace;

		// Build list of all name replacements
		for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
		{
			if (!pResource->IsValid(n))
				continue;

			const char* sOriginalName = pResource->GetName(n);
			const char* sReplaceName = StreamerMode::GetPlayerName(n);

			if (sOriginalName && sReplaceName)
			{
				// Skip if name is same as replacement (case-insensitive)
				std::string sOrigLower = sOriginalName;
				std::string sReplLower = sReplaceName;
				std::transform(sOrigLower.begin(), sOrigLower.end(), sOrigLower.begin(), ::tolower);
				std::transform(sReplLower.begin(), sReplLower.end(), sReplLower.begin(), ::tolower);
				if (sOrigLower == sReplLower)
					continue;

				vReplace.emplace_back(sOriginalName, sReplaceName);
			}
		}

		// Apply all replacements (case-insensitive matching)
		for (auto& [sFind, sReplace] : vReplace)
		{
			std::string sFindLower = sFind;
			std::transform(sFindLower.begin(), sFindLower.end(), sFindLower.begin(), ::tolower);

			size_t iPos = 0;
			while (true)
			{
				std::string sMessageLower = sMessage;
				std::transform(sMessageLower.begin(), sMessageLower.end(), sMessageLower.begin(), ::tolower);

				auto iFind = sMessageLower.find(sFindLower, iPos);
				if (iFind == std::string::npos)
					break;

				iPos = iFind + sReplace.length();
				sMessage = sMessage.replace(iFind, sFind.length(), sReplace);
			}
		}
	}

	// Chat tags
	if (CFG::Visuals_Chat_Name_Tags && ecx)
	{
		std::string sTag, cColor;
		if (clientIndex == I::EngineClient->GetLocalPlayer())
		{
			sTag = "Local";
			cColor = CFG::Color_Local.toHexStr();
		}
		else
		{
			C_TFPlayer* pl = I::ClientEntityList->GetClientEntity(clientIndex)->As<C_TFPlayer>();
			if (pl && pl->IsPlayerOnSteamFriendsList())
			{
				sTag = "Friend";
				cColor = CFG::Color_Friend.toHexStr();
			}

			PlayerPriority pi{};
			if (F::Players->GetInfo(clientIndex, pi))
			{
				if (pi.Cheater)
				{
					sTag = "Cheater";
					cColor = CFG::Color_Cheater.toHexStr();
				}
				else if (pi.RetardLegit)
				{
					sTag = "Retard Legit";
					cColor = CFG::Color_RetardLegit.toHexStr();
				}
				else if (pi.Ignored)
				{
					sTag = "Ignored";
					cColor = CFG::Color_Friend.toHexStr();
				}
			}
		}

		if (!sTag.empty())
		{
			if (!sMessage.empty() && sMessage[0] == '\x2')
				sMessage.erase(0, 1);

			const std::string sPrefix = std::format("\x8{}[{}] \x1", cColor, sTag);
			sMessage.insert(0, sPrefix);

			const char* sPlayerName = pResource->GetName(clientIndex);
			if (sPlayerName && *sPlayerName)
			{
				size_t iName = sMessage.find(sPlayerName, sPrefix.length());
				if (iName != std::string::npos)
				{
					sMessage.insert(iName + strlen(sPlayerName), "\x1");
					sMessage.insert(iName, "\x3");
				}
			}
		}
	}

	CALL_ORIGINAL(ecx, const_cast<wchar_t*>(UTF8ToWide(sMessage).c_str()), clientIndex);
}
