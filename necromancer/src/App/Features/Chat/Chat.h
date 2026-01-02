#pragma once

#include "../../../SDK/SDK.h"

// Initialize chat files (creates folder and default files if needed)
void InitChatFiles();

// Run chat spammer (call from main loop)
void RunChatSpammer();

// Called when local player kills someone
void OnKill(const char* victimName, int victimEntIndex = -1);

// Reload messages from text files
void ReloadChatSpammerMessages();
void ReloadKillsayMessages();

// Open the chat text files folder
void OpenChatTextFiles();
