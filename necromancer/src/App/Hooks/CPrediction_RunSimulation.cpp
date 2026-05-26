// ============================================
// CPrediction_RunSimulation Hook
// ============================================
// Ports Amalgam's tickbase fix for doubletap shifts.
//
// When we shift N ticks, the client creates N commands with tickbase
// T+1, T+2, ..., T+N. The server receives them all at once and processes
// them sequentially, advancing its tickbase by 1 per command.
//
// The problem: When Prediction::Update replays these commands via
// RunSimulation, m_nTickBase has already been advanced by the shift.
// The server hasn't ACKed them yet, so the client's predicted tickbase
// is ahead of what the server will confirm, causing prediction errors.
//
// The fix (from Amalgam): When a shift command is being simulated,
// subtract the shift amount from m_nTickBase. RunCommand then adds +1
// per command naturally, so the net tickbase advancement is correct
// from the server's perspective.
// ============================================

#include "../../SDK/SDK.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/TickbaseManip/TickbaseManip.h"

MAKE_SIGNATURE(CPrediction_RunSimulation, "client.dll", "48 83 EC 38 4C 8B 44", 0x0);

// Tickbase fix entries — recorded BEFORE a shift starts, applied during RunSimulation.
//
// Server flow (player.cpp:PhysicsSimulate):
//   1. DetermineSimulationTicks() → total queued commands = N
//   2. AdjustPlayerTimeBase(N) → sets tickbase = idealFinal - N + simTicksThisFrame
//   3. For each command: RunCommand() → m_nTickBase++ at end
//   Result: tickbase ends at idealFinal after all N commands.
//
// Client must mirror this: on the FIRST RunSimulation of a shift batch,
// apply AdjustPlayerTimeBase with the full shift count. Subsequent
// RunSimulations in the same batch naturally increment tickbase by 1.
struct TickbaseFix_t
{
    int m_iStartCommandNumber;         // First command number of the shift batch
    int m_iEndCommandNumber;           // Last command number of the shift batch
    int m_iTickbaseShift;              // Total ticks shifted (N)
    bool m_bApplied;                   // Applied AdjustPlayerTimeBase on first cmd?
};
static std::vector<TickbaseFix_t> s_vTickbaseFixes = {};

static void ApplyWindowedTickbaseRetime(C_TFPlayer* pLocal, int nSimulationTicks)
{
    if (!pLocal || !I::GlobalVars || nSimulationTicks <= 0)
        return;

    if (I::GlobalVars->maxClients == 1)
    {
        pLocal->m_nTickBase() = I::GlobalVars->tickcount - nSimulationTicks + I::GlobalVars->simTicksThisFrame;
        return;
    }

    static ConVar* sv_clockcorrection_msecs = I::CVar ? I::CVar->FindVar("sv_clockcorrection_msecs") : nullptr;
    const float flClockCorrectionMs = sv_clockcorrection_msecs ? sv_clockcorrection_msecs->GetFloat() : 60.0f;
    const float flCorrectionSeconds = std::clamp(flClockCorrectionMs / 1000.0f, 0.0f, 1.0f);
    const int nCorrectionTicks = TIME_TO_TICKS(flCorrectionSeconds);

    const int nIdealFinalTick = I::GlobalVars->tickcount + nCorrectionTicks;
    const int nEstimatedFinalTick = pLocal->m_nTickBase() + nSimulationTicks;

    const int nTooFastLimit = nIdealFinalTick + nCorrectionTicks;
    const int nTooSlowLimit = nIdealFinalTick - nCorrectionTicks;

    if (nEstimatedFinalTick > nTooFastLimit || nEstimatedFinalTick < nTooSlowLimit)
    {
        pLocal->m_nTickBase() = nIdealFinalTick - nSimulationTicks + I::GlobalVars->simTicksThisFrame;
    }
}

MAKE_HOOK(CPrediction_RunSimulation, Signatures::CPrediction_RunSimulation.Get(), void, __fastcall,
    void* rcx, int current_command, float curtime, CUserCmd* cmd, C_TFPlayer* localPlayer)
{
    // Safety check
    if (!localPlayer || !cmd)
        return CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);

    // Apply tickbase fix for shift commands
    // Only apply AdjustPlayerTimeBase on the FIRST command of the batch,
    // just like the server does it once before running all commands.
    // Subsequent RunSimulation calls in the same batch will naturally
    // increment tickbase by 1 via RunCommand's m_nTickBase++.
    for (auto it = s_vTickbaseFixes.begin(); it != s_vTickbaseFixes.end();)
    {
        // Clean up entries that have been ACKed by the server
        if (it->m_iEndCommandNumber <= I::ClientState->last_command_ack)
        {
            it = s_vTickbaseFixes.erase(it);
            continue;
        }

        // Only apply on the first command of the batch
        if (!it->m_bApplied && cmd->command_number == it->m_iStartCommandNumber)
        {
            ApplyWindowedTickbaseRetime(localPlayer, it->m_iTickbaseShift);
            it->m_bApplied = true;
            break;
        }
        ++it;
    }

    F::EnginePrediction->AdjustPlayers(localPlayer);
    CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);
    F::EnginePrediction->RestorePlayers();
}

// Public interface for level shutdown cleanup
namespace TickbaseFix
{
    void Add(int nStartCommandNumber, int nTickbaseShift)
    {
        if (nStartCommandNumber <= 0 || nTickbaseShift <= 0)
            return;

        // Avoid duplicates for the same start command
        for (auto& fix : s_vTickbaseFixes)
        {
            if (fix.m_iStartCommandNumber == nStartCommandNumber)
            {
                fix.m_iEndCommandNumber = nStartCommandNumber + nTickbaseShift - 1;
                fix.m_iTickbaseShift = nTickbaseShift;
                fix.m_bApplied = false;
                return;
            }
        }

        s_vTickbaseFixes.emplace_back(TickbaseFix_t{
            nStartCommandNumber,
            nStartCommandNumber + nTickbaseShift - 1,
            nTickbaseShift,
            false
        });
    }

    void Clear() { s_vTickbaseFixes.clear(); }
}
