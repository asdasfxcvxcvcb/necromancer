#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/ChatESP/ChatESP.h"
#include "../Features/AutoVote/AutoVote.h"
#include "../Features/AutoUberPop/AutoUberPop.h"

MAKE_SIGNATURE(CUserMessages_DispatchUserMessage, "client.dll", "40 56 48 83 EC ? 49 8B F0", 0x0);

// User message types
enum EUserMessageType
{
	UM_VoiceSubtitle = 1,
	UM_SayText2 = 4,
	UM_TextMsg = 5,
	UM_Shake = 10,
	UM_Fade = 11,
	UM_VGUIMenu = 12,
	UM_VoteStart = 46
};

MAKE_HOOK(CUserMessages_DispatchUserMessage, Signatures::CUserMessages_DispatchUserMessage.Get(), bool, __fastcall,
	void* ecx, int msg_type, bf_read& msg_data)
{
	if (msg_type == UM_Shake && CFG::Visuals_Remove_Screen_Shake)
	{
		return true;
	}

	if (msg_type == UM_Fade && CFG::Visuals_Remove_Screen_Fade)
	{
		return true;
	}

	// MOTD removal - VGUIMenu message with "info" panel name
	if (msg_type == UM_VGUIMenu && CFG::Visuals_Remove_MOTD)
	{
		// Read the panel name from the message data
		auto bufData = reinterpret_cast<const char*>(msg_data.m_pData);
		if (bufData && strcmp(bufData, "info") == 0)
		{
			I::EngineClient->ClientCmd_Unrestricted("closedwelcomemenu");
			return true;
		}
	}

	if (msg_type == UM_TextMsg && F::SeedPred->ParsePlayerPerf(msg_data))
	{
		return true;
	}

	// Auto F1/F2 - automatic voting on the VoteStart user message
	if (msg_type == UM_VoteStart)
	{
		F::AutoVote->UserMessage(msg_data);
	}

	// AutoUber - track "Medic!" voice menu calls for the ActivateOnVoice pop
	if (msg_type == UM_VoiceSubtitle)
	{
		F::AutoUberPop->UserMessage(msg_data);
	}

	// Chat ESP - capture SayText2 messages
	if (msg_type == UM_SayText2 && CFG::Visuals_ChatESP_Active)
	{
		const int nStartBit = msg_data.GetNumBitsRead();

		msg_data.SetAssertOnOverflow(false);

		int iPlayerIndex = msg_data.ReadByte();
		msg_data.ReadByte(); // bWantsToChat

		// Skip the localized message format string
		char szFormat[256];
		msg_data.ReadString(szFormat, sizeof(szFormat));

		// Read player name (skip it)
		char szPlayerName[256];
		msg_data.ReadString(szPlayerName, sizeof(szPlayerName));

		// Read the actual chat message
		char szMessage[256];
		msg_data.ReadString(szMessage, sizeof(szMessage));

		const bool bOverflow = msg_data.IsOverflowed();
		msg_data.Seek(nStartBit);

		// Pass to ChatESP
		if (!bOverflow && iPlayerIndex > 0 && szMessage[0] != '\0')
		{
			F::ChatESP->OnChatMessage(iPlayerIndex, szMessage);
		}
	}

	return CALL_ORIGINAL(ecx, msg_type, msg_data);
}
