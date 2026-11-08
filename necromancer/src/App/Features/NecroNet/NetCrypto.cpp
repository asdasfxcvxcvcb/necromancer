#include "NetCrypto.h"

#pragma comment(lib, "bcrypt.lib")

#include <cwchar>
#include <cstring>
#include <vector>

#ifndef BCRYPT_ECDH_P256_ALGORITHM
#define BCRYPT_ECDH_P256_ALGORITHM L"ECDH_P256"
#endif
#ifndef BCRYPT_ECC_PUBLIC_BLOB
#define BCRYPT_ECC_PUBLIC_BLOB L"ECCPUBLICBLOB"
#endif
#ifndef BCRYPT_ECC_PRIVATE_BLOB
#define BCRYPT_ECC_PRIVATE_BLOB L"ECCPRIVATEBLOB"
#endif

static const char g_szBase64UrlChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static const char g_szHexChars[] = "0123456789abcdef";

static int NetBase64UrlValue(char cCharacter)
{
	if (cCharacter >= 'A' && cCharacter <= 'Z')
		return cCharacter - 'A';
	if (cCharacter >= 'a' && cCharacter <= 'z')
		return cCharacter - 'a' + 26;
	if (cCharacter >= '0' && cCharacter <= '9')
		return cCharacter - '0' + 52;
	if (cCharacter == '-' || cCharacter == '+')
		return 62;
	if (cCharacter == '_' || cCharacter == '/')
		return 63;
	return -1;
}

static bool NetImportAesKey(BCRYPT_ALG_HANDLE hAlgorithm, const std::string& strKey, std::vector<unsigned char>& vecKeyObject, BCRYPT_KEY_HANDLE& hKeyOut)
{
	hKeyOut = nullptr;
	vecKeyObject.clear();
	if (hAlgorithm == nullptr || strKey.size() != static_cast<size_t>(NET_SHARED_KEY_BYTES))
		return false;

	ULONG cbObject = 0;
	ULONG cbResult = 0;
	NTSTATUS st = BCryptGetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&cbObject), sizeof(cbObject), &cbResult, 0);
	if (!BCRYPT_SUCCESS(st) || cbObject == 0)
		return false;

	vecKeyObject.assign(cbObject, 0);
	st = BCryptGenerateSymmetricKey(hAlgorithm, &hKeyOut, vecKeyObject.data(), cbObject, reinterpret_cast<PUCHAR>(const_cast<char*>(strKey.data())), static_cast<ULONG>(strKey.size()), 0);
	if (!BCRYPT_SUCCESS(st))
	{
		hKeyOut = nullptr;
		vecKeyObject.clear();
		return false;
	}
	return true;
}

CNetCrypto::CNetCrypto()
{
}

CNetCrypto::~CNetCrypto()
{
	if (m_hKeyPair != nullptr)
		BCryptDestroyKey(m_hKeyPair);
	if (m_hEcdhAlg != nullptr)
		BCryptCloseAlgorithmProvider(m_hEcdhAlg, 0);
	if (m_hAesAlg != nullptr)
		BCryptCloseAlgorithmProvider(m_hAesAlg, 0);
}

bool CNetCrypto::EnsureAlgorithms()
{
	if (m_hEcdhAlg != nullptr && m_hAesAlg != nullptr)
		return true;

	if (m_hEcdhAlg == nullptr)
	{
		NTSTATUS st = BCryptOpenAlgorithmProvider(&m_hEcdhAlg, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0);
		if (!BCRYPT_SUCCESS(st))
		{
			m_hEcdhAlg = nullptr;
			return false;
		}
	}

	if (m_hAesAlg == nullptr)
	{
		NTSTATUS st = BCryptOpenAlgorithmProvider(&m_hAesAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
		if (!BCRYPT_SUCCESS(st))
		{
			m_hAesAlg = nullptr;
			BCryptCloseAlgorithmProvider(m_hEcdhAlg, 0);
			m_hEcdhAlg = nullptr;
			return false;
		}

		st = BCryptSetProperty(m_hAesAlg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0);
		if (!BCRYPT_SUCCESS(st))
		{
			BCryptCloseAlgorithmProvider(m_hAesAlg, 0);
			m_hAesAlg = nullptr;
			BCryptCloseAlgorithmProvider(m_hEcdhAlg, 0);
			m_hEcdhAlg = nullptr;
			return false;
		}
	}
	return true;
}

bool CNetCrypto::GenerateKeyPair()
{
	if (!EnsureAlgorithms())
		return false;

	if (m_hKeyPair != nullptr)
		BCryptDestroyKey(m_hKeyPair);
	m_hKeyPair = nullptr;
	m_strPublicKeyB64.clear();
	m_strPrivateKeyB64.clear();
	m_mapSharedKeys.clear();

	NTSTATUS st = BCryptGenerateKeyPair(m_hEcdhAlg, &m_hKeyPair, NET_ECDH_KEY_BYTES * 8, 0);
	if (!BCRYPT_SUCCESS(st))
	{
		m_hKeyPair = nullptr;
		return false;
	}

	st = BCryptFinalizeKeyPair(m_hKeyPair, 0);
	if (!BCRYPT_SUCCESS(st))
	{
		BCryptDestroyKey(m_hKeyPair);
		m_hKeyPair = nullptr;
		return false;
	}

	std::string strPoint;
	if (!ExportPublicPoint(strPoint))
	{
		BCryptDestroyKey(m_hKeyPair);
		m_hKeyPair = nullptr;
		return false;
	}
	m_strPublicKeyB64 = Base64UrlEncode(strPoint.data(), strPoint.size());

	std::string strBlob;
	if (!ExportPrivateBlob(strBlob))
	{
		BCryptDestroyKey(m_hKeyPair);
		m_hKeyPair = nullptr;
		m_strPublicKeyB64.clear();
		return false;
	}
	m_strPrivateKeyB64 = Base64UrlEncode(strBlob.data(), strBlob.size());
	return true;
}

bool CNetCrypto::RestoreKeyPair(const std::string& strPrivateB64)
{
	if (!EnsureAlgorithms() || strPrivateB64.empty())
		return false;

	std::string strBlob;
	if (!Base64UrlDecode(strPrivateB64, strBlob) || strBlob.size() != static_cast<size_t>(NET_PRIV_BLOB_BYTES))
		return false;

	BCRYPT_KEY_HANDLE hKey = nullptr;
	NTSTATUS st = BCryptImportKeyPair(m_hEcdhAlg, nullptr, BCRYPT_ECC_PRIVATE_BLOB, &hKey, reinterpret_cast<PUCHAR>(const_cast<char*>(strBlob.data())), static_cast<ULONG>(strBlob.size()), 0);
	if (!BCRYPT_SUCCESS(st))
		return false;

	std::string strPoint;
	strPoint.reserve(NET_PUB_POINT_BYTES);
	strPoint += static_cast<char>(0x04);
	ULONG cbBlob = 0;
	st = BCryptExportKey(hKey, nullptr, BCRYPT_ECC_PUBLIC_BLOB, nullptr, 0, &cbBlob, 0);
	if (BCRYPT_SUCCESS(st) && cbBlob == static_cast<ULONG>(8 + NET_ECDH_KEY_BYTES * 2))
	{
		std::string strExported;
		strExported.resize(cbBlob);
		st = BCryptExportKey(hKey, nullptr, BCRYPT_ECC_PUBLIC_BLOB, reinterpret_cast<PUCHAR>(strExported.data()), cbBlob, &cbBlob, 0);
		if (BCRYPT_SUCCESS(st) && cbBlob == static_cast<ULONG>(8 + NET_ECDH_KEY_BYTES * 2))
			strPoint.append(strExported, 8, strExported.size() - 8);
	}
	if (strPoint.size() != static_cast<size_t>(NET_PUB_POINT_BYTES))
	{
		BCryptDestroyKey(hKey);
		return false;
	}

	if (m_hKeyPair != nullptr)
		BCryptDestroyKey(m_hKeyPair);
	m_hKeyPair = hKey;
	m_strPublicKeyB64 = Base64UrlEncode(strPoint.data(), strPoint.size());
	m_strPrivateKeyB64 = strPrivateB64;
	m_mapSharedKeys.clear();
	return true;
}

bool CNetCrypto::HasKeyPair() const
{
	return m_hKeyPair != nullptr;
}

std::string CNetCrypto::GetPublicKeyB64() const
{
	return m_strPublicKeyB64;
}

std::string CNetCrypto::GetPrivateKeyB64() const
{
	return m_strPrivateKeyB64;
}

bool CNetCrypto::ExportPublicPoint(std::string& strPointOut) const
{
	strPointOut.clear();
	if (m_hKeyPair == nullptr)
		return false;

	ULONG cbBlob = 0;
	NTSTATUS st = BCryptExportKey(m_hKeyPair, nullptr, BCRYPT_ECC_PUBLIC_BLOB, nullptr, 0, &cbBlob, 0);
	if (!BCRYPT_SUCCESS(st) || cbBlob != static_cast<ULONG>(8 + NET_ECDH_KEY_BYTES * 2))
		return false;

	std::string strBlob;
	strBlob.resize(cbBlob);
	st = BCryptExportKey(m_hKeyPair, nullptr, BCRYPT_ECC_PUBLIC_BLOB, reinterpret_cast<PUCHAR>(strBlob.data()), cbBlob, &cbBlob, 0);
	if (!BCRYPT_SUCCESS(st) || cbBlob != static_cast<ULONG>(8 + NET_ECDH_KEY_BYTES * 2))
		return false;

	strPointOut.reserve(NET_PUB_POINT_BYTES);
	strPointOut += static_cast<char>(0x04);
	strPointOut.append(strBlob, 8, strBlob.size() - 8);
	return true;
}

bool CNetCrypto::ExportPrivateBlob(std::string& strBlobOut) const
{
	strBlobOut.clear();
	if (m_hKeyPair == nullptr)
		return false;

	ULONG cbBlob = 0;
	NTSTATUS st = BCryptExportKey(m_hKeyPair, nullptr, BCRYPT_ECC_PRIVATE_BLOB, nullptr, 0, &cbBlob, 0);
	if (!BCRYPT_SUCCESS(st) || cbBlob != static_cast<ULONG>(NET_PRIV_BLOB_BYTES))
		return false;

	strBlobOut.resize(cbBlob);
	st = BCryptExportKey(m_hKeyPair, nullptr, BCRYPT_ECC_PRIVATE_BLOB, reinterpret_cast<PUCHAR>(strBlobOut.data()), cbBlob, &cbBlob, 0);
	if (!BCRYPT_SUCCESS(st) || cbBlob != static_cast<ULONG>(NET_PRIV_BLOB_BYTES))
	{
		strBlobOut.clear();
		return false;
	}
	return true;
}

bool CNetCrypto::ImportPeerPoint(const std::string& strPoint, BCRYPT_KEY_HANDLE& hKeyOut) const
{
	hKeyOut = nullptr;
	if (m_hEcdhAlg == nullptr || strPoint.size() != static_cast<size_t>(NET_PUB_POINT_BYTES) || static_cast<unsigned char>(strPoint[0]) != 0x04)
		return false;

	std::string strBlob;
	strBlob.resize(8 + NET_ECDH_KEY_BYTES * 2);
	ULONG ulMagic = BCRYPT_ECDH_PUBLIC_P256_MAGIC;
	ULONG cbKey = NET_ECDH_KEY_BYTES;
	memcpy(strBlob.data(), &ulMagic, sizeof(ULONG));
	memcpy(strBlob.data() + 4, &cbKey, sizeof(ULONG));
	memcpy(strBlob.data() + 8, strPoint.data() + 1, NET_ECDH_KEY_BYTES * 2);

	NTSTATUS st = BCryptImportKeyPair(m_hEcdhAlg, nullptr, BCRYPT_ECC_PUBLIC_BLOB, &hKeyOut, reinterpret_cast<PUCHAR>(const_cast<char*>(strBlob.data())), static_cast<ULONG>(strBlob.size()), 0);
	if (!BCRYPT_SUCCESS(st))
	{
		hKeyOut = nullptr;
		return false;
	}
	return true;
}

bool CNetCrypto::DeriveKey(const std::string& strUsername, const std::string& strPeerPublicB64)
{
	if (!EnsureAlgorithms() || m_hKeyPair == nullptr || strUsername.empty())
		return false;

	std::string strPoint;
	if (!Base64UrlDecode(strPeerPublicB64, strPoint))
		return false;

	BCRYPT_KEY_HANDLE hPeer = nullptr;
	if (!ImportPeerPoint(strPoint, hPeer))
		return false;

	BCRYPT_SECRET_HANDLE hSecret = nullptr;
	NTSTATUS st = BCryptSecretAgreement(m_hKeyPair, hPeer, &hSecret, 0);
	if (!BCRYPT_SUCCESS(st))
	{
		BCryptDestroyKey(hPeer);
		return false;
	}

	BCryptBuffer kdfParam = {};
	kdfParam.cbBuffer = static_cast<ULONG>((wcslen(BCRYPT_SHA256_ALGORITHM) + 1) * sizeof(wchar_t));
	kdfParam.BufferType = KDF_HASH_ALGORITHM;
	kdfParam.pvBuffer = const_cast<wchar_t*>(BCRYPT_SHA256_ALGORITHM);

	BCryptBufferDesc kdfDesc = {};
	kdfDesc.ulVersion = BCRYPTBUFFER_VERSION;
	kdfDesc.cBuffers = 1;
	kdfDesc.pBuffers = &kdfParam;

	ULONG cbDerived = 0;
	st = BCryptDeriveKey(hSecret, BCRYPT_KDF_HASH, &kdfDesc, nullptr, 0, &cbDerived, 0);
	if (!BCRYPT_SUCCESS(st) || cbDerived != static_cast<ULONG>(NET_SHARED_KEY_BYTES))
	{
		BCryptDestroySecret(hSecret);
		BCryptDestroyKey(hPeer);
		return false;
	}

	std::string strKey;
	strKey.resize(cbDerived);
	st = BCryptDeriveKey(hSecret, BCRYPT_KDF_HASH, &kdfDesc, reinterpret_cast<PUCHAR>(strKey.data()), cbDerived, &cbDerived, 0);
	BCryptDestroySecret(hSecret);
	BCryptDestroyKey(hPeer);
	if (!BCRYPT_SUCCESS(st) || cbDerived != static_cast<ULONG>(NET_SHARED_KEY_BYTES))
		return false;

	m_mapSharedKeys[strUsername] = strKey;
	return true;
}

bool CNetCrypto::HasKey(const std::string& strUsername) const
{
	return m_mapSharedKeys.find(strUsername) != m_mapSharedKeys.end();
}

void CNetCrypto::DropDerivedKeys()
{
	m_mapSharedKeys.clear();
}

bool CNetCrypto::DropKey(const std::string& strUsername)
{
	return m_mapSharedKeys.erase(strUsername) > 0;
}

bool CNetCrypto::EncryptMessage(const std::string& strUsername, const std::string& strPlaintext, std::string& strOutNonceB64, std::string& strOutCtB64)
{
	strOutNonceB64.clear();
	strOutCtB64.clear();
	if (!EnsureAlgorithms() || strPlaintext.size() > static_cast<size_t>(ULONG_MAX))
		return false;

	auto it = m_mapSharedKeys.find(strUsername);
	if (it == m_mapSharedKeys.end() || it->second.size() != static_cast<size_t>(NET_SHARED_KEY_BYTES))
		return false;

	std::string strNonce = RandomBytes(NET_GCM_NONCE_BYTES);
	if (strNonce.size() != static_cast<size_t>(NET_GCM_NONCE_BYTES))
		return false;

	std::vector<unsigned char> vecKeyObject;
	BCRYPT_KEY_HANDLE hKey = nullptr;
	if (!NetImportAesKey(m_hAesAlg, it->second, vecKeyObject, hKey))
		return false;

	std::string strCiphertext;
	strCiphertext.resize(strPlaintext.size());
	unsigned char rgTag[NET_GCM_TAG_BYTES] = {};

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(strNonce.data()));
	authInfo.cbNonce = NET_GCM_NONCE_BYTES;
	authInfo.pbTag = rgTag;
	authInfo.cbTag = NET_GCM_TAG_BYTES;

	ULONG cbResult = 0;
	NTSTATUS st = BCryptEncrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char*>(strPlaintext.data())), static_cast<ULONG>(strPlaintext.size()), &authInfo, nullptr, 0, reinterpret_cast<PUCHAR>(strCiphertext.data()), static_cast<ULONG>(strCiphertext.size()), &cbResult, 0);
	BCryptDestroyKey(hKey);
	if (!BCRYPT_SUCCESS(st) || cbResult != static_cast<ULONG>(strCiphertext.size()))
		return false;

	strCiphertext.append(reinterpret_cast<const char*>(rgTag), NET_GCM_TAG_BYTES);
	strOutNonceB64 = Base64UrlEncode(strNonce.data(), strNonce.size());
	strOutCtB64 = Base64UrlEncode(strCiphertext.data(), strCiphertext.size());
	return true;
}

bool CNetCrypto::DecryptMessage(const std::string& strUsername, const std::string& strNonceB64, const std::string& strCtB64, std::string& strOutPlaintext)
{
	strOutPlaintext.clear();
	if (!EnsureAlgorithms())
		return false;

	auto it = m_mapSharedKeys.find(strUsername);
	if (it == m_mapSharedKeys.end() || it->second.size() != static_cast<size_t>(NET_SHARED_KEY_BYTES))
		return false;

	std::string strNonce;
	if (!Base64UrlDecode(strNonceB64, strNonce) || strNonce.size() != static_cast<size_t>(NET_GCM_NONCE_BYTES))
		return false;

	std::string strCiphertext;
	if (!Base64UrlDecode(strCtB64, strCiphertext) || strCiphertext.size() < static_cast<size_t>(NET_GCM_TAG_BYTES))
		return false;

	const size_t nBodySize = strCiphertext.size() - NET_GCM_TAG_BYTES;
	unsigned char rgTag[NET_GCM_TAG_BYTES];
	memcpy(rgTag, strCiphertext.data() + nBodySize, NET_GCM_TAG_BYTES);

	std::vector<unsigned char> vecKeyObject;
	BCRYPT_KEY_HANDLE hKey = nullptr;
	if (!NetImportAesKey(m_hAesAlg, it->second, vecKeyObject, hKey))
		return false;

	std::string strPlaintext;
	strPlaintext.resize(nBodySize);

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(strNonce.data()));
	authInfo.cbNonce = NET_GCM_NONCE_BYTES;
	authInfo.pbTag = rgTag;
	authInfo.cbTag = NET_GCM_TAG_BYTES;

	ULONG cbResult = 0;
	NTSTATUS st = BCryptDecrypt(hKey, reinterpret_cast<PUCHAR>(const_cast<char*>(strCiphertext.data())), static_cast<ULONG>(nBodySize), &authInfo, nullptr, 0, reinterpret_cast<PUCHAR>(strPlaintext.data()), static_cast<ULONG>(nBodySize), &cbResult, 0);
	BCryptDestroyKey(hKey);
	if (!BCRYPT_SUCCESS(st) || cbResult > static_cast<ULONG>(nBodySize))
		return false;

	strPlaintext.resize(cbResult);
	strOutPlaintext = std::move(strPlaintext);
	return true;
}

std::string CNetCrypto::Base64UrlEncode(const void* pData, size_t nSize)
{
	std::string strOut;
	if (pData == nullptr || nSize == 0)
		return strOut;

	const unsigned char* pBytes = static_cast<const unsigned char*>(pData);
	strOut.reserve(((nSize + 2) / 3) * 4);
	size_t i = 0;
	while (i + 3 <= nSize)
	{
		unsigned int nTriple = (static_cast<unsigned int>(pBytes[i]) << 16) | (static_cast<unsigned int>(pBytes[i + 1]) << 8) | static_cast<unsigned int>(pBytes[i + 2]);
		strOut += g_szBase64UrlChars[(nTriple >> 18) & 0x3F];
		strOut += g_szBase64UrlChars[(nTriple >> 12) & 0x3F];
		strOut += g_szBase64UrlChars[(nTriple >> 6) & 0x3F];
		strOut += g_szBase64UrlChars[nTriple & 0x3F];
		i += 3;
	}
	if (i + 1 == nSize)
	{
		unsigned int nPair = static_cast<unsigned int>(pBytes[i]) << 16;
		strOut += g_szBase64UrlChars[(nPair >> 18) & 0x3F];
		strOut += g_szBase64UrlChars[(nPair >> 12) & 0x3F];
	}
	else if (i + 2 == nSize)
	{
		unsigned int nTriple = (static_cast<unsigned int>(pBytes[i]) << 16) | (static_cast<unsigned int>(pBytes[i + 1]) << 8);
		strOut += g_szBase64UrlChars[(nTriple >> 18) & 0x3F];
		strOut += g_szBase64UrlChars[(nTriple >> 12) & 0x3F];
		strOut += g_szBase64UrlChars[(nTriple >> 6) & 0x3F];
	}
	return strOut;
}

std::string CNetCrypto::Base64UrlEncode(const std::string& strData)
{
	return Base64UrlEncode(strData.data(), strData.size());
}

bool CNetCrypto::Base64UrlDecode(const std::string& strText, std::string& strOut)
{
	strOut.clear();
	strOut.reserve((strText.size() / 4) * 3 + 3);
	unsigned int nAccumulator = 0;
	int nBits = 0;
	for (char cCharacter : strText)
	{
		if (cCharacter == '=' || cCharacter == ' ' || cCharacter == '\t' || cCharacter == '\r' || cCharacter == '\n')
			continue;
		int nValue = NetBase64UrlValue(cCharacter);
		if (nValue < 0)
		{
			strOut.clear();
			return false;
		}
		nAccumulator = (nAccumulator << 6) | static_cast<unsigned int>(nValue);
		nBits += 6;
		if (nBits >= 8)
		{
			nBits -= 8;
			strOut += static_cast<char>((nAccumulator >> nBits) & 0xFF);
		}
	}
	if (nBits >= 6)
	{
		strOut.clear();
		return false;
	}
	return true;
}

std::string CNetCrypto::RandomBytes(size_t nCount)
{
	std::string strOut;
	if (nCount == 0 || nCount > static_cast<size_t>(ULONG_MAX))
		return strOut;

	strOut.resize(nCount);
	NTSTATUS st = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(strOut.data()), static_cast<ULONG>(nCount), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (!BCRYPT_SUCCESS(st))
	{
		strOut.clear();
		return strOut;
	}
	return strOut;
}

std::string CNetCrypto::RandomHex(size_t nBytes)
{
	std::string strBytes = RandomBytes(nBytes);
	std::string strOut;
	strOut.reserve(strBytes.size() * 2);
	for (char cByte : strBytes)
	{
		unsigned char cValue = static_cast<unsigned char>(cByte);
		strOut += g_szHexChars[cValue >> 4];
		strOut += g_szHexChars[cValue & 0x0F];
	}
	return strOut;
}
