#pragma once
#include <cstdint>

#include "../Singleton/Singleton.h"
#include <vector>

class CSignature
{
private:
	std::uintptr_t m_dwVal = 0x0;
	const char *m_pszDLLName = {};
	const char *m_pszSignature = {};
	int m_nOffset = 0;
	const char *m_pszName = {};

public:
	CSignature(const char *sDLLName, const char *sSignature, int nOffset, const char *sName);

	void Initialize();

	inline std::uintptr_t Get()
	{
		return m_dwVal;
	}

	inline std::uintptr_t operator()()
	{
		return m_dwVal;
	}

	template <typename T, typename... Args>
	inline T Call(Args... args) const
	{
		return reinterpret_cast<T(__fastcall*)(Args...)>(m_dwVal)(args...);
	}
};

#define MAKE_SIGNATURE(name, dll, sig, offset) namespace Signatures { inline CSignature name(dll, sig, offset, #name); }

class CSignatureManager
{
private:
	std::vector<CSignature *> m_vecSignatures = {};

public:
	void InitializeAllSignatures();

	inline void AddSignature(CSignature *pSignature)
	{
		m_vecSignatures.push_back(pSignature);
	}
};

MAKE_SINGLETON_SCOPED(CSignatureManager, SignatureManager, U);