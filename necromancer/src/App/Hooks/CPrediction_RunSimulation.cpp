// ============================================
// CPrediction_RunSimulation Hook
// ============================================
// This hook fixes prediction errors during doubletap/tick shifting.
// When we shift ticks, the server processes commands differently than
// the client predicts. This hook adjusts the tickbase during prediction
// to match what the server will actually do.
//
// Based on Amalgam's implementation.
// This is ADDITIVE - it doesn't change your existing DT logic,
// it just makes prediction more accurate during shifts.
//
// With CL_Move Rebuild:
// - Charging: We skip CL_Move calls, banking ticks
// - Shifting: We call CL_Move multiple times, spending banked ticks
// - The tickbase adjustment ensures prediction matches server state
// ============================================

#include "../../SDK/SDK.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/TickbaseManip/TickbaseManip.h"

// Signature for CPrediction::RunSimulation (different from RunCommand!)
// RunSimulation is called for each command during prediction
MAKE_SIGNATURE(CPrediction_RunSimulation, "client.dll", "48 83 EC 38 4C 8B 44", 0x0);

// ============================================
// Tickbase Fix Tracking
// ============================================
// When we start a shift, we record:
// - The command that started the shift
// - The last outgoing command number at that time
// - How many ticks we're shifting
//
// During prediction, if we see that command again, we adjust
// the tickbase to account for the shift.

struct TickbaseFix_t
{
    CUserCmd* m_pCmd;               // The command that started the shift
    int m_iLastOutgoingCommand;     // lastoutgoingcommand when shift started
    int m_iTickbaseShift;           // How many ticks we're shifting
};

static std::vector<TickbaseFix_t> s_vTickbaseFixes = {};

// ============================================
// Public Interface
// ============================================

namespace TickbaseFix
{
    // Call this when starting a tick shift (in CL_Move or RapidFire)
    // Records the shift so prediction can be adjusted
    inline void OnShiftStart(int nTicksToShift)
    {
        if (!G::CurrentUserCmd || nTicksToShift <= 0)
            return;
        
        s_vTickbaseFixes.emplace_back(
            G::CurrentUserCmd,
            I::ClientState->lastoutgoingcommand,
            nTicksToShift
        );
    }
    
    // Call this on level shutdown to clear state
    inline void Clear()
    {
        s_vTickbaseFixes.clear();
    }
}

// ============================================
// Hook Implementation
// ============================================

MAKE_HOOK(CPrediction_RunSimulation, Signatures::CPrediction_RunSimulation.Get(), void, __fastcall,
    void* rcx, int current_command, float curtime, CUserCmd* cmd, C_TFPlayer* localPlayer)
{
    // Safety check
    if (!localPlayer || !cmd)
        return CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);
    
    // ============================================
    // Record new shifts
    // ============================================
    // When we're in the middle of a shift and this is the first tick,
    // record it for tickbase adjustment
    if (Shifting::bShifting && !Shifting::bShiftingWarp && Shifting::nCurrentShiftTick == 0)
    {
        // Only record if we haven't already recorded this shift
        bool bAlreadyRecorded = false;
        for (const auto& fix : s_vTickbaseFixes)
        {
            if (fix.m_pCmd == cmd)
            {
                bAlreadyRecorded = true;
                break;
            }
        }
        
        if (!bAlreadyRecorded && Shifting::nTotalShiftTicks > 0)
        {
            s_vTickbaseFixes.emplace_back(
                cmd,
                I::ClientState->lastoutgoingcommand,
                Shifting::nTotalShiftTicks
            );
        }
    }
    
    // ============================================
    // Clean up old fixes and apply current ones
    // ============================================
    int iTickbaseAdjustment = 0;
    
    for (auto it = s_vTickbaseFixes.begin(); it != s_vTickbaseFixes.end();)
    {
        // Remove fixes for commands that have been acknowledged by server
        if (it->m_iLastOutgoingCommand < I::ClientState->last_command_ack)
        {
            it = s_vTickbaseFixes.erase(it);
            continue;
        }
        
        // If this is the command that started a shift, apply the adjustment
        if (cmd == it->m_pCmd)
        {
            iTickbaseAdjustment = it->m_iTickbaseShift;
            break;
        }
        
        ++it;
    }
    
    // ============================================
    // Apply tickbase adjustment
    // ============================================
    // Temporarily adjust tickbase for this prediction run
    // This makes the client predict the same state the server will have
    if (iTickbaseAdjustment > 0)
    {
        localPlayer->m_nTickBase() -= iTickbaseAdjustment;
    }
    
    // NOTE: We don't call AdjustPlayers/RestorePlayers here because:
    // 1. It's already called in CPrediction_RunCommand hook
    // 2. It's already called in EnginePrediction::Simulate
    // Calling it here too causes double-adjustment which breaks ESP scaling
    
    CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);
    
    // Restore tickbase (the original function may have modified it)
    // Note: We don't restore here because the game's prediction system
    // handles tickbase updates. The adjustment is only for this specific
    // prediction run.
}
