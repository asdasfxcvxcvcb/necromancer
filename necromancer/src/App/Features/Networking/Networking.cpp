#include "Networking.h"
#include "../../../SDK/TF2/demo.h"

// Signatures for CL_Move rebuild internals
MAKE_SIGNATURE(Net_Time, "engine.dll", "F2 0F 10 0D ? ? ? ? F2 0F 5C 0D", 0x4);
MAKE_SIGNATURE(Host_Framerate_unbounded, "engine.dll", "F3 0F 10 05 ? ? ? ? F3 0F 11 45 ? F3 0F 11 4D ? 89 45", 0x4);
MAKE_SIGNATURE(Host_Framerate_stddeviation, "engine.dll", "F3 0F 10 0D ? ? ? ? 48 8D 54 24 ? 0F 57 C0 48 89 44 24 ? 8B 05", 0x4);

void CNetworking::CL_Sendmove()
{
	std::byte data[4000];
	const auto pNetChan = reinterpret_cast<CNetChannel*>(I::ClientState->m_NetChannel);
	const int Chocked = pNetChan->m_nChokedPackets;
	const int NextCommandNumber = I::ClientState->lastoutgoingcommand + Chocked + 1;

	CLC_Move Message;
	Message.m_DataOut.StartWriting(data, sizeof(data));

	Message.m_nNewCommands = 1 + Chocked;
	Message.m_nNewCommands = std::clamp(Message.m_nNewCommands, 0, MAX_NEW_COMMANDS);
	const int ExtraCommands = (Chocked + 1) - Message.m_nNewCommands;
	const int BackupCommands = std::max(2, ExtraCommands);
	Message.m_nBackupCommands = std::clamp(BackupCommands, 0, MAX_BACKUP_COMMANDS);

	const int numcmds = Message.m_nNewCommands + Message.m_nBackupCommands;

	int from = -1;
	bool bOK = true;
	for (int to = NextCommandNumber - numcmds + 1; to <= NextCommandNumber; to++) {
		const bool isnewcmd = to >= NextCommandNumber - Message.m_nNewCommands + 1;
		bOK = bOK && I::BaseClientDLL->WriteUsercmdDeltaToBuffer(&Message.m_DataOut, from, to, isnewcmd);
		from = to;
	}

	if (bOK) {
		I::ClientState->m_NetChannel->SendNetMsg(Message, false, false);
	}
}

void CNetworking::CL_Move(float AccumulatedExtraSamples, bool FinalTick) {
	auto cl_cmdrate = I::CVar->FindVar("cl_cmdrate");

	if (!I::ClientState->IsConnected())
	{
		return;
	}

	bSendPacket = true;

	double NetTime = *reinterpret_cast<double*>(Signatures::Net_Time.Get());

	if (I::DemoPlayer->IsPlayingBack())
	{
		if (I::ClientState->ishltv || I::ClientState->isreplay) {
			bSendPacket = false;
		}

		else {
			return;
		}
	}

	if ((!I::ClientState->m_NetChannel->IsLoopback()) &&
		((NetTime < I::ClientState->m_flNextCmdTime) || !I::ClientState->m_NetChannel->CanPacket() || !FinalTick)) {
		bSendPacket = false;
	}

	if (I::ClientState->IsActive()) {
		int nextcommandnr = I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands + 1;

		// Have client .dll create and store usercmd structure
		I::BaseClientDLL->CreateMove(nextcommandnr, I::GlobalVars->interval_per_tick - AccumulatedExtraSamples,
			!I::ClientState->IsPaused());

		// Store new usercmd to dem file
		if (I::DemoRecorder->IsRecording()) {
			// Back up one because we've incremented outgoing_sequence each frame by 1 unit
			I::DemoRecorder->RecordUserInput(nextcommandnr);
		}

		if (bSendPacket) {
			CL_Sendmove();
		}
		else {
			// netchanll will increase internal outgoing sequnce number too
			I::ClientState->m_NetChannel->SetChoked();
			// Mark command as held back so we'll send it next time
			I::ClientState->chokedcommands++;
		}

	}
	if (!bSendPacket)
		return;

	// Request non delta compression if high packet loss, show warning message
	bool HasProblem = I::ClientState->m_NetChannel->IsTimingOut() && !I::DemoPlayer->IsPlayingBack() && I::ClientState->IsActive();
	if (HasProblem) {
		I::ClientState->m_nDeltaTick = -1;
	}

	float unbounded = *reinterpret_cast<float*>(Signatures::Host_Framerate_unbounded.Get());
	float stddeviation = *reinterpret_cast<float*>(Signatures::Host_Framerate_stddeviation.Get());

	if (I::ClientState->IsActive()) {
		NET_Tick mymsg(I::ClientState->m_nDeltaTick, unbounded, stddeviation);
		I::ClientState->m_NetChannel->SendNetMsg(mymsg);
	}

	I::ClientState->lastoutgoingcommand = I::ClientState->m_NetChannel->SendDatagram(NULL);
	I::ClientState->chokedcommands = 0;

	if (I::ClientState->IsActive()) {
		// use full update rate when active
		float commandInterval = 1.0f / cl_cmdrate->GetFloat();
		float maxDelta = std::min(I::GlobalVars->interval_per_tick, commandInterval);
		float delta = std::clamp((float)(NetTime - I::ClientState->m_flNextCmdTime), 0.0f, maxDelta);
		I::ClientState->m_flNextCmdTime = NetTime + commandInterval - delta;
	}
	else {
		// during signon process send only 5 packets/second
		I::ClientState->m_flNextCmdTime = NetTime + (1.0f / 5.0f);
	}
}

int CNetworking::get_latest_command_number()
{
	return (I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands + 1);
}
