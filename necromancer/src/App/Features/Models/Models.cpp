#include <filesystem>
#include <fstream>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <set>
#include <vector>

#include "../CFG.h"
#include "GlovesData.h"
#include "Models.h"

static void ReloadMaterialPaths(const std::string &strSubs)
{
	static auto svCheats = I::CVar->FindVar("sv_cheats");
	if (!svCheats || strSubs.empty())
		return;

	const std::string strOriginal = svCheats->GetString();
	svCheats->SetValue("1");
	I::EngineClient->ClientCmd_Unrestricted(("mat_reloadmaterial \"" + strSubs + "\"").c_str());
	I::EngineClient->ClientCmd_Unrestricted(("sv_cheats " + strOriginal).c_str());
}

static bool IsSkySuffix(std::string &strSuffix)
{
	for (char &ch : strSuffix)
		ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));

	return strSuffix == "rt" || strSuffix == "bk" || strSuffix == "lf" || strSuffix == "ft" || strSuffix == "up" || strSuffix == "dn";
}

static void CollectLooseSkyNames(std::set<std::string> &setNames)
{
	const auto& workFolder = U::Storage->GetWorkFolder();
	const auto skyRoot = workFolder / "user_models" / "materials" / "skybox";

	std::error_code ec;
	for (std::filesystem::directory_iterator it(skyRoot, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file(ec))
			continue;

		if (_stricmp(it->path().extension().string().c_str(), ".vmt"))
			continue;

		const auto stem = it->path().stem().string();
		if (stem.size() <= 2)
			continue;

		std::string strSuffix = stem.substr(stem.size() - 2);
		if (IsSkySuffix(strSuffix))
			setNames.insert(stem.substr(0, stem.size() - 2));
	}
}

static void CollectVpkSkyNames(const std::filesystem::path &vpkPath, std::set<std::string> &setNames, int &nFileCount)
{
	std::ifstream in(vpkPath, std::ios::binary);
	if (!in)
		return;

	char szHeader[12] = {};
	in.read(szHeader, 12);
	if (!in)
		return;

	unsigned int nSignature = 0, nVersion = 0, nTreeSize = 0;
	memcpy(&nSignature, szHeader, 4);
	memcpy(&nVersion, szHeader + 4, 4);
	memcpy(&nTreeSize, szHeader + 8, 4);

	if (nSignature != 0x55aa1234u || nTreeSize == 0 || nTreeSize >(64u << 20))
		return;

	in.seekg(nVersion == 2 ? 28 : 12);
	std::string strTree(nTreeSize, '\0');
	in.read(strTree.data(), static_cast<std::streamsize>(nTreeSize));
	if (!in)
		return;

	size_t uPos = 0;
	auto ReadString = [&strTree, &uPos]() {
		std::string strOut;
		while (uPos < strTree.size() && strTree[uPos])
			strOut += strTree[uPos++];
		uPos++;
		return strOut;
	};

	while (uPos < strTree.size())
	{
		const std::string strExt = ReadString();
		if (strExt.empty())
			break;

		while (uPos < strTree.size())
		{
			std::string strPath = ReadString();
			if (strPath.empty())
				break;

			std::replace(strPath.begin(), strPath.end(), '\\', '/');
			const bool bSkybox = !_stricmp(strPath.c_str(), "materials/skybox");

			while (uPos < strTree.size())
			{
				const std::string strFile = ReadString();
				if (strFile.empty())
					break;

				if (uPos + 18 > strTree.size())
					return;

				unsigned short nPreload = 0;
				memcpy(&nPreload, strTree.data() + uPos + 4, 2);
				uPos += 18 + nPreload;
				if (uPos > strTree.size())
					return;

				nFileCount++;

				if (bSkybox && !_stricmp(strExt.c_str(), "vmt") && strFile.size() > 2)
				{
					std::string strSuffix = strFile.substr(strFile.size() - 2);
					if (IsSkySuffix(strSuffix))
						setNames.insert(strFile.substr(0, strFile.size() - 2));
				}
			}
		}
	}
}

static int ExtractVpkFile(const std::filesystem::path &vpkPath, const std::filesystem::path &destRoot, int &nSkipped, int &nModels)
{
	std::ifstream in(vpkPath, std::ios::binary);
	if (!in)
		return -1;

	char szHeader[12] = {};
	in.read(szHeader, 12);
	if (!in)
		return -1;

	unsigned int nSignature = 0, nVersion = 0, nTreeSize = 0;
	memcpy(&nSignature, szHeader, 4);
	memcpy(&nVersion, szHeader + 4, 4);
	memcpy(&nTreeSize, szHeader + 8, 4);

	if (nSignature != 0x55aa1234u || nTreeSize == 0 || nTreeSize >(64u << 20))
		return -1;

	const unsigned int nHeaderSize = nVersion == 2 ? 28 : 12;
	in.seekg(nHeaderSize);
	std::string strTree(nTreeSize, '\0');
	in.read(strTree.data(), static_cast<std::streamsize>(nTreeSize));
	if (!in)
		return -1;

	const unsigned int nDataBase = nHeaderSize + nTreeSize;
	char szBuffer[65536];

	auto CopyBytes = [&](std::istream &src, unsigned int nLength, const std::filesystem::path &destFile) {
		std::ofstream out(destFile, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;

		unsigned int nRemaining = nLength;
		while (nRemaining)
		{
			const auto nChunk = static_cast<std::streamsize>((std::min)(nRemaining, static_cast<unsigned int>(sizeof(szBuffer))));
			src.read(szBuffer, nChunk);
			if (src.gcount() != nChunk)
				return false;
			out.write(szBuffer, nChunk);
			nRemaining -= static_cast<unsigned int>(nChunk);
		}
		return true;
	};

	int nWritten = 0;
	size_t uPos = 0;
	auto ReadString = [&strTree, &uPos]() {
		std::string strOut;
		while (uPos < strTree.size() && strTree[uPos])
			strOut += strTree[uPos++];
		uPos++;
		return strOut;
	};

	while (uPos < strTree.size())
	{
		const std::string strExt = ReadString();
		if (strExt.empty())
			break;

		while (uPos < strTree.size())
		{
			std::string strPath = ReadString();
			if (strPath.empty())
				break;

			std::replace(strPath.begin(), strPath.end(), '\\', '/');
			const bool bBadPath = strPath.find("..") != std::string::npos || strPath.find(':') != std::string::npos;

			while (uPos < strTree.size())
			{
				const std::string strFile = ReadString();
				if (strFile.empty())
					break;

				if (uPos + 18 > strTree.size())
					return nWritten;

				unsigned short nPreload = 0, nArchiveIndex = 0;
				unsigned int nOffset = 0, nLength = 0;
				memcpy(&nPreload, strTree.data() + uPos + 4, 2);
				memcpy(&nArchiveIndex, strTree.data() + uPos + 6, 2);
				memcpy(&nOffset, strTree.data() + uPos + 8, 4);
				memcpy(&nLength, strTree.data() + uPos + 12, 4);
				uPos += 18 + nPreload;

				const bool bBadName = bBadPath
					|| strFile.find("..") != std::string::npos
					|| strFile.find(':') != std::string::npos
					|| strFile.find('/') != std::string::npos
					|| strFile.find('\\') != std::string::npos;

				if (bBadName || !_stricmp(strExt.c_str(), "vpk") || !nLength)
					continue;

				const auto destFile = destRoot / strPath / (strFile + "." + strExt);
				std::error_code ecDir;
				std::filesystem::create_directories(destFile.parent_path(), ecDir);

				std::error_code ecExists;
				if (std::filesystem::exists(destFile, ecExists))
				{
					std::error_code ecSize;
					if (std::filesystem::file_size(destFile, ecSize) == static_cast<std::uintmax_t>(nLength))
					{
						nSkipped++;
						continue;
					}
				}

				if (nArchiveIndex == 0x7FFF)
				{
					in.clear();
					in.seekg(static_cast<std::streamoff>(nDataBase) + nOffset);
					if (in && CopyBytes(in, nLength, destFile))
					{
						nWritten++;
						if (!_stricmp(strExt.c_str(), "mdl"))
							nModels++;
					}
				}
				else
				{
					std::string strIdx = std::to_string(nArchiveIndex);
					while (strIdx.size() < 3)
						strIdx.insert(strIdx.begin(), '0');
					std::ifstream part(vpkPath.parent_path() / (vpkPath.stem().string() + "_" + strIdx + ".vpk"), std::ios::binary);
					if (part)
					{
						part.seekg(static_cast<std::streamoff>(nOffset));
						if (CopyBytes(part, nLength, destFile))
						{
							nWritten++;
							if (!_stricmp(strExt.c_str(), "mdl"))
								nModels++;
						}
					}
				}
			}
		}
	}
	return nWritten;
}

void CModels::Init()
{
	MountUserFolder();
	ExtractVPKs();
}

void CModels::OnLevelInit()
{
	MountUserFolder();
	if (!m_bMounted)
		return;

	ScanAndMountPacks();
	ExtractVPKs();
}

void CModels::OnLevelShutdown()
{
	MountUserFolder();

	if (m_bMounted)
	{
		ScanAndMountPacks();
		if (ExtractVPKs() > 0)
			m_bFlushPending = true;
	}
}

bool CModels::ConsumePendingFlush()
{
	if (!m_bFlushPending)
		return false;

	m_bFlushPending = false;

	if (ConCommand *pCommand = I::CVar->FindCommand("r_flushlod"))
	{
		CCommand tEmpty = {};
		pCommand->Dispatch(tEmpty);
	}
	else
		I::EngineClient->ClientCmd_Unrestricted("r_flushlod");

	I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] custom models: flushing the model cache for the next map load\n");
	return true;
}

const std::vector<std::string> &CModels::GetSkyNames()
{
	std::set<std::string> setLoose;
	CollectLooseSkyNames(setLoose);

	if (!m_bSkyNamesDirty && setLoose == m_setLooseNames)
		return m_vecSkyNames;

	m_bSkyNamesDirty = false;
	m_setLooseNames = setLoose;

	std::set<std::string> setNames(setLoose.begin(), setLoose.end());

	const auto& workFolder = U::Storage->GetWorkFolder();
	const auto userModels = workFolder / "user_models";

	std::vector<std::filesystem::path> vecVpks;
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(userModels, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file(ec))
			continue;

		if (_stricmp(it->path().extension().string().c_str(), ".vpk"))
			continue;

		const auto stem = it->path().stem().string();
		if (stem.size() >= 4)
		{
			const bool bDigits = isdigit(static_cast<unsigned char>(stem[stem.size() - 1]))
				&& isdigit(static_cast<unsigned char>(stem[stem.size() - 2]))
				&& isdigit(static_cast<unsigned char>(stem[stem.size() - 3]));
			if (bDigits && stem[stem.size() - 4] == '_')
				continue;
		}

		vecVpks.push_back(it->path());
	}

	for (const auto &vpkPath : vecVpks)
	{
		int nFileCount = 0;
		CollectVpkSkyNames(vpkPath, setNames, nFileCount);
	}

	const char *pszGameDir = I::EngineClient->GetGameDirectory();
	if (pszGameDir && *pszGameDir)
	{
		int nFileCount = 0;
		CollectVpkSkyNames(std::filesystem::path(pszGameDir) / "tf2_misc_dir.vpk", setNames, nFileCount);
	}

	m_vecSkyNames.assign(setNames.begin(), setNames.end());
	return m_vecSkyNames;
}

void CModels::ApplyGloves()
{
	const bool bWant = CFG::Models_Active && CFG::Models_Gloves;
	if (m_bGlovesApplied == bWant)
		return;
	m_bGlovesApplied = bWant;

	const auto& workFolder = U::Storage->GetWorkFolder();
	const auto handsFile = workFolder / "user_models" / "materials" / "models" / "player" / "hvyweapon" / "hvyweapon_hands.vtf";

	if (bWant)
	{
		std::error_code ec;
		std::filesystem::create_directories(handsFile.parent_path(), ec);

		std::ofstream out(handsFile, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			I::CVar->ConsoleColorPrintf({ 255, 70, 70, 255 }, "[Necromancer] Necromancer Gloves: could not write hvyweapon_hands.vtf\n");
			return;
		}

		out.write(reinterpret_cast<const char *>(Necro::Gloves::g_ucHeavyHands), static_cast<std::streamsize>(Necro::Gloves::g_nHeavyHands));
		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] Necromancer Gloves on (heavy hands texture)\n");
	}
	else
	{
		std::error_code ec;
		std::filesystem::remove(handsFile, ec);
		I::CVar->ConsoleColorPrintf({ 255, 200, 70, 255 }, "[Necromancer] Necromancer Gloves off, heavy hands texture removed\n");
	}

	ReloadMaterialPaths("models/player/hvyweapon");
}

void CModels::Refresh()
{
	MountUserFolder();
	ScanAndMountPacks();
	const int nNewModels = ExtractVPKs();
	ApplyGloves();

	if (nNewModels > 0)
		m_bFlushPending = true;

	if (!CFG::Models_Active)
		I::CVar->ConsoleColorPrintf({ 255, 200, 70, 255 }, "[Necromancer] custom models are OFF (Active unchecked in the menu)\n");

	const auto& vecSkies = GetSkyNames();
	if (vecSkies.empty())
		I::CVar->ConsoleColorPrintf({ 255, 200, 70, 255 }, "[Necromancer] no skybox names found in user_models (drop a skybox .vpk or .vmt files, then Refresh again)\n");
	else
	{
		std::string strNames;
		int nPrinted = 0;
		for (auto itName = vecSkies.begin(); itName != vecSkies.end() && nPrinted < 50; ++itName, nPrinted++)
		{
			if (!strNames.empty())
				strNames += ", ";
			strNames += *itName;
		}
		if (vecSkies.size() > 50)
			strNames += ", ...";

		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] available skybox names (%i, loose + vpks + stock): %s\n", static_cast<int>(vecSkies.size()), strNames.c_str());
	}

	const auto& workFolder = U::Storage->GetWorkFolder();
	const auto materialsRoot = workFolder / "user_models" / "materials";

	std::set<std::string> setSubStrings;
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(materialsRoot, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file(ec))
			continue;

		const auto extension = it->path().extension().string();
		if (_stricmp(extension.c_str(), ".vmt") && _stricmp(extension.c_str(), ".vtf"))
			continue;

		std::string strSub = it->path().lexically_relative(materialsRoot).parent_path().generic_string();
		if (strSub.empty())
			strSub = it->path().stem().string();

		setSubStrings.insert(std::move(strSub));
	}

	if (setSubStrings.empty())
		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] no custom textures in user_models, nothing to refresh\n");
	else
	{
		std::string strSubs;
		for (const auto& strSub : setSubStrings)
		{
			if (!strSubs.empty())
				strSubs += '*';
			strSubs += strSub;
		}

		ReloadMaterialPaths(strSubs);

		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] refreshed %i custom path(s) from your folder, only those materials reload\n", static_cast<int>(setSubStrings.size()));
	}

	if (m_bFlushPending)
		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] model changes queued, the game re-reads models on the next map change\n");
}

void CModels::UpdateMaterialFilter()
{
	static bool s_bApplied = false;
	static std::string s_strSavedEnable;
	static std::string s_strSavedTextOut;

	const bool bWant = CFG::Models_Active && CFG::Models_HideMaterialErrors;
	if (s_bApplied == bWant)
		return;

	const auto conFilterEnable = I::CVar->FindVar("con_filter_enable");
	const auto conFilterTextOut = I::CVar->FindVar("con_filter_text_out");
	if (!conFilterEnable || !conFilterTextOut)
		return;

	if (bWant)
	{
		s_strSavedEnable = conFilterEnable->GetString();
		s_strSavedTextOut = conFilterTextOut->GetString();
		conFilterTextOut->SetValue("Material");
		conFilterEnable->SetValue("1");
	}
	else
	{
		conFilterTextOut->SetValue(s_strSavedTextOut.c_str());
		conFilterEnable->SetValue(s_strSavedEnable.c_str());
	}

	s_bApplied = bWant;
}

void CModels::MountUserFolder()
{
	UpdateMaterialFilter();

	if (!CFG::Models_Active)
	{
		UnmountUserFolder();
		return;
	}

	if (m_bMounted)
		return;

	const auto& workFolder = U::Storage->GetWorkFolder();
	const auto userModels = workFolder / "user_models";

	std::error_code ec;
	std::filesystem::create_directories(userModels / "models" / "player", ec);
	std::filesystem::create_directories(userModels / "materials" / "models" / "player", ec);
	std::filesystem::create_directories(userModels / "materials" / "skybox", ec);

	const auto readmePath = userModels / "README.txt";
	{
		std::ofstream out(readmePath, std::ios::trunc);
		out << "Necromancer custom models\n";
		out << "=========================\n\n";
		out << "Just drop folders in here, like the game's tf/custom folder:\n";
		out << "any folder that has its own materials or models inside gets\n";
		out << "mounted automatically on the next map load (or click Refresh).\n\n";
		out << "  user_models\\MyCoolPack\\materials\\models\\player\\...  <- works\n";
		out << "  user_models\\materials\\skybox\\...                     <- also works\n\n";
		out << "VPKs: drop any .vpk anywhere in here, the files get extracted\n";
		out << "automatically, then click Refresh.\n\n";
		out << "SKYBOXES\n";
		out << "  Pick one from the search list in Visuals > Other2 > Custom Skybox.\n\n";
		out << "CLASS MODELS / VIEWMODEL ARMS\n";
		out << "  Keep the pack's own folder layout inside its folder, nothing to\n";
		out << "  rearrange. Models MUST be built on the original class skeleton\n";
		out << "  (same bones + animations) or they will T-pose or crash the game.\n\n";
		out << "NECROMANCER GLOVES\n";
		out << "  Toggle in Visuals > Other2 > Custom Models. Right now it swaps\n";
		out << "  the Heavy's hands texture. While it is ON it owns\n";
		out << "  materials\\models\\player\\hvyweapon\\hvyweapon_hands.vtf here.\n\n";
		out << "HIDE MATERIAL ERRORS\n";
		out << "  Toggle in Visuals > Other2 > Custom Models. While it is ON it owns\n";
		out << "  the con_filter console settings (hides lines containing \"Material\").\n\n";
		out << "Refresh reloads custom textures live. Custom MODELS apply on the\n";
		out << "next map change (the game cannot safely swap .mdl files mid-map).\n";
		out << "Unchecking Active brings stock models back on the next map change.\n";
	}

	const auto pFileSystem = Memory::FindInterface("filesystem_stdio.dll", "VFileSystem022");
	if (!pFileSystem)
	{
		I::CVar->ConsoleColorPrintf({ 255, 70, 70, 255 }, "[Necromancer] custom models: filesystem interface not found\n");
		return;
	}

	const auto pathString = userModels.string();

	using AddSearchPathFn = void(__fastcall *)(void *, const char *, const char *, int);
	const auto fnAddSearchPath = reinterpret_cast<AddSearchPathFn>(Memory::GetVFunc(pFileSystem, 7));
	fnAddSearchPath(pFileSystem, pathString.c_str(), "GAME", 0);

	char szPaths[8192] = {};
	using GetSearchPathFn = int(__fastcall *)(void *, const char *, bool, char *, int);
	const auto fnGetSearchPath = reinterpret_cast<GetSearchPathFn>(Memory::GetVFunc(pFileSystem, 13));
	fnGetSearchPath(pFileSystem, "GAME", false, szPaths, sizeof(szPaths));

	m_vecMountedPaths.clear();
	m_vecMountedPaths.push_back(pathString);
	m_bMounted = true;
	m_bFlushPending = true;

	if (strstr(szPaths, "user_models"))
	{
		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] custom models folder mounted and verified: %s\n", pathString.c_str());
	}
	else
	{
		I::CVar->ConsoleColorPrintf({ 255, 70, 70, 255 }, "[Necromancer] custom models MOUNT FAILED, game search paths: %s\n", szPaths);
	}

	ScanAndMountPacks();

	ApplyGloves();
}

void CModels::UnmountUserFolder()
{
	if (!m_bMounted)
		return;

	const auto pFileSystem = Memory::FindInterface("filesystem_stdio.dll", "VFileSystem022");
	if (pFileSystem)
	{
		using RemoveSearchPathFn = bool(__fastcall *)(void *, const char *, const char *);
		const auto fnRemoveSearchPath = reinterpret_cast<RemoveSearchPathFn>(Memory::GetVFunc(pFileSystem, 8));
		for (const auto &strPath : m_vecMountedPaths)
			fnRemoveSearchPath(pFileSystem, strPath.c_str(), "GAME");
	}

	m_vecMountedPaths.clear();
	m_bMounted = false;
	m_bFlushPending = true;

	I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] custom models disabled, stock models return on the next map change\n");

	ApplyGloves();
}

int CModels::ScanAndMountPacks()
{
	if (!m_bMounted)
		return 0;

	const auto pFileSystem = Memory::FindInterface("filesystem_stdio.dll", "VFileSystem022");
	if (!pFileSystem)
		return 0;

	const auto& workFolder = U::Storage->GetWorkFolder();
	const auto userModels = workFolder / "user_models";

	using AddSearchPathFn = void(__fastcall *)(void *, const char *, const char *, int);
	const auto fnAddSearchPath = reinterpret_cast<AddSearchPathFn>(Memory::GetVFunc(pFileSystem, 7));

	int nPacks = 0;
	std::error_code ecPacks;
	for (std::filesystem::directory_iterator it(userModels, ecPacks), end; it != end && !ecPacks; it.increment(ecPacks))
	{
		if (!it->is_directory(ecPacks))
			continue;

		std::vector<std::filesystem::path> vecRoots;
		if (std::filesystem::exists(it->path() / "materials") || std::filesystem::exists(it->path() / "models"))
			vecRoots.push_back(it->path());
		else
		{
			std::error_code ecInner;
			for (std::filesystem::directory_iterator inner(it->path(), ecInner), innerEnd; inner != innerEnd && !ecInner; inner.increment(ecInner))
			{
				if (inner->is_directory(ecInner) && (std::filesystem::exists(inner->path() / "materials") || std::filesystem::exists(inner->path() / "models")))
					vecRoots.push_back(inner->path());
			}
		}

		for (const auto &path : vecRoots)
		{
			const std::string strPath = path.string();

			bool bKnown = false;
			for (const auto &strMounted : m_vecMountedPaths)
			{
				if (!_stricmp(strMounted.c_str(), strPath.c_str()))
				{
					bKnown = true;
					break;
				}
			}
			if (bKnown)
				continue;

			fnAddSearchPath(pFileSystem, strPath.c_str(), "GAME", 0);
			m_vecMountedPaths.push_back(strPath);
			m_bFlushPending = true;
			nPacks++;
		}
	}

	if (nPacks)
		I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] mounted %i new dropped pack folder(s) from user_models\n", nPacks);

	return nPacks;
}

int CModels::ExtractVPKs()
{
	m_bSkyNamesDirty = true;

	int nTotalModels = 0;

	const auto& workFolder = U::Storage->GetWorkFolder();
	const auto userModels = workFolder / "user_models";

	std::vector<std::filesystem::path> vecVpks;
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(userModels, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file(ec))
			continue;

		if (_stricmp(it->path().extension().string().c_str(), ".vpk"))
			continue;

		const auto stem = it->path().stem().string();
		if (stem.size() >= 4)
		{
			const bool bDigits = isdigit(static_cast<unsigned char>(stem[stem.size() - 1]))
				&& isdigit(static_cast<unsigned char>(stem[stem.size() - 2]))
				&& isdigit(static_cast<unsigned char>(stem[stem.size() - 3]));
			if (bDigits && stem[stem.size() - 4] == '_')
				continue;
		}

		vecVpks.push_back(it->path());
	}

	for (const auto &vpkPath : vecVpks)
	{
		int nSkipped = 0, nModels = 0;
		const int nExtracted = ExtractVpkFile(vpkPath, userModels, nSkipped, nModels);
		nTotalModels += nModels;

		if (nExtracted < 0)
			I::CVar->ConsoleColorPrintf({ 255, 70, 70, 255 }, "[Necromancer] could not read vpk (not a valid vpk): %s\n", vpkPath.string().c_str());
		else if (!nExtracted && !nSkipped)
			I::CVar->ConsoleColorPrintf({ 255, 200, 70, 255 }, "[Necromancer] nothing to extract from: %s\n", vpkPath.filename().string().c_str());
		else if (nExtracted)
		{
			if (nSkipped)
				I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] extracted %i new file(s), %i up-to-date, from %s (loose in user_models, auto-mounted)\n", nExtracted, nSkipped, vpkPath.filename().string().c_str());
			else
				I::CVar->ConsoleColorPrintf({ 197, 108, 240, 255 }, "[Necromancer] extracted %i files from %s (loose in user_models, auto-mounted)\n", nExtracted, vpkPath.filename().string().c_str());
		}
	}
	return nTotalModels;
}
