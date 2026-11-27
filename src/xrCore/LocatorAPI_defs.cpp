#include "stdafx.h"
#pragma hdrstop

#include "LocatorAPI_defs.h"
#pragma warning(disable:4995)
#include <io.h>
#include <direct.h>
#include <fcntl.h>
#include <sys\stat.h>
#pragma warning(default:4995)

//////////////////////////////////////////////////////////////////////
// FS_File
//////////////////////////////////////////////////////////////////////
FS_File::FS_File(const xr_string& nm, long sz, time_t modif, unsigned attr) { set(nm, sz, modif, attr); }
FS_File::FS_File(const xr_string& nm) { set(nm, 0, 0, 0); }
FS_File::FS_File(const _FINDDATA_T& f) { set(f.name, f.size, f.time_write, (f.attrib & _A_SUBDIR) ? flSubDir : 0); }

FS_File::FS_File(const xr_string& nm, const _FINDDATA_T& f)
{
	set(nm, f.size, f.time_write, (f.attrib & _A_SUBDIR) ? flSubDir : 0);
}

void FS_File::set(const xr_string& nm, long sz, time_t modif, unsigned attr)
{
	name = nm;
	xr_strlwr(name);
	size = sz;
	time_write = modif;
	attrib = attr;
}

//////////////////////////////////////////////////////////////////////
// FS_Path
//////////////////////////////////////////////////////////////////////
namespace
{
LPSTR xr_strdup_lwr(LPCSTR value)
{
	return value ? xr_strlwr(xr_strdup(value)) : 0;
}

void append_path_separator(LPSTR path, size_t path_size)
{
	if (path[0] && path[xr_strlen(path) - 1] != '\\')
		xr_strcat(path, path_size, "\\");
}
}

FS_Path::FS_Path(LPCSTR _Root, LPCSTR _Add, LPCSTR _DefExt, LPCSTR _FilterCaption, u32 flags)
{
	// VERIFY (_Root&&_Root[0]);
	m_Path = 0;
	m_Root = 0;
	m_Add = 0;
	m_DefExt = 0;
	m_FilterCaption = 0;

	LPCSTR root = _Root ? _Root : "";
	LPCSTR add = _Add ? _Add : "";

	string_path temp;
	xr_strcpy(temp, sizeof(temp), root);
	xr_strcat(temp, sizeof(temp), add);
	append_path_separator(temp, sizeof(temp));

	m_Path = xr_strdup_lwr(temp);
	m_DefExt = xr_strdup_lwr(_DefExt);
	m_FilterCaption = xr_strdup_lwr(_FilterCaption);
	m_Add = xr_strdup_lwr(_Add);
	m_Root = xr_strdup_lwr(_Root);
	m_Flags.assign(flags);
#ifdef _EDITOR
    // Editor(s)/User(s) wants pathes already created in "real" file system :)
    VerifyPath(m_Path);
#endif
}

FS_Path::~FS_Path()
{
	xr_free(m_Root);
	xr_free(m_Path);
	xr_free(m_Add);
	xr_free(m_DefExt);
	xr_free(m_FilterCaption);
}

void FS_Path::_set(LPCSTR add)
{
	// m_Add
	LPCSTR new_add = add ? add : "";
	LPSTR duplicated_add = xr_strdup_lwr(new_add);

	// m_Path
	string_path temp;
	strconcat(sizeof(temp), temp, m_Root ? m_Root : "", new_add);
	append_path_separator(temp, sizeof(temp));
	LPSTR duplicated_path = xr_strdup_lwr(temp);

	xr_free(m_Add);
	m_Add = duplicated_add;
	xr_free(m_Path);
	m_Path = duplicated_path;
}

void FS_Path::_set_root(LPCSTR root)
{
	LPCSTR new_root = root ? root : "";

	string_path temp;
	xr_strcpy(temp, sizeof(temp), new_root);
	append_path_separator(temp, sizeof(temp));
	LPSTR duplicated_root = xr_strdup_lwr(temp);

	// m_Path
	strconcat(sizeof(temp), temp, duplicated_root ? duplicated_root : "", m_Add ? m_Add : "");
	append_path_separator(temp, sizeof(temp));
	LPSTR duplicated_path = xr_strdup_lwr(temp);

	xr_free(m_Root);
	m_Root = duplicated_root;

	xr_free(m_Path);
	m_Path = duplicated_path;
}

LPCSTR FS_Path::_update(string_path& dest, LPCSTR src) const
{
	R_ASSERT(dest);
	R_ASSERT(src);
	string_path temp;
	xr_strcpy(temp, sizeof(temp), src);
	strconcat(sizeof(dest), dest, m_Path, temp);
	return xr_strlwr(dest);
}

/*
void FS_Path::_update(xr_string& dest, LPCSTR src)const
{
R_ASSERT(src);
dest = xr_string(m_Path)+src;
xr_strlwr (dest);
}*/
void FS_Path::rescan_path_cb()
{
	m_Flags.set(flNeedRescan, TRUE);
	FS.m_Flags.set(CLocatorAPI::flNeedRescan, TRUE);
}

bool XRCORE_API PatternMatch(LPCSTR s, LPCSTR mask)
{
	LPCSTR cp = 0;
	LPCSTR mp = 0;
	for (; *s && *mask != '*'; mask++, s++) if (*mask != *s && *mask != '?') return false;
	for (;;)
	{
		if (!*s)
		{
			while (*mask == '*') mask++;
			return !*mask;
		}
		if (*mask == '*')
		{
			if (!*++mask) return true;
			mp = mask;
			cp = s + 1;
			continue;
		}
		if (*mask == *s || *mask == '?')
		{
			mask++, s++;
			continue;
		}
		mask = mp;
		s = cp++;
	}
}
