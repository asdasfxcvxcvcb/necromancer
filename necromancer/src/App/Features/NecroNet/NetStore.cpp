#include "NetStore.h"

#include "NetCrypto.h"

#include <windows.h>
#include <wincrypt.h>
#include <aclapi.h>

#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

static constexpr long long NET_HISTORY_MAX_FILE_BYTES = 8 * 1024 * 1024;
static constexpr long long NET_HISTORY_TAIL_BYTES = 512 * 1024;

static bool IsLegacyIdName(const std::string& strName)
{
	if (strName.size() != 17)
		return false;

	for (const char cChar : strName)
	{
		if (cChar < '0' || cChar > '9')
			return false;
	}

	return true;
}

static void SweepLegacyHistoryFiles(const std::string& strFolder)
{
	if (strFolder.empty())
		return;

	WIN32_FIND_DATAA wfd{};
	const std::string strPattern = strFolder + "\\*";
	HANDLE hFind = FindFirstFileA(strPattern.c_str(), &wfd);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		const std::string strFile = wfd.cFileName;
		const size_t nDot = strFile.rfind('.');
		if (nDot == std::string::npos)
			continue;

		const std::string strExt = strFile.substr(nDot);
		if (strExt != ".jsonl" && strExt != ".jsonl.old")
			continue;

		if (!IsLegacyIdName(strFile.substr(0, nDot)))
			continue;

		DeleteFileA((strFolder + "\\" + strFile).c_str());
	} while (FindNextFileA(hFind, &wfd));

	FindClose(hFind);
}

static std::string ProtectString(const std::string& strPlain)
{
	if (strPlain.empty())
		return std::string();

	DATA_BLOB blobIn{};
	blobIn.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(strPlain.data()));
	blobIn.cbData = static_cast<DWORD>(strPlain.size());

	DATA_BLOB blobOut{};
	if (!CryptProtectData(&blobIn, L"necromancer-net", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &blobOut))
		return std::string();

	const std::string strEncoded = CNetCrypto::Base64UrlEncode(blobOut.pbData, blobOut.cbData);
	LocalFree(blobOut.pbData);
	return "dpapi:" + strEncoded;
}

static std::string UnprotectString(const std::string& strStored)
{
	if (strStored.rfind("dpapi:", 0) != 0)
		return strStored;

	std::string strDecoded;
	if (!CNetCrypto::Base64UrlDecode(strStored.substr(6), strDecoded) || strDecoded.empty())
		return std::string();

	DATA_BLOB blobIn{};
	blobIn.pbData = reinterpret_cast<BYTE*>(strDecoded.data());
	blobIn.cbData = static_cast<DWORD>(strDecoded.size());

	DATA_BLOB blobOut{};
	if (!CryptUnprotectData(&blobIn, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &blobOut))
		return std::string();

	std::string strPlain(reinterpret_cast<const char*>(blobOut.pbData), blobOut.cbData);
	LocalFree(blobOut.pbData);
	return strPlain;
}

static void RestrictFolderDACL(const std::string& strPath)
{
	HANDLE hToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
		return;

	DWORD dwNeeded = 0;
	GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwNeeded);
	std::vector<char> vecUser(dwNeeded);
	if (!dwNeeded || !GetTokenInformation(hToken, TokenUser, vecUser.data(), dwNeeded, &dwNeeded))
	{
		CloseHandle(hToken);
		return;
	}
	CloseHandle(hToken);

	PSID pUserSid = reinterpret_cast<TOKEN_USER*>(vecUser.data())->User.Sid;

	SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
	PSID pAdminsSid = nullptr;
	PSID pSystemSid = nullptr;
	if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminsSid))
		return;
	if (!AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pSystemSid))
	{
		FreeSid(pAdminsSid);
		return;
	}

	EXPLICIT_ACCESSA rgAccess[3] = {};
	rgAccess[0].grfAccessPermissions = GENERIC_ALL;
	rgAccess[0].grfAccessMode = GRANT_ACCESS;
	rgAccess[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	rgAccess[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
	rgAccess[0].Trustee.ptstrName = reinterpret_cast<LPCH>(pUserSid);

	rgAccess[1].grfAccessPermissions = GENERIC_ALL;
	rgAccess[1].grfAccessMode = GRANT_ACCESS;
	rgAccess[1].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	rgAccess[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
	rgAccess[1].Trustee.ptstrName = reinterpret_cast<LPCH>(pAdminsSid);

	rgAccess[2].grfAccessPermissions = GENERIC_ALL;
	rgAccess[2].grfAccessMode = GRANT_ACCESS;
	rgAccess[2].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	rgAccess[2].Trustee.TrusteeType = TRUSTEE_IS_USER;
	rgAccess[2].Trustee.ptstrName = reinterpret_cast<LPCH>(pSystemSid);

	PACL pAcl = nullptr;
	if (SetEntriesInAclA(3, rgAccess, nullptr, &pAcl) == ERROR_SUCCESS && pAcl)
	{
		SetNamedSecurityInfoA(const_cast<LPSTR>(strPath.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, pAcl, nullptr);
		LocalFree(pAcl);
	}

	FreeSid(pSystemSid);
	FreeSid(pAdminsSid);
}

bool CNetStore::Init(const std::string& strWorkFolder)
{
	std::string strBase = strWorkFolder;
	while (!strBase.empty() && (strBase.back() == '\\' || strBase.back() == '/'))
		strBase.pop_back();

	if (strBase.empty())
	{
		m_strNetFolder.clear();
		m_strHistoryFolder.clear();
		return false;
	}

	m_strNetFolder = strBase + "\\net";
	m_strHistoryFolder = m_strNetFolder + "\\history";

	if (!EnsureFolder(m_strNetFolder))
		return false;

	if (!EnsureFolder(m_strHistoryFolder))
		return false;

	SweepLegacyHistoryFiles(m_strHistoryFolder);

	RestrictFolderDACL(m_strNetFolder);

	return true;
}

const std::string& CNetStore::GetNetFolder() const
{
	return m_strNetFolder;
}

const std::string& CNetStore::GetHistoryFolder() const
{
	return m_strHistoryFolder;
}

bool CNetStore::LoadProfile(NetProfile& out) const
{
	out = NetProfile{};

	if (m_strNetFolder.empty())
		return false;

	std::string strData;
	if (!ReadTextFile(GetProfilePath(), strData))
		return false;

	bool bOk = false;
	const CJsonValue jRoot = NetJson::Parse(strData, &bOk);
	if (!bOk || !jRoot.IsObject())
		return false;

	const CJsonValue* pToken = jRoot.Find("token");
	const CJsonValue* pSteamId = jRoot.Find("steamid");
	const CJsonValue* pUsername = jRoot.Find("username");
	const CJsonValue* pName = jRoot.Find("name");
	const CJsonValue* pPubkey = jRoot.Find("pubkey");
	const CJsonValue* pPrivKey = jRoot.Find("privkey");

	if (!pToken || !pSteamId || !pPubkey || !pPrivKey)
		return false;

	const std::string strStoredToken = pToken->GetString();
	const std::string strStoredPrivKey = pPrivKey->GetString();

	out.m_strToken = UnprotectString(strStoredToken);
	out.m_strSteamId = pSteamId->GetString();
	out.m_strUsername = pUsername ? pUsername->GetString() : std::string();
	out.m_strName = pName ? pName->GetString() : std::string();
	out.m_strPubkey = pPubkey->GetString();
	out.m_strPrivKey = UnprotectString(strStoredPrivKey);

	const bool bLoaded = !out.m_strToken.empty() && !out.m_strSteamId.empty() && !out.m_strPubkey.empty() && !out.m_strPrivKey.empty();
	if (bLoaded && (strStoredToken.rfind("dpapi:", 0) != 0 || strStoredPrivKey.rfind("dpapi:", 0) != 0))
		SaveProfile(out);

	return bLoaded;
}

bool CNetStore::SaveProfile(const NetProfile& profile) const
{
	if (!EnsureFolder(m_strNetFolder))
		return false;

	const CJsonValue jRoot = NetJson::MakeObject({
		{ "v", 2 },
		{ "token", ProtectString(profile.m_strToken) },
		{ "steamid", profile.m_strSteamId },
		{ "username", profile.m_strUsername },
		{ "name", profile.m_strName },
		{ "pubkey", profile.m_strPubkey },
		{ "privkey", ProtectString(profile.m_strPrivKey) } });

	const std::string strFinal = GetProfilePath();
	const std::string strTemp = strFinal + ".tmp";

	std::ofstream stream(strTemp.c_str(), std::ios::binary | std::ios::trunc);
	if (!stream.is_open())
		return false;

	stream << jRoot.Dump();
	stream.close();

	if (stream.fail())
	{
		DeleteFileA(strTemp.c_str());
		return false;
	}

	HANDLE hFile = CreateFileA(strTemp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		DeleteFileA(strTemp.c_str());
		return false;
	}

	const bool bFlushed = FlushFileBuffers(hFile) != FALSE;
	CloseHandle(hFile);

	if (!bFlushed)
	{
		DeleteFileA(strTemp.c_str());
		return false;
	}

	if (!MoveFileExA(strTemp.c_str(), strFinal.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileA(strTemp.c_str());
		return false;
	}

	return true;
}

void CNetStore::ClearProfile() const
{
	if (m_strNetFolder.empty())
		return;

	const std::string strTemp = GetProfilePath() + ".tmp";
	DeleteFileA(strTemp.c_str());
	DeleteFileA(GetProfilePath().c_str());
}

bool CNetStore::AppendHistory(const std::string& strUsername, const NetHistoryLine& line) const
{
	if (!IsValidUsername(strUsername))
		return false;

	if (!EnsureFolder(m_strHistoryFolder))
		return false;

	const CJsonValue jLine = NetJson::MakeObject({
		{ "dir", line.m_bOut ? "out" : "in" },
		{ "from", line.m_strFrom },
		{ "text", line.m_strText },
		{ "ts", line.m_llTs } });

	return AppendTextFile(GetHistoryPath(strUsername), jLine.Dump());
}

bool CNetStore::LoadHistory(const std::string& strUsername, std::vector<NetHistoryLine>& out) const
{
	out.clear();

	if (!IsValidUsername(strUsername) || m_strHistoryFolder.empty())
		return false;

	std::string strData;
	bool bTruncated = false;
	if (!ReadTextFileTail(GetHistoryPath(strUsername), strData, bTruncated))
		return false;

	if (bTruncated)
	{
		const size_t nFirstNewline = strData.find('\n');
		if (nFirstNewline != std::string::npos)
			strData.erase(0, nFirstNewline + 1);
		else
			strData.clear();
	}

	std::istringstream stream(strData);
	std::string strLine;
	while (std::getline(stream, strLine))
	{
		if (!strLine.empty() && strLine.back() == '\r')
			strLine.pop_back();

		if (strLine.empty())
			continue;

		bool bOk = false;
		const CJsonValue jLine = NetJson::Parse(strLine, &bOk);
		if (!bOk || !jLine.IsObject())
			continue;

		NetHistoryLine historyLine;
		const CJsonValue* pDir = jLine.Find("dir");
		const CJsonValue* pFrom = jLine.Find("from");
		const CJsonValue* pText = jLine.Find("text");
		const CJsonValue* pTs = jLine.Find("ts");

		if (pDir)
			historyLine.m_bOut = pDir->GetString() == "out";
		if (pFrom)
			historyLine.m_strFrom = pFrom->GetString();
		if (pText)
			historyLine.m_strText = pText->GetString();
		if (pTs)
			historyLine.m_llTs = pTs->GetInt();

		out.push_back(std::move(historyLine));
	}

	return true;
}

bool CNetStore::IsValidUsername(const std::string& strUsername)
{
	if (strUsername.size() < NET_USERNAME_MIN || strUsername.size() > NET_USERNAME_MAX)
		return false;

	for (const char cChar : strUsername)
	{
		const bool bValid = (cChar >= 'A' && cChar <= 'Z') || (cChar >= 'a' && cChar <= 'z') || (cChar >= '0' && cChar <= '9') || cChar == '_';
		if (!bValid)
			return false;
	}

	return true;
}

bool CNetStore::EnsureFolder(const std::string& strPath)
{
	if (strPath.empty())
		return false;

	std::string strBuilt;
	size_t nPos = 0;
	while (nPos <= strPath.size())
	{
		size_t nEnd = strPath.find_first_of("\\/", nPos);
		if (nEnd == std::string::npos)
			nEnd = strPath.size();

		const std::string strPart = strPath.substr(nPos, nEnd - nPos);
		nPos = nEnd + 1;

		if (strPart.empty())
			continue;

		if (!strBuilt.empty())
			strBuilt += "\\";
		strBuilt += strPart;

		if (strBuilt.size() == 2 && strBuilt[1] == ':')
			continue;

		if (!CreateDirectoryA(strBuilt.c_str(), nullptr))
		{
			if (GetLastError() != ERROR_ALREADY_EXISTS)
				return false;
		}
	}

	return true;
}

bool CNetStore::ReadTextFile(const std::string& strPath, std::string& strOut)
{
	std::ifstream stream(strPath.c_str(), std::ios::binary);
	if (!stream.is_open())
		return false;

	std::string strData((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	const bool bOk = !stream.bad();
	stream.close();

	if (!bOk)
		return false;

	strOut = std::move(strData);
	return true;
}

bool CNetStore::ReadTextFileTail(const std::string& strPath, std::string& strOut, bool& bTruncated)
{
	bTruncated = false;

	HANDLE hFile = CreateFileA(strPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER liSize;
	if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart <= 0)
	{
		CloseHandle(hFile);
		return false;
	}

	bTruncated = liSize.QuadPart > NET_HISTORY_TAIL_BYTES;
	LARGE_INTEGER liOffset;
	liOffset.QuadPart = bTruncated ? liSize.QuadPart - NET_HISTORY_TAIL_BYTES : 0;
	if (!SetFilePointerEx(hFile, liOffset, nullptr, FILE_BEGIN))
	{
		CloseHandle(hFile);
		return false;
	}

	const DWORD dwToRead = static_cast<DWORD>(liSize.QuadPart - liOffset.QuadPart);
	std::string strData(dwToRead, '\0');
	DWORD dwRead = 0;
	const bool bOk = ReadFile(hFile, strData.data(), dwToRead, &dwRead, nullptr) && dwRead > 0;
	CloseHandle(hFile);

	if (!bOk)
		return false;

	strData.resize(dwRead);
	strOut = std::move(strData);
	return true;
}

bool CNetStore::AppendTextFile(const std::string& strPath, const std::string& strLine)
{
	bool bNeedsNewline = false;

	HANDLE hCheck = CreateFileA(strPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hCheck != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER liSize;
		if (GetFileSizeEx(hCheck, &liSize) && liSize.QuadPart > 0)
		{
			if (liSize.QuadPart > NET_HISTORY_MAX_FILE_BYTES)
			{
				CloseHandle(hCheck);
				const std::string strOld = strPath + ".old";
				DeleteFileA(strOld.c_str());
				MoveFileExA(strPath.c_str(), strOld.c_str(), MOVEFILE_REPLACE_EXISTING);
				return AppendTextFile(strPath, strLine);
			}

			LARGE_INTEGER liPos;
			liPos.QuadPart = -1;
			char chLast = 0;
			DWORD dwRead = 0;
			if (SetFilePointerEx(hCheck, liPos, nullptr, FILE_END) && ReadFile(hCheck, &chLast, 1, &dwRead, nullptr) && dwRead == 1 && chLast != '\n')
				bNeedsNewline = true;
		}
		CloseHandle(hCheck);
	}

	std::ofstream stream(strPath.c_str(), std::ios::binary | std::ios::app);
	if (!stream.is_open())
		return false;

	if (bNeedsNewline)
		stream << '\n';

	stream << strLine << '\n';
	stream.close();
	return !stream.fail();
}

std::string CNetStore::GetProfilePath() const
{
	return m_strNetFolder + "\\profile.json";
}

std::string CNetStore::GetHistoryPath(const std::string& strUsername) const
{
	return m_strHistoryFolder + "\\" + strUsername + ".jsonl";
}
