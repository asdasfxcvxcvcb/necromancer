#include "AutoQueue.h"

#include "../CFG.h"

void CAutoQueue::Run()
{
	if (!CFG::Misc_Auto_Queue)
		return;

	// Check if we're not already in queue for casual
	if (I::TFPartyClient && !I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default))
	{
		static bool bHasLoaded = false;
		if (!bHasLoaded)
		{
			// Load saved casual criteria (map selection, etc.)
			I::TFPartyClient->LoadSavedCasualCriteria();
			bHasLoaded = true;
		}

		// Request to queue for casual match
		I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
	}
}
