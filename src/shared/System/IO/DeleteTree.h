#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

namespace Nilesoft
{
	namespace IO
	{
		namespace detail
		{
			inline void clear_readonly(const std::wstring &path, DWORD attributes)
			{
				if(attributes & FILE_ATTRIBUTE_READONLY)
					::SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
			}

			inline std::wstring join(const std::wstring &dir, const wchar_t *name)
			{
				std::wstring result = dir;
				if(!result.empty() && result.back() != L'\\' && result.back() != L'/')
					result += L'\\';
				result += name;
				return result;
			}
		}

		// Deletes a file, or a directory together with everything inside it.
		// Junctions and symbolic links are removed as links and never followed,
		// so nothing outside the tree can be deleted through them.
		// Walks the tree with an explicit stack, so depth cannot exhaust the
		// calling thread's stack. Returns true only if the path was removed.
		inline bool DeleteTree(const std::wstring &path)
		{
			auto attributes = ::GetFileAttributesW(path.c_str());
			if(attributes == INVALID_FILE_ATTRIBUTES)
				return false;

			if(!(attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				detail::clear_readonly(path, attributes);
				return ::DeleteFileW(path.c_str()) != 0;
			}

			// second: true once the directory's children have been handled.
			std::vector<std::pair<std::wstring, bool>> pending;
			pending.emplace_back(path, (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);

			bool root_removed = false;
			while(!pending.empty())
			{
				if(pending.back().second)
				{
					auto dir = std::move(pending.back().first);
					pending.pop_back();

					auto dir_attributes = ::GetFileAttributesW(dir.c_str());
					if(dir_attributes != INVALID_FILE_ATTRIBUTES)
						detail::clear_readonly(dir, dir_attributes);

					// Removes a junction or link itself, never its target.
					bool removed = ::RemoveDirectoryW(dir.c_str()) != 0;
					if(pending.empty())
						root_removed = removed;
					continue;
				}

				pending.back().second = true;
				const auto dir = pending.back().first;

				WIN32_FIND_DATAW data{};
				auto pattern = detail::join(dir, L"*");
				auto find = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
											   FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
				if(find == INVALID_HANDLE_VALUE)
					continue;

				do
				{
					if(data.cFileName[0] == L'.' &&
					   (data.cFileName[1] == 0 || (data.cFileName[1] == L'.' && data.cFileName[2] == 0)))
						continue;

					auto child = detail::join(dir, data.cFileName);
					if(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					{
						// A linked directory is queued as already handled, so only the
						// link is removed and its target is never listed.
						pending.emplace_back(std::move(child), (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
					}
					else
					{
						detail::clear_readonly(child, data.dwFileAttributes);
						::DeleteFileW(child.c_str());
					}
				} while(::FindNextFileW(find, &data));

				::FindClose(find);
			}

			return root_removed;
		}
	}
}
