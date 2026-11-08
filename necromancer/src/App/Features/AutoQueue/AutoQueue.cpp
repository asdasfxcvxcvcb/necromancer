#include "AutoQueue.h"

#include "../CFG.h"

void CAutoQueue::Run()
{
	if (!CFG::Misc_Auto_Queue)
		return;

	// Check if we're not already in queue for casual
	if (I::TFPartyClient && !I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default))
	{
		// The old one-shot LoadSavedCasualCriteria could fire before the party
		// client had its data (fresh session) and silently do nothing, which
		// left auto queue dead until the user queued manually once. Amalgam
		// re-loads whenever no maps are selected; we retry on a short cooldown
		// so toggling it on always converges to an actual queue.
		static float flNextCriteriaLoad = 0.0f;
		const float flRealTime = I::GlobalVars->realtime;
		if (flRealTime >= flNextCriteriaLoad)
		{
			I::TFPartyClient->LoadSavedCasualCriteria();
			flNextCriteriaLoad = flRealTime + 5.0f;
		}

		// Request to queue for casual match (throttled - the GC round-trip to
		// register the queue isn't instant and spamming the request every
		// frame while it pends accomplishes nothing)
		static float flNextQueueRequest = 0.0f;
		if (flRealTime >= flNextQueueRequest)
		{
			I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
			flNextQueueRequest = flRealTime + 1.0f;
		}
	}
}
