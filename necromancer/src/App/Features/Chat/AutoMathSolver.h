#pragma once

#include "../../../SDK/SDK.h"

// Process incoming chat message and auto-solve math problems
void ProcessChatForMath(const wchar_t* message);

// Sends queued math solver answers once their delay has elapsed (call every frame)
void ProcessMathAnswerQueue();
