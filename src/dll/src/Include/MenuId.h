#pragma once

#include <cstdint>

namespace Nilesoft
{
	namespace Shell
	{
		namespace MenuId
		{
			inline bool needs_unique(uint32_t native_id, bool is_popup)
			{
				return is_popup || native_id == 0 || native_id == static_cast<uint32_t>(-1);
			}

			inline uint32_t allocate(uint32_t native_id, bool is_popup, uint32_t &next_id)
			{
				return needs_unique(native_id, is_popup) ? next_id++ : native_id;
			}
		}
	}
}
