#pragma once
#include "../../../SDK/SDK.h"

class CAutoVote
{
public:
	void UserMessage(bf_read& msgData);
	void Tick();

private:
	struct VoteEntry_t
	{
		int iVoteID = 0;
		int iOption = 0;
		int iTarget = 0;
		int iCaller = 0;
		float flDeadline = 0.0f;
	};

	void FireVote(int iVoteID, int iOption);
	void QueueVote(int iVoteID, int iOption, int iTarget, int iCaller);

	std::vector<VoteEntry_t> m_vecQueue = {};
	std::vector<VoteEntry_t> m_vecFired = {};
};

MAKE_SINGLETON_SCOPED(CAutoVote, AutoVote, F);
