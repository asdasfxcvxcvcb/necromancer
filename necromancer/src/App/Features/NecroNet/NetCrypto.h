#pragma once

#include <windows.h>
#include <bcrypt.h>

#include <cstddef>
#include <map>
#include <string>

#pragma comment(lib, "bcrypt.lib")

inline constexpr int NET_ECDH_KEY_BYTES = 32;
inline constexpr int NET_PUB_POINT_BYTES = 65;
inline constexpr int NET_PRIV_BLOB_BYTES = 104;
inline constexpr int NET_SHARED_KEY_BYTES = 32;
inline constexpr int NET_GCM_NONCE_BYTES = 12;
inline constexpr int NET_GCM_TAG_BYTES = 16;

class CNetCrypto
{
public:
	CNetCrypto();
	~CNetCrypto();

	CNetCrypto(const CNetCrypto&) = delete;
	CNetCrypto& operator=(const CNetCrypto&) = delete;

	bool GenerateKeyPair();
	bool RestoreKeyPair(const std::string& strPrivateB64);
	bool HasKeyPair() const;
	std::string GetPublicKeyB64() const;
	std::string GetPrivateKeyB64() const;

	bool DeriveKey(const std::string& strUsername, const std::string& strPeerPublicB64);
	bool HasKey(const std::string& strUsername) const;
	bool DropKey(const std::string& strUsername);
	void DropDerivedKeys();

	bool EncryptMessage(const std::string& strUsername, const std::string& strPlaintext, std::string& strOutNonceB64, std::string& strOutCtB64);
	bool DecryptMessage(const std::string& strUsername, const std::string& strNonceB64, const std::string& strCtB64, std::string& strOutPlaintext);

	static std::string Base64UrlEncode(const void* pData, size_t nSize);
	static std::string Base64UrlEncode(const std::string& strData);
	static bool Base64UrlDecode(const std::string& strText, std::string& strOut);
	static std::string RandomBytes(size_t nCount);
	static std::string RandomHex(size_t nBytes);

private:
	bool EnsureAlgorithms();
	bool ExportPublicPoint(std::string& strPointOut) const;
	bool ExportPrivateBlob(std::string& strBlobOut) const;
	bool ImportPeerPoint(const std::string& strPoint, BCRYPT_KEY_HANDLE& hKeyOut) const;

	BCRYPT_ALG_HANDLE m_hEcdhAlg = nullptr;
	BCRYPT_ALG_HANDLE m_hAesAlg = nullptr;
	BCRYPT_KEY_HANDLE m_hKeyPair = nullptr;
	std::string m_strPublicKeyB64;
	std::string m_strPrivateKeyB64;
	std::map<std::string, std::string> m_mapSharedKeys;
};
