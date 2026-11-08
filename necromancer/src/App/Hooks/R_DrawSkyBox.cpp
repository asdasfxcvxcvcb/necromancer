#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

#include <cstring>
#include <filesystem>

MAKE_SIGNATURE(R_DrawSkyBox, "engine.dll", "48 8B C4 55 53 41 54 41 55", 0x0);

MAKE_HOOK(R_DrawSkyBox, Signatures::R_DrawSkyBox.Get(), void, __fastcall,
	float zFar, int nDrawFlags)
{
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		CALL_ORIGINAL(zFar, nDrawFlags);

		return;
	}

	static std::string s_strRaw;
	static std::string s_strName;

	const auto& custom = CFG::Visuals_Custom_Skybox_Texture_Name;
	if (custom != s_strRaw)
	{
		s_strRaw = custom;

		const size_t nSlash = custom.find_last_of("/\\");
		s_strName = nSlash != std::string::npos ? custom.substr(nSlash + 1) : custom;

		for (const char *pszExt : { ".vmt", ".vtf", ".vpk", ".bsp" })
		{
			const size_t nLen = strlen(pszExt);
			if (s_strName.size() > nLen && !_stricmp(s_strName.c_str() + s_strName.size() - nLen, pszExt))
			{
				s_strName.resize(s_strName.size() - nLen);
				break;
			}
		}

		if (!s_strName.empty())
		{
			const auto& workFolder = U::Storage->GetWorkFolder();
			const auto skyRoot = workFolder / "user_models" / "materials" / "skybox";

			bool bFound = false;
			std::error_code ec;
			for (std::filesystem::directory_iterator it(skyRoot, ec), end; it != end && !bFound; it.increment(ec))
			{
				if (ec || !it->is_regular_file(ec))
					continue;

				const auto stem = it->path().stem().string();
				for (const char *pszSuffix : { "rt", "bk", "lf", "ft", "up", "dn" })
				{
					if (!_stricmp(stem.c_str(), (s_strName + pszSuffix).c_str()))
					{
						bFound = true;
						break;
					}
				}
			}

			if (!bFound)
				I::CVar->ConsoleColorPrintf({ 255, 200, 70, 255 }, "[Necromancer] skybox '%s': not found loose in user_models (vpk/stock sky can still work; if the sky does not change, click Refresh in Custom Models to list valid names)\n", s_strName.c_str());
		}
	}

	if (!s_strName.empty())
	{
		static auto sv_skyname{ I::CVar->FindVar("sv_skyname") };
		if (sv_skyname)
		{
			const std::string original = sv_skyname->GetString();

			sv_skyname->SetValue(s_strName.c_str());

			CALL_ORIGINAL(zFar, nDrawFlags);

			sv_skyname->SetValue(original.c_str());

			return;
		}
	}

	CALL_ORIGINAL(zFar, nDrawFlags);
}
