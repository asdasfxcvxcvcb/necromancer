#pragma once

#include <string>
#include <vector>

class CNetAvatar
{
public:
	static bool EncodeFromFile(const std::string& strPath, std::string& strOutB64, std::string& strError);
	static bool EncodeFromRGBA(const unsigned char* pData, int nWidth, int nHeight, std::string& strOutB64, std::string& strError);
	static bool DecodeToRGBA(const std::string& strB64, std::vector<unsigned char>& vecOut, int& nWidth, int& nHeight);
};
