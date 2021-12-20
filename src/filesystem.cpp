#include <blah/filesystem.h>
#include <blah/streams/filestream.h>
#include "internal/platform.h"

using namespace Blah;

FileRef File::open(const FilePath& path, FileMode mode)
{
	return Platform::file_open(path.cstr(), mode);
}

bool File::exists(const FilePath& path)
{
	return Platform::file_exists(path.cstr());
}

bool File::destroy(const FilePath& path)
{
	return Platform::file_delete(path.cstr());
}

bool Directory::create(const FilePath& path)
{
	return Platform::dir_create(path.cstr());
}

bool Directory::exists(const FilePath& path)
{
	return Platform::dir_exists(path.cstr());
}

bool Directory::destroy(const FilePath& path)
{
	return Platform::dir_delete(path.cstr());
}

Vector<FilePath> Directory::enumerate(const FilePath& path, bool recursive)
{
	Vector<FilePath> list;

	// get files
	Platform::dir_enumerate(list, path.cstr(), recursive);

	// normalize path names
	for (auto& it : list)
		it.replace('\\', '/');

	return list;
}

void Directory::explore(const FilePath& path)
{
	Platform::dir_explore(path);
}

FilePath Path::get_file_name(const FilePath& path)
{
	const char* cstr = path.cstr();
	auto length = path.length();

	for (auto n = length; n > 0; n--)
		if (*(cstr + n - 1) == '/')
			return FilePath(cstr + n);

	return path;
}

FilePath Path::get_file_name_no_ext(const FilePath& path)
{
	FilePath filename = get_file_name(path);
	return get_path_no_ext(filename);
}

FilePath Path::get_path_no_ext(const FilePath& path)
{
	const char* cstr = path.cstr();
	for (int n = 0; n < path.length(); n++)
		if (*(cstr + n) == '.')
			return FilePath(cstr, cstr + n);
	return path;
}

FilePath Path::get_directory_name(const FilePath& path)
{
	FilePath directory = path;
	while (directory.ends_with("/"))
		directory = directory.substr(0, -1);
	auto last = directory.last_index_of('/');
	if (last >= 0)
		directory = directory.substr(0, last + 1);
	return directory;
}

FilePath Path::get_path_after(const FilePath& path, const FilePath& after)
{
	for (int i = 0; i < path.length(); i++)
	{
		FilePath substr(path.begin() + i, path.end());
		if (substr.starts_with(after, false))
			return FilePath(path.begin() + i + after.length(), path.end());
	}

	return path;
}

FilePath Path::normalize(const FilePath& path)
{
	FilePath normalized;

	auto len = path.length();
	for (auto n = 0; n < len; n++)
	{
		// normalize slashes
		if (path[n] == '\\' || path[n] == '/')
		{
			if (normalized.length() == 0 || normalized[normalized.length() - 1] != '/')
				normalized.append('/');
		}
		// move up a directory
		else if (path[n] == '.' && n < len - 1 && path[n + 1] == '.')
		{
			// search backwards for last /
			bool could_move_up = false;
			if (normalized.length() > 0)
			{
				for (auto k = normalized.length() - 1; k > 0; k--)
					if (normalized[k - 1] == '/')
					{
						normalized = normalized.substr(0, k - 1);
						could_move_up = true;
						break;
					}
			}

			if (!could_move_up)
				normalized.append('.');
			else
				n++;
		}
		else
			normalized.append(path[n]);
	}

	return normalized;
}

FilePath Path::join(const FilePath& a, const FilePath& b)
{
	return normalize(FilePath(a).append("/").append(b));
}