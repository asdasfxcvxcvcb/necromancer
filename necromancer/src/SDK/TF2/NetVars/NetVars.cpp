#include "NetVars.h"

int NetVars::GetOffset(RecvTable *pTable, const char *szNetVar)
{
	if (!pTable || !pTable->m_pProps)
		return 0;

	for (int i = 0; i < pTable->m_nProps; i++)
	{
		RecvProp Prop = pTable->m_pProps[i];

		if (Prop.m_pVarName && HASH_RT(Prop.m_pVarName) == HASH_RT(szNetVar))
			return Prop.GetOffset();

		if (auto DataTable = Prop.GetDataTable())
		{
			if (auto nOffset = GetOffset(DataTable, szNetVar))
				return nOffset + Prop.GetOffset();
		}
	}

	return 0;
}

int NetVars::GetNetVar(const char *szClass, const char *szNetVar)
{
	ClientClass *pClasses = I::BaseClientDLL->GetAllClasses();

	for (auto pCurrNode = pClasses; pCurrNode; pCurrNode = pCurrNode->m_pNext)
	{
		if (HASH_RT(szClass) == HASH_RT(pCurrNode->m_pNetworkName))
			return pCurrNode->m_pRecvTable ? GetOffset(pCurrNode->m_pRecvTable, szNetVar) : 0;
	}

	return 0;
}