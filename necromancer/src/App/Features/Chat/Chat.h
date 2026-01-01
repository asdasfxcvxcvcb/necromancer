#pragma once

#include "../../../SDK/SDK.h"

// Run chat spammer (call from main loop)
void RunChatSpammer();

// Called when local player kills someone
void OnKill(const char* victimName);
