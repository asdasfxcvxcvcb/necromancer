#pragma once

#include <string>
#include <vector>

#include "NetworkShared.h"
#include "NetJson.h"

class CNetStore
{
public:
	bool Init(const std::string& strWorkFolder);
	const std::string& GetNetFolder() const;
	const std::string& GetHistoryFolder() const;

	bool LoadProfile(NetProfile& out) const;
	bool SaveProfile(const NetProfile& profile) const;
	void ClearProfile() const;

	bool AppendHistory(const std::string& strUsername, const NetHistoryLine& line) const;
	bool LoadHistory(const std::string& strUsername, std::vector<NetHistoryLine>& out) const;

	static bool IsValidUsername(const std::string& strUsername);

private:
	static bool EnsureFolder(const std::string& strPath);
	static bool ReadTextFile(const std::string& strPath, std::string& strOut);
	static bool ReadTextFileTail(const std::string& strPath, std::string& strOut, bool& bTruncated);
	static bool AppendTextFile(const std::string& strPath, const std::string& strLine);
	std::string GetProfilePath() const;
	std::string GetHistoryPath(const std::string& strUsername) const;

	std::string m_strNetFolder;
	std::string m_strHistoryFolder;
};
