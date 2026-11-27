#if defined(SVP_TEST_CLIENT)
#include "../xrCore/xrCore.h"
#ifndef ENGINE_API
#define ENGINE_API
#endif
#include "../xrEngine/svp_state.h"
#include "svp_optic_config_script.h"
#include <luabind/luabind.hpp>
#include <luabind/object.hpp>
#include <lua.hpp>
#else
#include "pch_script.h"
#include "svp_optic_config_script.h"
#include "../xrEngine/device.h"
#include "../xrEngine/svp_gameplay_cvars.h"
#include "../Layers/xrRender/svp_console.h"
#include "ai_space.h"
#include "script_engine.h"
#endif

#include <cmath>

#if defined(SVP_TEST_CLIENT)
namespace svp_test_detail
{
lua_State* lua = nullptr;
CSecondVPParams viewport;
int scope_mode = 0;
}

void svp_test_client_attach(lua_State* state)
{
	svp_test_detail::lua = state;
}

void svp_test_client_set_scope_mode(int mode)
{
	svp_test_detail::scope_mode = mode;
	svp_test_detail::viewport.SetOpticScopeMode(static_cast<u8>(mode));
}

CSecondVPParams& svp_test_client_viewport()
{
	return svp_test_detail::viewport;
}
#endif

namespace
{
#if defined(SVP_TEST_CLIENT)
#define SVP_CONFIG_LOG(...) ((void)0)
#else
#define SVP_CONFIG_LOG(...) Msg(__VA_ARGS__)
#endif

lua_State* svp_lua_state()
{
#if defined(SVP_TEST_CLIENT)
	return svp_test_detail::lua;
#else
	return ai().script_engine().lua();
#endif
}

CSecondVPParams& svp_viewport()
{
#if defined(SVP_TEST_CLIENT)
	return svp_test_detail::viewport;
#else
	return Device.m_SecondViewport;
#endif
}

bool svp_transport_active()
{
#if defined(SVP_TEST_CLIENT)
	return svp_viewport().IsOpticApiConnected();
#else
	return svp_optic_api_active();
#endif
}

int svp_scope_mode()
{
#if defined(SVP_TEST_CLIENT)
	return svp_test_detail::scope_mode;
#else
	return scope_svp_enabled;
#endif
}

using EFieldType = CSecondVPParams::EOpticFieldType;
using EFieldId = CSecondVPParams::EOpticFieldId;
using SObjectMemberDescriptor = CSecondVPParams::OpticObjectMemberDescriptor;
using SFieldDescriptor = CSecondVPParams::OpticFieldDescriptor;

constexpr auto field_integer = CSecondVPParams::optic_type_integer;
constexpr auto field_number = CSecondVPParams::optic_type_number;
constexpr auto field_boolean = CSecondVPParams::optic_type_boolean;
constexpr auto field_string = CSecondVPParams::optic_type_string;
constexpr auto field_objective = CSecondVPParams::optic_type_objective;
constexpr auto field_mode = CSecondVPParams::optic_type_mode;
constexpr auto type_magnifications = CSecondVPParams::optic_type_magnifications;
constexpr auto field_lane = CSecondVPParams::optic_type_lane;
constexpr auto type_sources = CSecondVPParams::optic_type_sources;

constexpr auto field_schema_version = CSecondVPParams::optic_field_schema_version;
constexpr auto field_context_token = CSecondVPParams::optic_field_context_token;
constexpr auto field_context = CSecondVPParams::optic_field_context;
constexpr auto field_weapon = CSecondVPParams::optic_field_weapon;
constexpr auto field_weapon_id = CSecondVPParams::optic_field_weapon_id;
constexpr auto field_scope = CSecondVPParams::optic_field_scope;
constexpr auto field_diagnostic_scope = CSecondVPParams::optic_field_diagnostic_scope;
constexpr auto field_identity_source = CSecondVPParams::optic_field_identity_source;
constexpr auto field_zoom_type = CSecondVPParams::optic_field_zoom_type;
constexpr auto field_profile_id = CSecondVPParams::optic_field_profile_id;
constexpr auto field_spec_section = CSecondVPParams::optic_field_spec_section;
constexpr auto field_model = CSecondVPParams::optic_field_model;
constexpr auto field_binding = CSecondVPParams::optic_field_binding;
constexpr auto field_binding_section = CSecondVPParams::optic_field_binding_section;
constexpr auto field_reticle_type = CSecondVPParams::optic_field_reticle_type;
constexpr auto field_hybrid_reflex = CSecondVPParams::optic_field_hybrid_reflex;
constexpr auto field_objective_offset = CSecondVPParams::optic_field_objective_offset;
constexpr auto field_objective_mm = CSecondVPParams::optic_field_objective_mm;
constexpr auto field_middle_grey = CSecondVPParams::optic_field_middle_grey;
constexpr auto field_adapt_speed = CSecondVPParams::optic_field_adapt_speed;
constexpr auto field_convergence_limit_m = CSecondVPParams::optic_field_convergence_limit_m;
constexpr auto field_tunneling_parallax = CSecondVPParams::optic_field_tunneling_parallax;
constexpr auto field_tunneling_min = CSecondVPParams::optic_field_tunneling_min;
constexpr auto field_tunneling_max = CSecondVPParams::optic_field_tunneling_max;
constexpr auto field_tracking_speed = CSecondVPParams::optic_field_tracking_speed;
constexpr auto field_tracking_accel = CSecondVPParams::optic_field_tracking_accel;
constexpr auto field_tracking_limit = CSecondVPParams::optic_field_tracking_limit;
constexpr auto field_eye_relief_low = CSecondVPParams::optic_field_eye_relief_low;
constexpr auto field_eye_relief_high = CSecondVPParams::optic_field_eye_relief_high;
constexpr auto field_exit_pupil_low = CSecondVPParams::optic_field_exit_pupil_low;
constexpr auto field_exit_pupil_high = CSecondVPParams::optic_field_exit_pupil_high;
constexpr auto field_pupil_parity = CSecondVPParams::optic_field_pupil_parity;
constexpr auto field_pupil_field_low = CSecondVPParams::optic_field_pupil_field_low;
constexpr auto field_pupil_field_high = CSecondVPParams::optic_field_pupil_field_high;
constexpr auto field_transmission = CSecondVPParams::optic_field_transmission;
constexpr auto field_twilight_strength = CSecondVPParams::optic_field_twilight_strength;
constexpr auto field_physical_min = CSecondVPParams::optic_field_physical_min;
constexpr auto field_physical_max = CSecondVPParams::optic_field_physical_max;
constexpr auto field_eye_coupling = CSecondVPParams::optic_field_eye_coupling;
constexpr auto field_reticle_illum = CSecondVPParams::optic_field_reticle_illum;
constexpr auto field_magnification_mode = CSecondVPParams::optic_field_magnification_mode;
constexpr auto field_magnifications = CSecondVPParams::optic_field_magnifications;
constexpr auto field_mod_lane = CSecondVPParams::optic_field_mod_lane;
constexpr auto field_sources = CSecondVPParams::optic_field_sources;
constexpr auto field_count = CSecondVPParams::optic_field_count;

const auto& s_fields = CSecondVPParams::OpticFieldDescriptors();

u32 svp_source_index(EFieldId id)
{
	return id >= field_reticle_type && id <= field_mod_lane
		? static_cast<u32>(id - field_reticle_type) : u32(-1);
}

struct SParseError
{
	string64 code = {};
	string128 path = {};
	string256 message = {};
	string128 expected = {};
	string128 actual = {};
};

struct SParsedConfig
{
	CSecondVPParams::OpticConfig config;
	bool present[field_count] = {};
};

class SLuaStackGuard
{
public:
	explicit SLuaStackGuard(lua_State* state) : m_state(state), m_top(lua_gettop(state)) {}
	~SLuaStackGuard() { lua_settop(m_state, m_top); }

private:
	lua_State* m_state;
	int m_top;
};

template <size_t N>
void svp_copy_text(char (&destination)[N], LPCSTR source)
{
	const LPCSTR value = source ? source : "";
	const size_t source_length = xr_strlen(value);
	const size_t length = source_length < N - 1 ? source_length : N - 1;
	memcpy(destination, value, length);
	destination[length] = 0;
}

void svp_error(SParseError& error, LPCSTR code, LPCSTR path, LPCSTR message,
	LPCSTR expected = nullptr, LPCSTR actual = nullptr)
{
	svp_copy_text(error.code, code ? code : "invalid");
	svp_copy_text(error.path, path ? path : "table");
	svp_copy_text(error.message, message ? message : "invalid value");
	svp_copy_text(error.expected, expected);
	svp_copy_text(error.actual, actual);
}

const SFieldDescriptor* svp_descriptor(LPCSTR key, size_t length)
{
	for (const SFieldDescriptor& field : s_fields)
		if (xr_strlen(field.name) == length && !memcmp(field.name, key, length))
			return &field;
	return nullptr;
}

LPCSTR svp_type_name(EFieldType type)
{
	return CSecondVPParams::OpticFieldTypeName(type);
}

LPCSTR svp_schema_hash()
{
	return CSecondVPParams::OpticSchemaHash();
}

int svp_raw_field(lua_State* state, int table_index, LPCSTR key)
{
	lua_pushstring(state, key);
	lua_rawget(state, table_index);
	return lua_gettop(state);
}

bool svp_no_metatable(lua_State* state, int table_index, LPCSTR path, SParseError& error)
{
	if (!lua_getmetatable(state, table_index))
		return true;
	lua_pop(state, 1);
	svp_error(error, "metatable", path, "metatables are not accepted");
	return false;
}

bool svp_validate_top(lua_State* state, int table_index, bool partial,
	SParsedConfig& parsed, SParseError& error)
{
	if (!svp_no_metatable(state, table_index, "table", error))
		return false;

	lua_pushnil(state);
	while (lua_next(state, table_index) != 0)
	{
		if (lua_type(state, -2) != LUA_TSTRING)
		{
			svp_error(error, "invalid_key", "table", "top level keys must be strings");
			return false;
		}
		size_t length = 0;
		LPCSTR key = lua_tolstring(state, -2, &length);
		const SFieldDescriptor* field = svp_descriptor(key, length);
		if (!field)
		{
			string128 path = {};
			const size_t count = _min(length, sizeof(path) - 1);
			memcpy(path, key, count);
			svp_error(error, "unknown_field", path, "unknown optic field");
			return false;
		}
		if (partial && !field->registrable)
		{
			svp_error(error, "not_registrable", field->name, "field is not provider registrable");
			return false;
		}
		parsed.present[field->id] = true;
		lua_pop(state, 1);
	}

	if (!partial)
		for (const SFieldDescriptor& field : s_fields)
			if (field.required && !parsed.present[field.id])
			{
				svp_error(error, "missing_field", field.name, "required field is missing");
				return false;
			}
	return true;
}

bool svp_read_number(lua_State* state, int table_index, const SFieldDescriptor& field,
	double& value, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, field.name);
	if (lua_type(state, value_index) != LUA_TNUMBER)
	{
		svp_error(error, "type", field.name, "numeric value required", svp_type_name(field.type),
			lua_typename(state, lua_type(state, value_index)));
		return false;
	}
	value = static_cast<double>(lua_tonumber(state, value_index));
	lua_pop(state, 1);
	if (field.finite && !std::isfinite(value))
	{
		svp_error(error, "finite", field.name, "finite numeric value required");
		return false;
	}
	if (field.type == field_integer && std::floor(value) != value)
	{
		svp_error(error, "integer", field.name, "integer value required");
		return false;
	}
	const bool below = field.minimum_exclusive ? value <= field.minimum : value < field.minimum;
	const bool above = field.maximum_exclusive ? value >= field.maximum : value > field.maximum;
	if (!(field.allow_zero && value == 0.0) && (below || above))
	{
		string64 expected = {};
		xr_sprintf(expected, "%s%c%.6g,%.6g%c", field.allow_zero ? "0|" : "",
			field.minimum_exclusive ? '(' : '[', field.minimum, field.maximum,
			field.maximum_exclusive ? ')' : ']');
		string64 actual = {};
		xr_sprintf(actual, "%.9g", value);
		svp_error(error, "range", field.name, "numeric value is outside its range",
			expected, actual);
		return false;
	}
	return true;
}

bool svp_read_bool(lua_State* state, int table_index, const SFieldDescriptor& field,
	bool& value, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, field.name);
	if (lua_type(state, value_index) != LUA_TBOOLEAN)
	{
		svp_error(error, "type", field.name, "boolean value required", "boolean",
			lua_typename(state, lua_type(state, value_index)));
		return false;
	}
	value = lua_toboolean(state, value_index) != 0;
	lua_pop(state, 1);
	return true;
}

bool svp_read_string(lua_State* state, int table_index, const SFieldDescriptor& field,
	LPSTR destination, size_t capacity, SParseError& error)
{
	if (capacity != field.string_capacity)
	{
		svp_error(error, "internal", field.name, "string storage does not match its descriptor");
		return false;
	}
	const int value_index = svp_raw_field(state, table_index, field.name);
	if (lua_type(state, value_index) != LUA_TSTRING)
	{
		svp_error(error, "type", field.name, "string value required", "string",
			lua_typename(state, lua_type(state, value_index)));
		return false;
	}
	size_t length = 0;
	LPCSTR value = lua_tolstring(state, value_index, &length);
	if (!length || length >= capacity || memchr(value, 0, length))
	{
		svp_error(error, "string", field.name, "non empty bounded string required");
		return false;
	}
	memcpy(destination, value, length);
	destination[length] = 0;
	lua_pop(state, 1);
	return true;
}

bool svp_exact_map(lua_State* state, int table_index, LPCSTR path,
	const SObjectMemberDescriptor* members, size_t member_count, SParseError& error)
{
	if (!svp_no_metatable(state, table_index, path, error))
		return false;
	if (!members || !member_count || member_count > 16)
	{
		svp_error(error, "internal", path, "object descriptor is invalid");
		return false;
	}
	bool seen[16] = {};
	lua_pushnil(state);
	while (lua_next(state, table_index) != 0)
	{
		if (lua_type(state, -2) != LUA_TSTRING)
		{
			svp_error(error, "invalid_key", path, "object keys must be strings");
			return false;
		}
		size_t length = 0;
		LPCSTR key = lua_tolstring(state, -2, &length);
		size_t found = member_count;
		for (size_t i = 0; i < member_count; ++i)
			if (xr_strlen(members[i].name) == length && !memcmp(members[i].name, key, length))
			{
				found = i;
				break;
			}
		if (found == member_count)
		{
			svp_error(error, "unknown_field", path, "unknown object field");
			return false;
		}
		seen[found] = true;
		lua_pop(state, 1);
	}
	for (size_t i = 0; i < member_count; ++i)
		if (!seen[i])
		{
			string128 child = {};
			xr_sprintf(child, "%s.%s", path, members[i].name);
			svp_error(error, "missing_field", child, "required object field is missing");
			return false;
		}
	return true;
}

bool svp_read_object(lua_State* state, int table_index, const SFieldDescriptor& field,
	float* const* values, u32 value_count, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, field.name);
	if (lua_type(state, value_index) != LUA_TTABLE)
	{
		svp_error(error, "type", field.name, "numeric object required");
		return false;
	}
	if (value_count != field.member_count)
	{
		svp_error(error, "internal", field.name, "object storage does not match its descriptor");
		return false;
	}
	if (!svp_exact_map(state, value_index, field.name, field.members, field.member_count, error))
		return false;
	for (u32 i = 0; i < field.member_count; ++i)
	{
		const SObjectMemberDescriptor& member = field.members[i];
		const int child = svp_raw_field(state, value_index, member.name);
		if (lua_type(state, child) != LUA_TNUMBER)
		{
			string64 path = {};
			xr_sprintf(path, "%s.%s", field.name, member.name);
			svp_error(error, "type", path, "numeric value required");
			return false;
		}
		const double number = static_cast<double>(lua_tonumber(state, child));
		lua_pop(state, 1);
		if (member.finite && !std::isfinite(number))
		{
			string64 path = {};
			xr_sprintf(path, "%s.%s", field.name, member.name);
			svp_error(error, "finite", path, "finite numeric value required");
			return false;
		}
		if (member.has_range)
		{
			const bool below = member.minimum_exclusive
				? number <= member.minimum : number < member.minimum;
			const bool above = member.maximum_exclusive
				? number >= member.maximum : number > member.maximum;
			if (below || above)
			{
				string64 path = {};
				xr_sprintf(path, "%s.%s", field.name, member.name);
				svp_error(error, "range", path, "object value is outside its range");
				return false;
			}
		}
		const float normalized = static_cast<float>(number);
		if (!_valid(normalized) || (number != 0.0 && normalized == 0.f))
		{
			string64 path = {};
			xr_sprintf(path, "%s.%s", field.name, member.name);
			svp_error(error, "representation", path, "value is not representable by the engine");
			return false;
		}
		*values[i] = normalized;
	}
	lua_pop(state, 1);
	return true;
}

bool svp_read_objective(lua_State* state, int table_index,
	CSecondVPParams::OpticConfig& config, SParseError& error)
{
	const SFieldDescriptor& field = s_fields[field_objective_offset];
	float* values[] =
	{
		&config.objective_offset.x, &config.objective_offset.y,
		&config.objective_offset.z, &config.objective_offset.w
	};
	if (!svp_read_object(state, table_index, field, values, _countof(values), error))
		return false;
	config.has_objective_offset = true;
	return true;
}

bool svp_read_lane(lua_State* state, int table_index,
	CSecondVPParams::OpticConfig& config, SParseError& error)
{
	const SFieldDescriptor& field = s_fields[field_mod_lane];
	float* values[] =
	{
		&config.mod_lane.x, &config.mod_lane.y,
		&config.mod_lane.z, &config.mod_lane.w
	};
	if (!svp_read_object(state, table_index, field, values, _countof(values), error))
		return false;
	config.has_mod_lane = true;
	return true;
}

bool svp_read_mode(lua_State* state, int table_index,
	CSecondVPParams::OpticConfig& config, SParseError& error)
{
	const SFieldDescriptor& field = s_fields[field_magnification_mode];
	if (!field.enum_values || field.enum_value_count != 3)
	{
		svp_error(error, "internal", field.name, "enum descriptor is invalid");
		return false;
	}
	const int value_index = svp_raw_field(state, table_index, field.name);
	if (lua_type(state, value_index) != LUA_TSTRING)
	{
		svp_error(error, "type", field.name, "magnification mode string required");
		return false;
	}
	size_t length = 0;
	LPCSTR value = lua_tolstring(state, value_index, &length);
	u32 selected = field.enum_value_count;
	for (u32 i = 0; i < field.enum_value_count; ++i)
		if (length == xr_strlen(field.enum_values[i]) &&
			!memcmp(value, field.enum_values[i], length))
		{
			selected = i;
			break;
		}
	if (selected == field.enum_value_count)
	{
		string128 expected = {};
		for (u32 i = 0; i < field.enum_value_count; ++i)
		{
			if (i)
				xr_strcat(expected, "|");
			xr_strcat(expected, field.enum_values[i]);
		}
		svp_error(error, "enum", field.name, "unknown magnification mode",
			expected, value);
		return false;
	}
	config.magnifications.mode = static_cast<CSecondVPParams::EOpticMagnificationMode>(
		CSecondVPParams::optic_magnification_fixed + selected);
	lua_pop(state, 1);
	return true;
}

bool svp_read_magnifications(lua_State* state, int table_index,
	CSecondVPParams::OpticConfig& config, SParseError& error)
{
	const SFieldDescriptor& field = s_fields[field_magnifications];
	if (field.array_max > _countof(config.magnifications.values))
	{
		svp_error(error, "internal", field.name, "array descriptor exceeds native storage");
		return false;
	}
	const int value_index = svp_raw_field(state, table_index, field.name);
	if (lua_type(state, value_index) != LUA_TTABLE)
	{
		svp_error(error, "type", field.name, "numeric array required");
		return false;
	}
	if (!svp_no_metatable(state, value_index, field.name, error))
		return false;
	bool seen[16] = {};
	u32 count = 0;
	u32 maximum = 0;
	lua_pushnil(state);
	while (lua_next(state, value_index) != 0)
	{
		if (lua_type(state, -2) != LUA_TNUMBER)
		{
			svp_error(error, "array_key", field.name, "array keys must be numeric");
			return false;
		}
		const double key = static_cast<double>(lua_tonumber(state, -2));
		if (!std::isfinite(key) || std::floor(key) != key ||
			key < 1.0 || key > field.array_max)
		{
			string64 expected = {};
			xr_sprintf(expected, "integer 1..%u", field.array_max);
			svp_error(error, "array_key", field.name, "array index is outside its range",
				expected);
			return false;
		}
		const u32 index = static_cast<u32>(key) - 1;
		if (seen[index])
		{
			svp_error(error, "array_key", field.name, "duplicate array index");
			return false;
		}
		if (lua_type(state, -1) != LUA_TNUMBER)
		{
			svp_error(error, "type", field.name, "array values must be numeric");
			return false;
		}
		const double number = static_cast<double>(lua_tonumber(state, -1));
		const bool below = field.minimum_exclusive
			? number <= field.minimum : number < field.minimum;
		const bool above = field.maximum_exclusive
			? number >= field.maximum : number > field.maximum;
		if ((field.finite && !std::isfinite(number)) || below || above)
		{
			svp_error(error, "range", field.name, "magnification is outside its range");
			return false;
		}
		const float normalized = static_cast<float>(number);
		if (!_valid(normalized) || normalized <= 0.f)
		{
			svp_error(error, "representation", field.name,
				"magnification is not representable by the engine");
			return false;
		}
		config.magnifications.values[index] = normalized;
		seen[index] = true;
		++count;
		maximum = _max(maximum, index + 1);
		lua_pop(state, 1);
	}
	if (count < field.array_min || count > field.array_max || count != maximum)
	{
		svp_error(error, "array_shape", field.name, "array must be contiguous from index 1");
		return false;
	}
	for (u32 i = 0; i < count; ++i)
		if (!seen[i] || (field.ordered && i &&
			!(config.magnifications.values[i] > config.magnifications.values[i - 1])))
		{
			svp_error(error, "array_order", field.name, "magnifications must be strictly increasing");
			return false;
		}
	config.magnifications.count = static_cast<u8>(count);
	lua_pop(state, 1);
	return true;
}

template <typename T>
auto& svp_float_member(T& config, EFieldId id)
{
	switch (id)
	{
	case field_objective_mm: return config.objective_mm;
	case field_middle_grey: return config.middle_grey;
	case field_adapt_speed: return config.adapt_speed;
	case field_convergence_limit_m: return config.convergence_limit_m;
	case field_tunneling_parallax: return config.tunneling_parallax;
	case field_tunneling_min: return config.tunneling_min;
	case field_tunneling_max: return config.tunneling_max;
	case field_tracking_speed: return config.tracking_speed;
	case field_tracking_accel: return config.tracking_accel_mm_s2;
	case field_tracking_limit: return config.tracking_limit_mm;
	case field_eye_relief_low: return config.eye_relief_low_mm;
	case field_eye_relief_high: return config.eye_relief_high_mm;
	case field_exit_pupil_low: return config.exit_pupil_low_mm;
	case field_exit_pupil_high: return config.exit_pupil_high_mm;
	case field_pupil_parity: return config.pupil_parity;
	case field_pupil_field_low: return config.pupil_field_low;
	case field_pupil_field_high: return config.pupil_field_high;
	case field_transmission: return config.transmission;
	case field_twilight_strength: return config.twilight_strength;
	case field_physical_min: return config.physical_min;
	case field_physical_max: return config.physical_max;
	case field_reticle_illum: return config.reticle_illum;
	default: NODEFAULT; return config.middle_grey;
	}
}

bool svp_parse_values(lua_State* state, int table_index,
	SParsedConfig& parsed, SParseError& error)
{
	CSecondVPParams::OpticConfig& config = parsed.config;
	for (const SFieldDescriptor& field : s_fields)
	{
		if (!parsed.present[field.id] || field.id == field_sources)
			continue;
		double number = 0.0;
		switch (field.type)
		{
		case field_integer:
			if (!svp_read_number(state, table_index, field, number, error))
				return false;
			switch (field.id)
			{
			case field_context_token: config.context_token = static_cast<u32>(number); break;
			case field_weapon_id: config.weapon_id = static_cast<u32>(number); break;
			case field_zoom_type: config.zoom_type = static_cast<u8>(number); break;
			case field_reticle_type: config.reticle_type = static_cast<u8>(number); break;
			default: break;
			}
			break;
		case field_number:
			if (!svp_read_number(state, table_index, field, number, error))
				return false;
			{
				const float normalized = static_cast<float>(number);
				if (!_valid(normalized) || (number != 0.0 && normalized == 0.f))
				{
					svp_error(error, "representation", field.name,
						"value is not representable by the engine");
					return false;
				}
				svp_float_member(config, field.id) = normalized;
			}
			if (field.id == field_objective_mm)
				config.has_objective_mm = true;
			break;
		case field_boolean:
			if (field.id == field_hybrid_reflex)
			{
				if (!svp_read_bool(state, table_index, field, config.hybrid_reflex, error))
					return false;
				config.has_hybrid_reflex = true;
			}
			else if (!svp_read_bool(state, table_index, field, config.eye_coupling, error))
				return false;
			break;
		case field_string:
			switch (field.id)
			{
			case field_context:
				if (!svp_read_string(state, table_index, field, config.context, sizeof(config.context), error)) return false;
				break;
			case field_weapon:
				if (!svp_read_string(state, table_index, field, config.weapon, sizeof(config.weapon), error)) return false;
				break;
			case field_scope:
				if (!svp_read_string(state, table_index, field, config.scope, sizeof(config.scope), error)) return false;
				break;
			case field_diagnostic_scope:
				if (!svp_read_string(state, table_index, field, config.diagnostic_scope, sizeof(config.diagnostic_scope), error)) return false;
				break;
			case field_identity_source:
				if (!svp_read_string(state, table_index, field, config.identity_source, sizeof(config.identity_source), error)) return false;
				break;
			case field_profile_id:
				if (!svp_read_string(state, table_index, field, config.profile_id, sizeof(config.profile_id), error)) return false;
				break;
			case field_spec_section:
				if (!svp_read_string(state, table_index, field, config.spec_section, sizeof(config.spec_section), error)) return false;
				break;
			case field_model:
				if (!svp_read_string(state, table_index, field, config.model, sizeof(config.model), error)) return false;
				break;
			case field_binding:
				if (!svp_read_string(state, table_index, field, config.binding, sizeof(config.binding), error)) return false;
				break;
			case field_binding_section:
				if (!svp_read_string(state, table_index, field, config.binding_section, sizeof(config.binding_section), error)) return false;
				break;
			default: break;
			}
			break;
		case field_objective:
			if (!svp_read_objective(state, table_index, config, error)) return false;
			break;
		case field_mode:
			if (!svp_read_mode(state, table_index, config, error)) return false;
			break;
		case type_magnifications:
			if (!svp_read_magnifications(state, table_index, config, error)) return false;
			break;
		case field_lane:
			if (!svp_read_lane(state, table_index, config, error)) return false;
			break;
		default: break;
		}
	}

	if (parsed.present[field_tunneling_min] && parsed.present[field_tunneling_max] &&
		config.tunneling_min > config.tunneling_max)
	{
		svp_error(error, "endpoint_order", "tunneling_min", "minimum exceeds maximum");
		return false;
	}
	if (parsed.present[field_physical_min] != parsed.present[field_physical_max])
	{
		svp_error(error, "paired_field", "physical_min", "physical endpoints must be supplied together");
		return false;
	}
	if (parsed.present[field_physical_min])
	{
		if (config.physical_min > config.physical_max)
		{
			svp_error(error, "endpoint_order", "physical_min", "minimum exceeds maximum");
			return false;
		}
		config.has_physical_range = true;
	}
	if (parsed.present[field_magnification_mode] != parsed.present[field_magnifications])
	{
		svp_error(error, "paired_field", "magnification_mode", "mode and magnifications must be supplied together");
		return false;
	}
	if (parsed.present[field_magnification_mode])
	{
		const u32 count = config.magnifications.count;
		const auto mode = config.magnifications.mode;
		const bool valid = (mode == CSecondVPParams::optic_magnification_fixed && count == 1) ||
			(mode == CSecondVPParams::optic_magnification_continuous && count == 2) ||
			(mode == CSecondVPParams::optic_magnification_detent && count >= 2 && count <= 16);
		if (!valid)
		{
			svp_error(error, "array_length", "magnifications", "array length does not match its mode");
			return false;
		}
	}
	return true;
}

bool svp_parse_sources(lua_State* state, int table_index, SParsedConfig& parsed,
	SParseError& error)
{
	const SFieldDescriptor& source_field = s_fields[field_sources];
	if (!source_field.element_type || xr_strcmp(source_field.element_type, "string") ||
		source_field.element_string_capacity != sizeof(parsed.config.source[0]))
	{
		svp_error(error, "internal", source_field.name, "source descriptor is invalid");
		return false;
	}
	const int source_index = svp_raw_field(state, table_index, "sources");
	if (lua_type(state, source_index) != LUA_TTABLE)
	{
		svp_error(error, "type", "sources", "source map required");
		return false;
	}
	if (!svp_no_metatable(state, source_index, "sources", error))
		return false;
	bool seen[field_count] = {};
	lua_pushnil(state);
	while (lua_next(state, source_index) != 0)
	{
		if (lua_type(state, -2) != LUA_TSTRING)
		{
			svp_error(error, "invalid_key", "sources", "source keys must be strings");
			return false;
		}
		size_t length = 0;
		LPCSTR key = lua_tolstring(state, -2, &length);
		const SFieldDescriptor* field = svp_descriptor(key, length);
		if (!field || !field->source_required || !parsed.present[field->id])
		{
			svp_error(error, "source_set", "sources", "source names an unknown or omitted field");
			return false;
		}
		if (lua_type(state, -1) != LUA_TSTRING)
		{
			string128 path = {};
			xr_sprintf(path, "sources.%s", field->name);
			svp_error(error, "type", path, "source must be a string");
			return false;
		}
		size_t source_length = 0;
		LPCSTR source = lua_tolstring(state, -1, &source_length);
		if ((source_field.element_non_empty && !source_length) ||
			source_length >= source_field.element_string_capacity ||
			memchr(source, 0, source_length))
		{
			string128 path = {};
			xr_sprintf(path, "sources.%s", field->name);
			svp_error(error, "string", path, "source must be non empty and bounded");
			return false;
		}
		const u32 source_index_native = svp_source_index(field->id);
		if (source_index_native >= CSecondVPParams::optic_value_count)
		{
			svp_error(error, "internal", field->name, "source descriptor index is invalid");
			return false;
		}
		memcpy(parsed.config.source[source_index_native], source, source_length);
		parsed.config.source[source_index_native][source_length] = 0;
		seen[field->id] = true;
		lua_pop(state, 1);
	}
	for (const SFieldDescriptor& field : s_fields)
		if (field.source_required && parsed.present[field.id] && !seen[field.id])
		{
			svp_error(error, "missing_source", field.name, "matching source is required");
			return false;
		}
	lua_pop(state, 1);
	return true;
}

bool svp_parse(const luabind::object& table, bool partial, SParsedConfig& parsed,
	SParseError& error)
{
	if (!table.is_valid() || table.type() != LUA_TTABLE)
	{
		svp_error(error, "type", "table", "optic table required");
		return false;
	}
	lua_State* state = table.lua_state();
	SLuaStackGuard guard(state);
	table.pushvalue();
	const int table_index = lua_gettop(state);
	if (!svp_validate_top(state, table_index, partial, parsed, error) ||
		!svp_parse_values(state, table_index, parsed, error))
		return false;
	if (!partial && !svp_parse_sources(state, table_index, parsed, error))
		return false;
	return true;
}

luabind::object svp_objective_table(lua_State* state, const Fvector4& value)
{
	luabind::object table = luabind::newtable(state);
	table["x"] = value.x;
	table["y"] = value.y;
	table["z"] = value.z;
	table["radius"] = value.w;
	return table;
}

luabind::object svp_lane_table(lua_State* state, const Fvector4& value)
{
	luabind::object table = luabind::newtable(state);
	table["x"] = value.x;
	table["y"] = value.y;
	table["z"] = value.z;
	table["w"] = value.w;
	return table;
}

LPCSTR svp_mode_name(CSecondVPParams::EOpticMagnificationMode mode)
{
	switch (mode)
	{
	case CSecondVPParams::optic_magnification_fixed: return "fixed";
	case CSecondVPParams::optic_magnification_continuous: return "continuous";
	case CSecondVPParams::optic_magnification_detent: return "detent";
	default: return "none";
	}
}

luabind::object svp_normalized_table(const SParsedConfig& parsed, bool partial)
{
	lua_State* state = svp_lua_state();
	luabind::object table = luabind::newtable(state);
	const auto& config = parsed.config;
	for (const SFieldDescriptor& field : s_fields)
	{
		if (!parsed.present[field.id] || field.id == field_sources)
			continue;
		switch (field.id)
		{
		case field_schema_version: table[field.name] = static_cast<int>(CSecondVPParams::optic_schema_version); break;
		case field_context_token: table[field.name] = config.context_token; break;
		case field_context: table[field.name] = config.context; break;
		case field_weapon: table[field.name] = config.weapon; break;
		case field_weapon_id: table[field.name] = config.weapon_id; break;
		case field_scope: table[field.name] = config.scope; break;
		case field_diagnostic_scope: table[field.name] = config.diagnostic_scope; break;
		case field_identity_source: table[field.name] = config.identity_source; break;
		case field_zoom_type: table[field.name] = static_cast<int>(config.zoom_type); break;
		case field_profile_id: table[field.name] = config.profile_id; break;
		case field_spec_section: table[field.name] = config.spec_section; break;
		case field_model: table[field.name] = config.model; break;
		case field_binding: table[field.name] = config.binding; break;
		case field_binding_section: table[field.name] = config.binding_section; break;
		case field_reticle_type: table[field.name] = static_cast<int>(config.reticle_type); break;
		case field_hybrid_reflex: table[field.name] = config.hybrid_reflex; break;
		case field_objective_offset: table[field.name] = svp_objective_table(state, config.objective_offset); break;
		case field_eye_coupling: table[field.name] = config.eye_coupling; break;
		case field_magnification_mode: table[field.name] = svp_mode_name(config.magnifications.mode); break;
		case field_magnifications:
		{
			luabind::object values = luabind::newtable(state);
			for (u32 i = 0; i < config.magnifications.count; ++i)
				values[i + 1] = config.magnifications.values[i];
			table[field.name] = values;
			break;
		}
		case field_mod_lane: table[field.name] = svp_lane_table(state, config.mod_lane); break;
		default:
			if (field.type == field_number)
				table[field.name] = svp_float_member(config, field.id);
			break;
		}
	}
	if (!partial)
	{
		luabind::object sources = luabind::newtable(state);
		for (const SFieldDescriptor& field : s_fields)
			if (field.source_required && parsed.present[field.id])
				sources[field.name] = config.source[svp_source_index(field.id)];
		table["sources"] = sources;
	}
	return table;
}

luabind::object svp_result_error(const SParseError& error)
{
	lua_State* state = svp_lua_state();
	luabind::object result = luabind::newtable(state);
	result["ok"] = false;
	result["code"] = error.code;
	result["path"] = error.path;
	result["message"] = error.message;
	if (error.expected[0])
		result["expected"] = error.expected;
	if (error.actual[0])
		result["actual"] = error.actual;
	return result;
}

luabind::object svp_result_error(LPCSTR code, LPCSTR message, LPCSTR path = nullptr)
{
	SParseError error;
	svp_error(error, code, path ? path : "table", message);
	return svp_result_error(error);
}

luabind::object svp_result_success(const SParsedConfig& parsed, bool partial)
{
	lua_State* state = svp_lua_state();
	luabind::object result = luabind::newtable(state);
	result["ok"] = true;
	result["normalized"] = svp_normalized_table(parsed, partial);
	return result;
}

luabind::object svp_apply_success(const CSecondVPParams::OpticConfig& accepted)
{
	lua_State* state = svp_lua_state();
	luabind::object result = luabind::newtable(state);
	string32 fingerprint = {};
	xr_sprintf(fingerprint, "%016llx", accepted.fingerprint);
	result["ok"] = true;
	result["context_token"] = accepted.context_token;
	result["config_generation"] = accepted.generation;
	result["profile_fingerprint"] = fingerprint;
	return result;
}

luabind::object svp_supported_pairs(lua_State* state)
{
	luabind::object pairs = luabind::newtable(state);
	luabind::object pair = luabind::newtable(state);
	pair["api"] = static_cast<int>(CSecondVPParams::optic_api_version);
	pair["schema"] = static_cast<int>(CSecondVPParams::optic_schema_version);
	pairs[1] = pair;
	return pairs;
}

constexpr LPCSTR s_optic_capabilities[] =
{
	"api_info",
	"field_descriptors",
	"structured_validation",
	"typed_transport",
	"hybrid_reflex",
	"profile_inspector"
};

bool svp_capability(LPCSTR capability)
{
	if (!capability)
		return false;
	for (LPCSTR value : s_optic_capabilities)
		if (!xr_strcmp(capability, value))
			return true;
	return false;
}

luabind::object svp_capabilities(lua_State* state)
{
	luabind::object values = luabind::newtable(state);
	for (LPCSTR value : s_optic_capabilities)
		values[value] = true;
	return values;
}

bool svp_bounded_text(LPCSTR text, size_t capacity, bool non_empty)
{
	return text && (!non_empty || text[0]) && xr_strlen(text) < capacity;
}

SParsedConfig svp_config_view(const CSecondVPParams::OpticConfig& config)
{
	SParsedConfig parsed;
	parsed.config = config;
	if (!config.valid)
		return parsed;
	for (const SFieldDescriptor& field : s_fields)
		parsed.present[field.id] = field.required;
	parsed.present[field_scope] = config.scope[0] != 0;
	parsed.present[field_diagnostic_scope] = config.diagnostic_scope[0] != 0;
	parsed.present[field_spec_section] = config.spec_section[0] != 0;
	parsed.present[field_model] = config.model[0] != 0;
	parsed.present[field_binding] = config.binding[0] != 0;
	parsed.present[field_binding_section] = config.binding_section[0] != 0;
	parsed.present[field_hybrid_reflex] = config.has_hybrid_reflex;
	parsed.present[field_objective_offset] = config.has_objective_offset;
	parsed.present[field_objective_mm] = config.has_objective_mm;
	parsed.present[field_physical_min] = config.has_physical_range;
	parsed.present[field_physical_max] = config.has_physical_range;
	parsed.present[field_magnification_mode] = config.magnifications.count != 0;
	parsed.present[field_magnifications] = config.magnifications.count != 0;
	parsed.present[field_mod_lane] = config.has_mod_lane;
	return parsed;
}
}

int svp_optic_api_version()
{
	return static_cast<int>(CSecondVPParams::optic_api_version);
}

luabind::object svp_optic_api_info()
{
	lua_State* state = svp_lua_state();
	luabind::object result = luabind::newtable(state);
	result["api_min"] = static_cast<int>(CSecondVPParams::optic_api_min);
	result["api_max"] = static_cast<int>(CSecondVPParams::optic_api_max);
	result["schema_min"] = static_cast<int>(CSecondVPParams::optic_schema_min);
	result["schema_max"] = static_cast<int>(CSecondVPParams::optic_schema_max);
	result["supported_pairs"] = svp_supported_pairs(state);
	result["schema_hash"] = svp_schema_hash();
	result["capabilities"] = svp_capabilities(state);
	return result;
}

bool svp_optic_api_has_capability(LPCSTR capability)
{
	return svp_capability(capability);
}

luabind::object svp_optic_api_connect(double api, double schema)
{
	lua_State* state = svp_lua_state();
	luabind::object result = luabind::newtable(state);
	if (std::isfinite(api) && std::floor(api) == api &&
		std::isfinite(schema) && std::floor(schema) == schema &&
		api == CSecondVPParams::optic_api_version &&
		schema == CSecondVPParams::optic_schema_version &&
		svp_viewport().ConnectOpticApi(
			static_cast<u32>(api), static_cast<u32>(schema)))
	{
		result["ok"] = true;
		result["api"] = static_cast<int>(api);
		result["schema"] = static_cast<int>(schema);
		result["schema_hash"] = svp_schema_hash();
		return result;
	}
	result["ok"] = false;
	result["code"] = "unsupported_version";
	result["message"] = "unsupported API and schema pair";
	result["api_min"] = static_cast<int>(CSecondVPParams::optic_api_min);
	result["api_max"] = static_cast<int>(CSecondVPParams::optic_api_max);
	result["schema_min"] = static_cast<int>(CSecondVPParams::optic_schema_min);
	result["schema_max"] = static_cast<int>(CSecondVPParams::optic_schema_max);
	result["supported_pairs"] = svp_supported_pairs(state);
	return result;
}

luabind::object svp_optic_api_describe()
{
	lua_State* state = svp_lua_state();
	luabind::object result = luabind::newtable(state);
	luabind::object fields = luabind::newtable(state);
	for (const SFieldDescriptor& field : s_fields)
	{
		luabind::object value = luabind::newtable(state);
		value["index"] = static_cast<int>(field.id) + 1;
		value["type"] = svp_type_name(field.type);
		value["required"] = field.required;
		value["registrable"] = field.registrable;
		value["source_required"] = field.source_required;
		if (field.type == field_integer || field.type == field_number)
		{
			value["finite"] = field.finite;
			value["minimum"] = field.minimum;
			value["maximum"] = field.maximum;
			value["minimum_exclusive"] = field.minimum_exclusive;
			value["maximum_exclusive"] = field.maximum_exclusive;
			value["integer"] = field.type == field_integer;
			value["allow_zero"] = field.allow_zero;
		}
		if (field.type == field_string)
		{
			value["maximum_length"] = static_cast<int>(field.string_capacity - 1);
			value["non_empty"] = field.non_empty;
		}
		if (field.array_max)
		{
			value["minimum_items"] = static_cast<int>(field.array_min);
			value["maximum_items"] = static_cast<int>(field.array_max);
			value["ordered"] = field.ordered;
			value["element_type"] = field.element_type;
			value["element_finite"] = field.finite;
			value["element_minimum"] = field.minimum;
			value["element_maximum"] = field.maximum;
			value["element_minimum_exclusive"] = field.minimum_exclusive;
			value["element_maximum_exclusive"] = field.maximum_exclusive;
		}
		if (field.member_count)
		{
			luabind::object keys = luabind::newtable(state);
			luabind::object members = luabind::newtable(state);
			for (u32 i = 0; i < field.member_count; ++i)
			{
				const SObjectMemberDescriptor& member = field.members[i];
				keys[i + 1] = member.name;
				luabind::object member_value = luabind::newtable(state);
				member_value["type"] = "number";
				member_value["finite"] = member.finite;
				if (member.has_range)
				{
					member_value["minimum"] = member.minimum;
					member_value["maximum"] = member.maximum;
					member_value["minimum_exclusive"] = member.minimum_exclusive;
					member_value["maximum_exclusive"] = member.maximum_exclusive;
				}
				members[member.name] = member_value;
			}
			value["allowed_keys"] = keys;
			value["members"] = members;
			value["vector_length"] = static_cast<int>(field.member_count);
		}
		if (field.enum_value_count)
		{
			luabind::object values = luabind::newtable(state);
			for (u32 i = 0; i < field.enum_value_count; ++i)
				values[i + 1] = field.enum_values[i];
			value["values"] = values;
		}
		if (field.type == type_sources)
		{
			value["value_type"] = field.element_type;
			value["value_non_empty"] = field.element_non_empty;
			value["value_maximum_length"] =
				static_cast<int>(field.element_string_capacity - 1);
		}
		if (field.constraint && field.constraint[0])
			value["constraint"] = field.constraint;
		fields[field.name] = value;
	}
	result["schema"] = static_cast<int>(CSecondVPParams::optic_schema_version);
	result["schema_hash"] = svp_schema_hash();
	result["fields"] = fields;
	return result;
}

luabind::object svp_validate_optic_fields(const luabind::object& table)
{
	SParsedConfig parsed;
	SParseError error;
	try
	{
		if (!svp_parse(table, true, parsed, error))
			return svp_result_error(error);
		return svp_result_success(parsed, true);
	}
	catch (...)
	{
		return svp_result_error("exception", "validation raised an exception");
	}
}

luabind::object svp_validate_optic_profile(const luabind::object& table)
{
	SParsedConfig parsed;
	SParseError error;
	try
	{
		if (!svp_parse(table, false, parsed, error))
			return svp_result_error(error);
		return svp_result_success(parsed, false);
	}
	catch (...)
	{
		return svp_result_error("exception", "validation raised an exception");
	}
}

u32 svp_optic_route_epoch()
{
	return svp_viewport().GetOpticRouteEpoch();
}

u32 svp_begin_optic_context(LPCSTR context, LPCSTR weapon, double weapon_id,
	LPCSTR scope, double zoom_type, LPCSTR identity_source, LPCSTR diagnostic_scope)
{
	if (!svp_transport_active() || svp_scope_mode() <= 0 ||
		!std::isfinite(weapon_id) || std::floor(weapon_id) != weapon_id ||
		weapon_id < 0 || weapon_id > u16(-1) ||
		!std::isfinite(zoom_type) || std::floor(zoom_type) != zoom_type ||
		zoom_type < 0 || zoom_type > u8(-1) ||
		!svp_bounded_text(context, sizeof(string256), true) ||
		!svp_bounded_text(weapon, sizeof(string128), true) ||
		!svp_bounded_text(scope, sizeof(string128), false) ||
		!svp_bounded_text(identity_source, sizeof(string64), true) ||
		!svp_bounded_text(diagnostic_scope, sizeof(string128), false))
		return 0;

	const u32 token = svp_viewport().BeginOpticContext(context, weapon,
		static_cast<u32>(weapon_id), scope, static_cast<u8>(zoom_type),
		identity_source, diagnostic_scope);
	SVP_CONFIG_LOG("[SVP-CONFIG] begin token=%u context=%s weapon_id=%d scope=%s diagnostic_scope=%s",
		token, context, static_cast<int>(weapon_id), scope, diagnostic_scope);
	return token;
}

luabind::object svp_apply_optic_profile(double context_token_value, const luabind::object& table)
{
	if (!svp_transport_active() || svp_scope_mode() <= 0)
		return svp_result_error("inactive", "typed optic transport is inactive");
	if (!std::isfinite(context_token_value) ||
		std::floor(context_token_value) != context_token_value ||
		context_token_value < 1.0 || context_token_value > u32(-1))
		return svp_result_error("stale_token", "context token is not active", "context_token");
	const u32 context_token = static_cast<u32>(context_token_value);

	SParsedConfig parsed;
	SParseError error;
	CSecondVPParams::OpticPublication publication;
	luabind::object success;
	luabind::object publication_failed;
	try
	{
		if (!svp_parse(table, false, parsed, error))
			return svp_result_error(error);
		if (parsed.config.context_token != context_token)
			return svp_result_error("stale_token", "apply token does not match the profile token",
				"context_token");

		CSecondVPParams::OpticConfig before;
		svp_viewport().ReadOpticConfig(before);
		if (before.context_token != context_token)
			return svp_result_error("stale_token", "context token is stale", "context_token");
		if (xr_strcmp(parsed.config.context, before.context) ||
			xr_strcmp(parsed.config.weapon, before.weapon) ||
			parsed.config.weapon_id != before.weapon_id ||
			xr_strcmp(parsed.config.scope, before.scope) ||
			xr_strcmp(parsed.config.identity_source, before.identity_source) ||
			xr_strcmp(parsed.config.diagnostic_scope, before.diagnostic_scope) ||
			parsed.config.zoom_type != before.zoom_type)
			return svp_result_error("identity_mismatch", "profile identity does not match the active context");
		if (!svp_viewport().PrepareOpticConfig(
			context_token, parsed.config, publication))
			return svp_result_error("publication_failed", "profile publication failed");
		success = svp_apply_success(publication.accepted);
		publication_failed =
			svp_result_error("publication_failed", "profile publication failed");
	}
	catch (const std::exception& exception)
	{
		SVP_CONFIG_LOG("![SVP-CONFIG] apply exception token=%u detail=%s", context_token, exception.what());
		return svp_result_error("exception", "profile apply raised an exception");
	}
	catch (...)
	{
		SVP_CONFIG_LOG("![SVP-CONFIG] apply exception token=%u", context_token);
		return svp_result_error("exception", "profile apply raised an exception");
	}

	if (!svp_viewport().PublishOpticConfig(context_token, publication))
		return publication_failed;
	const CSecondVPParams::OpticConfig& accepted = publication.accepted;
	if (accepted.generation != publication.base_generation)
		SVP_CONFIG_LOG("[SVP-CONFIG] publish token=%u gen=%u context=%s reticle=%u hybrid=%d/%d profile=%s spec=%s",
			accepted.context_token, accepted.generation, accepted.context,
			accepted.reticle_type, accepted.has_hybrid_reflex, accepted.hybrid_reflex,
			accepted.profile_id, accepted.spec_section);
	return success;
}

bool svp_clear_optic_profile(double context_token_value)
{
	if (!std::isfinite(context_token_value) ||
		std::floor(context_token_value) != context_token_value ||
		context_token_value < 1.0 || context_token_value > u32(-1))
		return false;
	return svp_viewport().ClearOpticConfig(static_cast<u32>(context_token_value));
}

luabind::object svp_current_optic_profile()
{
	lua_State* state = svp_lua_state();
	CSecondVPParams::OpticConfig config;
	if (!svp_viewport().ReadOpticConfig(config))
	{
		luabind::object result = luabind::newtable(state);
		result["valid"] = false;
		result["route"] = "none";
		return result;
	}
	SParsedConfig parsed = svp_config_view(config);
	luabind::object result = svp_normalized_table(parsed, false);
	result["valid"] = true;
	result["route"] = "typed";
	result["config_generation"] = config.generation;
	result["route_epoch"] = config.route_epoch;
	string32 fingerprint = {};
	xr_sprintf(fingerprint, "%016llx", config.fingerprint);
	result["profile_fingerprint"] = fingerprint;
	return result;
}
