#pragma once

#include <filesystem>
#include <string>
#include <vector>

bool SoundpadDecodeToMono48k(const std::filesystem::path& fsPath, std::vector<float>& vecPcm);
