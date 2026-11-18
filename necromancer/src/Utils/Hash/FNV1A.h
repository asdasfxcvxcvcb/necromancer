#pragma once

#include <cstdint>

namespace FNV1A
{
	constexpr uint32_t FNV_PRIME_32 = 0x01000193;
	constexpr uint32_t FNV_OFFSET_32 = 0x811c9dc5;

	constexpr uint32_t Hash32Const(const char* str, uint32_t hash = FNV_OFFSET_32)
	{
		return (*str == '\0') ? hash : Hash32Const(str + 1, (hash ^ static_cast<uint32_t>(*str)) * FNV_PRIME_32);
	}

	inline uint32_t Hash32(const char* str)
	{
		uint32_t hash = FNV_OFFSET_32;
		while (*str)
		{
			hash ^= static_cast<uint32_t>(*str++);
			hash *= FNV_PRIME_32;
		}
		return hash;
	}
}
