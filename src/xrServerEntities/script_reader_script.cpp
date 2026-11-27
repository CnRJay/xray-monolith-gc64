////////////////////////////////////////////////////////////////////////////
//	Module 		: script_reader_script.cpp
//	Created 	: 05.10.2004
//  Modified 	: 05.10.2004
//	Author		: Dmitriy Iassenev
//	Description : Script reader
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_reader.h"
#include "script_engine.h"
#include "ai_space.h"

using namespace luabind;

bool r_eof(IReader* self)
{
	return (!!self->eof());
}

LPCSTR r_stringZ(IReader* self)
{
	shared_str temp;
	self->r_stringZ(temp);
	return (*temp);
}

bool r_bool(IReader* self)
{
	return (!!self->r_u8());
}

void r_fvector3(IReader* self, Fvector* arg0)
{
	self->r_fvector3(*arg0);
}

static luabind::internal_string r_file_as_string(const char* path)
{
	auto fileIter = FS.exist(path);
	if (fileIter == nullptr)
	{
		lua_pushfstring(ai().script_engine().lua(), "Not found file: %s", path);
		lua_error(ai().script_engine().lua());

		return nullptr;
	}

	IReader* reader = FS.r_open(path);

	luabind::internal_string result;
	result.resize(reader->length());
	std::memcpy(result.data(), reader->pointer(), sizeof(char) * reader->length());
	FS.r_close(reader);

	return result;
}

static void w_file_from_string(const char* path, const char* buffer)
{
	auto fileIter = FS.exist(path);
	if (fileIter == nullptr)
	{
		lua_pushfstring(ai().script_engine().lua(), "Not found file: %s", path);
		lua_error(ai().script_engine().lua());

		return;
	}

	shared_str newPath = fileIter->name;
	IWriter* writer = FS.w_open(*newPath);
	writer->w(buffer, xr_strlen(buffer));
	FS.w_close(writer);
}

#pragma optimize("s",on)
void CScriptReader::script_register(lua_State* L)
{
	module(L)
	[
		class_<IReader>("reader")
		.def("r_seek", &IReader::seek)
		.def("r_tell", &IReader::tell)
		.def("r_vec3", &::r_fvector3)
		.def("r_float", (void (IReader::*)(float&))(&IReader::r_float))
		.def("r_u64", (void (IReader::*)(u64&))(&IReader::r_u64))
		.def("r_s64", (void (IReader::*)(s64&))(&IReader::r_s64))
		.def("r_u32", (void (IReader::*)(u32&))(&IReader::r_u32))
		.def("r_s32", (void (IReader::*)(s32&))(&IReader::r_s32))
		.def("r_u16", (void (IReader::*)(u16&))(&IReader::r_u16))
		.def("r_s16", (void (IReader::*)(s16&))(&IReader::r_s16))
		.def("r_u8", (void (IReader::*)(u8&))(&IReader::r_u8))
		.def("r_s8", (void (IReader::*)(s8&))(&IReader::r_s8))
		.def("r_bool", &::r_bool)
		.def("r_float", (float (IReader::*)())(&IReader::r_float))
		.def("r_u64", (u64 (IReader::*)())(&IReader::r_u64))
		.def("r_s64", (s64 (IReader::*)())(&IReader::r_s64))
		.def("r_u32", (u32 (IReader::*)())(&IReader::r_u32))
		.def("r_s32", (s32 (IReader::*)())(&IReader::r_s32))
		.def("r_u16", (u16 (IReader::*)())(&IReader::r_u16))
		.def("r_s16", (s16 (IReader::*)())(&IReader::r_s16))
		.def("r_u8", (u8 (IReader::*)())(&IReader::r_u8))
		.def("r_s8", (s8 (IReader::*)())(&IReader::r_s8))
		.def("r_float_q16", &IReader::r_float_q16)
		.def("r_float_q8", &IReader::r_float_q8)
		.def("r_angle16", &IReader::r_angle16)
		.def("r_angle8", &IReader::r_angle8)
		.def("r_dir", &IReader::r_dir)
		.def("r_sdir", &IReader::r_sdir)
		.def("r_stringZ", &r_stringZ)
		.def("r_elapsed", &IReader::elapsed)
		.def("r_advance", &IReader::advance)
		.def("r_eof", &r_eof),

		def("r_file_as_string", &r_file_as_string)
		//def("w_file_from_string", &w_file_from_string)
	];
}
