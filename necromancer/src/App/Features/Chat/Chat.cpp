#include "../../../SDK/SDK.h"
#include "../CFG.h"

// Chat Spammer
static float g_flLastSpamTime = 0.0f;

void RunChatSpammer()
{
	if (!CFG::Misc_Chat_Spammer_Active)
		return;

	if (CFG::Misc_Chat_Spammer_Text.empty())
		return;

	if (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame())
		return;

	float flCurrentTime = I::GlobalVars->realtime;
	if (flCurrentTime - g_flLastSpamTime < CFG::Misc_Chat_Spammer_Interval)
		return;

	g_flLastSpamTime = flCurrentTime;

	std::string cmd = "say \"" + CFG::Misc_Chat_Spammer_Text + "\"";
	I::EngineClient->ClientCmd_Unrestricted(cmd.c_str());
}

// Killsay - called when we kill someone
void OnKill(const char* victimName)
{
	if (!CFG::Misc_Chat_Killsay_Active)
		return;

	if (CFG::Misc_Chat_Killsay_Text.empty())
		return;

	std::string message = CFG::Misc_Chat_Killsay_Text;
	
	// Replace {name} with victim's name if present
	size_t pos = message.find("{name}");
	if (pos != std::string::npos && victimName)
	{
		message.replace(pos, 6, victimName);
	}

	std::string cmd = "say \"" + message + "\"";
	I::EngineClient->ClientCmd_Unrestricted(cmd.c_str());
}
