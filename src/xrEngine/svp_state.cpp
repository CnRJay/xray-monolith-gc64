#if defined(SVP_TEST_CLIENT)
#include "../xrCore/xrCore.h"
#ifndef ENGINE_API
#define ENGINE_API
#endif
#include "svp_state.h"
#else
#include "stdafx.h"
#endif

void CSecondVPParams::PublishWeaponPose(const WeaponPoseSnapshot& pose)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_weapon_pose = pose;
}

bool CSecondVPParams::ReadWeaponPose(WeaponPoseSnapshot& pose) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	pose = m_weapon_pose;
	return pose.frame != u32(-1);
}

void CSecondVPParams::ClearWeaponPose()
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_weapon_pose = WeaponPoseSnapshot{};
}

void CSecondVPParams::PublishSight(const SightSnapshot& sight)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_sight = sight;
}

bool CSecondVPParams::ReadSight(SightSnapshot& sight) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	sight = m_sight;
	return sight.frame != u32(-1);
}

void CSecondVPParams::ClearSight()
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_sight = SightSnapshot{};
}

void CSecondVPParams::AppendFireTrace(const FireTrace& trace)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_fire_traces[m_fire_trace_head % 16] = trace;
	++m_fire_trace_head;
}

void CSecondVPParams::ReadFireTraces(FireTrace (&traces)[16]) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	for (u32 i = 0; i < 16; ++i)
		traces[i] = m_fire_traces[i];
}

namespace
{
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

constexpr SObjectMemberDescriptor s_objective_members[] =
{
	{ "x", true, true, -8.0, 64.0, false, false },
	{ "y", true, true, -8.0, 64.0, false, false },
	{ "z", true, true, 0.0, 64.0, true, false },
	{ "radius", true, true, 0.0, 64.0, true, false }
};

constexpr SObjectMemberDescriptor s_lane_members[] =
{
	{ "x", true, false, 0.0, 0.0, false, false },
	{ "y", true, false, 0.0, 0.0, false, false },
	{ "z", true, false, 0.0, 0.0, false, false },
	{ "w", true, false, 0.0, 0.0, false, false }
};

constexpr LPCSTR s_magnification_modes[] = { "fixed", "continuous", "detent" };

#define SVP_INT(id, name, req, reg, src, low, high) \
	{ id, name, field_integer, req, reg, src, low, high, 0, 0, 0, true, false, false, \
		false, false, false, nullptr, 0, nullptr, 0, nullptr, false, 0, "" }
#define SVP_NUM(id, name, req, reg, src, low, high, zero, exclusive, rule) \
	{ id, name, field_number, req, reg, src, low, high, 0, 0, 0, true, false, zero, \
		exclusive, false, false, nullptr, 0, nullptr, 0, nullptr, false, 0, rule }
#define SVP_BOOL(id, name, req, reg, src) \
	{ id, name, field_boolean, req, reg, src, 0, 0, 0, 0, 0, false, false, false, \
		false, false, false, nullptr, 0, nullptr, 0, nullptr, false, 0, "" }
#define SVP_STR(id, name, req, cap) \
	{ id, name, field_string, req, false, false, 0, 0, cap, 0, 0, false, false, false, \
		false, false, true, nullptr, 0, nullptr, 0, nullptr, false, 0, "" }
#define SVP_OBJECT(id, name, req, reg, src, fields) \
	{ id, name, field_objective, req, reg, src, 0, 0, 0, 0, 0, true, false, false, \
		false, false, false, fields, _countof(fields), nullptr, 0, nullptr, false, 0, "" }
#define SVP_LANE(id, name, req, reg, src, fields) \
	{ id, name, field_lane, req, reg, src, 0, 0, 0, 0, 0, true, false, false, \
		false, false, false, fields, _countof(fields), nullptr, 0, nullptr, false, 0, "" }
#define SVP_ENUM(id, name, req, reg, src, values, rule) \
	{ id, name, field_mode, req, reg, src, 0, 0, 0, 0, 0, false, false, false, \
		false, false, false, nullptr, 0, values, _countof(values), nullptr, false, 0, rule }
#define SVP_ARRAY(id, name, req, reg, src, amin, amax, low, high, rule) \
	{ id, name, type_magnifications, req, reg, src, low, high, 0, amin, amax, true, true, false, \
		true, false, false, nullptr, 0, nullptr, 0, "number", false, 0, rule }
#define SVP_MAP(id, name, req, value_type, value_non_empty, value_capacity, rule) \
	{ id, name, type_sources, req, false, false, 0, 0, 0, 0, 0, false, false, false, \
		false, false, false, nullptr, 0, nullptr, 0, value_type, value_non_empty, value_capacity, rule }

constexpr SFieldDescriptor s_optic_fields[] =
{
	SVP_INT(field_schema_version, "schema_version", true, false, false, 3, 3),
	SVP_INT(field_context_token, "context_token", true, false, false, 1, u32(-1)),
	SVP_STR(field_context, "context", true, sizeof(string256)),
	SVP_STR(field_weapon, "weapon", true, sizeof(string128)),
	SVP_INT(field_weapon_id, "weapon_id", true, false, false, 0, u16(-1)),
	SVP_STR(field_scope, "scope", false, sizeof(string128)),
	SVP_STR(field_diagnostic_scope, "diagnostic_scope", false, sizeof(string128)),
	SVP_STR(field_identity_source, "identity_source", true, sizeof(string64)),
	SVP_INT(field_zoom_type, "zoom_type", true, false, false, 0, u8(-1)),
	SVP_STR(field_profile_id, "profile_id", true, sizeof(string128)),
	SVP_STR(field_spec_section, "spec_section", false, sizeof(string128)),
	SVP_STR(field_model, "model", false, sizeof(string32)),
	SVP_STR(field_binding, "binding", false, sizeof(string32)),
	SVP_STR(field_binding_section, "binding_section", false, sizeof(string128)),
	SVP_INT(field_reticle_type, "reticle_type", true, true, true, 0, u8(-1)),
	SVP_BOOL(field_hybrid_reflex, "hybrid_reflex", false, true, true),
	SVP_OBJECT(field_objective_offset, "objective_offset", false, true, true, s_objective_members),
	SVP_NUM(field_objective_mm, "objective_mm", false, true, true, 0, 200, false, true, ""),
	SVP_NUM(field_middle_grey, "middle_grey", true, true, true, 0, 2, true, false, ""),
	SVP_NUM(field_adapt_speed, "adapt_speed", true, true, true, 0, 20, true, false, ""),
	SVP_NUM(field_convergence_limit_m, "convergence_limit_m", true, true, true, 2, 1000, true, false, ""),
	SVP_NUM(field_tunneling_parallax, "tunneling_parallax", true, true, true, 0, 0.15, true, false, ""),
	SVP_NUM(field_tunneling_min, "tunneling_min", true, true, true, 0, 1, true, false, "paired_order:tunneling_max"),
	SVP_NUM(field_tunneling_max, "tunneling_max", true, true, true, 0, 1, true, false, "paired_order:tunneling_min"),
	SVP_NUM(field_tracking_speed, "tracking_speed", true, true, true, 0.1, 30, false, false, ""),
	SVP_NUM(field_tracking_accel, "tracking_accel_mm_s2", true, true, true, 1, 500, false, false, ""),
	SVP_NUM(field_tracking_limit, "tracking_limit_mm", true, true, true, 0, 20, true, false, ""),
	SVP_NUM(field_eye_relief_low, "eye_relief_low_mm", true, true, true, 20, 150, false, false, ""),
	SVP_NUM(field_eye_relief_high, "eye_relief_high_mm", true, true, true, 20, 150, false, false, ""),
	SVP_NUM(field_exit_pupil_low, "exit_pupil_low_mm", true, true, true, 0, 100, true, false, ""),
	SVP_NUM(field_exit_pupil_high, "exit_pupil_high_mm", true, true, true, 0, 100, true, false, ""),
	SVP_NUM(field_pupil_parity, "pupil_parity", true, true, true, -1, 1, true, false, ""),
	SVP_NUM(field_pupil_field_low, "pupil_field_low", true, true, true, 0, 6, true, false, ""),
	SVP_NUM(field_pupil_field_high, "pupil_field_high", true, true, true, 0, 6, true, false, ""),
	SVP_NUM(field_transmission, "transmission", true, true, true, 0, 1, true, false, ""),
	SVP_NUM(field_twilight_strength, "twilight_strength", true, true, true, 0, 1, true, false, ""),
	SVP_NUM(field_physical_min, "physical_min", false, true, true, 0, 200, false, true, "optional_pair_order:physical_max"),
	SVP_NUM(field_physical_max, "physical_max", false, true, true, 0, 200, false, true, "optional_pair_order:physical_min"),
	SVP_BOOL(field_eye_coupling, "eye_coupling", true, true, true),
	SVP_NUM(field_reticle_illum, "reticle_illum", true, true, true, 0, 1, true, false, ""),
	SVP_ENUM(field_magnification_mode, "magnification_mode", false, true, true,
		s_magnification_modes, "optional_pair_mode_length:magnifications"),
	SVP_ARRAY(field_magnifications, "magnifications", false, true, true, 1, 16, 0, 200,
		"optional_pair_mode_length:magnification_mode"),
	SVP_LANE(field_mod_lane, "mod_lane", false, true, true, s_lane_members),
	SVP_MAP(field_sources, "sources", true, "string", true, sizeof(string256),
		"exact_present_source_required_fields")
};

#undef SVP_INT
#undef SVP_NUM
#undef SVP_BOOL
#undef SVP_STR
#undef SVP_OBJECT
#undef SVP_LANE
#undef SVP_ENUM
#undef SVP_ARRAY
#undef SVP_MAP

static_assert(_countof(s_optic_fields) == field_count);

constexpr bool svp_field_order_valid()
{
	for (u32 i = 0; i < _countof(s_optic_fields); ++i)
		if (s_optic_fields[i].id != static_cast<EFieldId>(i))
			return false;
	return true;
}

constexpr u32 svp_source_count()
{
	u32 count = 0;
	for (const SFieldDescriptor& field : s_optic_fields)
		if (field.source_required)
			++count;
	return count;
}

constexpr bool svp_source_layout_valid()
{
	u32 source = 0;
	for (const SFieldDescriptor& field : s_optic_fields)
	{
		if (field.source_required)
		{
			if (field.id < field_reticle_type || field.id > field_mod_lane ||
				static_cast<u32>(field.id - field_reticle_type) != source)
				return false;
			++source;
		}
		else if (field.id >= field_reticle_type && field.id <= field_mod_lane)
			return false;
	}
	return source == CSecondVPParams::optic_value_count;
}

static_assert(svp_field_order_valid());
static_assert(svp_source_count() == CSecondVPParams::optic_value_count);
static_assert(svp_source_layout_valid());
static_assert(field_reticle_type - field_reticle_type == CSecondVPParams::optic_reticle_type);
static_assert(field_hybrid_reflex - field_reticle_type == CSecondVPParams::optic_hybrid_reflex);
static_assert(field_objective_offset - field_reticle_type == CSecondVPParams::optic_objective_offset);
static_assert(field_objective_mm - field_reticle_type == CSecondVPParams::optic_objective_mm);
static_assert(field_middle_grey - field_reticle_type == CSecondVPParams::optic_middle_grey);
static_assert(field_adapt_speed - field_reticle_type == CSecondVPParams::optic_adapt_speed);
static_assert(field_convergence_limit_m - field_reticle_type == CSecondVPParams::optic_convergence_limit_m);
static_assert(field_tunneling_parallax - field_reticle_type == CSecondVPParams::optic_tunneling_parallax);
static_assert(field_tunneling_min - field_reticle_type == CSecondVPParams::optic_tunneling_min);
static_assert(field_tunneling_max - field_reticle_type == CSecondVPParams::optic_tunneling_max);
static_assert(field_tracking_speed - field_reticle_type == CSecondVPParams::optic_tracking_speed);
static_assert(field_tracking_accel - field_reticle_type == CSecondVPParams::optic_tracking_accel);
static_assert(field_tracking_limit - field_reticle_type == CSecondVPParams::optic_tracking_limit);
static_assert(field_eye_relief_low - field_reticle_type == CSecondVPParams::optic_eye_relief_low);
static_assert(field_eye_relief_high - field_reticle_type == CSecondVPParams::optic_eye_relief_high);
static_assert(field_exit_pupil_low - field_reticle_type == CSecondVPParams::optic_exit_pupil_low);
static_assert(field_exit_pupil_high - field_reticle_type == CSecondVPParams::optic_exit_pupil_high);
static_assert(field_pupil_parity - field_reticle_type == CSecondVPParams::optic_pupil_parity);
static_assert(field_pupil_field_low - field_reticle_type == CSecondVPParams::optic_pupil_field_low);
static_assert(field_pupil_field_high - field_reticle_type == CSecondVPParams::optic_pupil_field_high);
static_assert(field_transmission - field_reticle_type == CSecondVPParams::optic_transmission);
static_assert(field_twilight_strength - field_reticle_type == CSecondVPParams::optic_twilight_strength);
static_assert(field_physical_min - field_reticle_type == CSecondVPParams::optic_physical_min);
static_assert(field_physical_max - field_reticle_type == CSecondVPParams::optic_physical_max);
static_assert(field_eye_coupling - field_reticle_type == CSecondVPParams::optic_eye_coupling);
static_assert(field_reticle_illum - field_reticle_type == CSecondVPParams::optic_reticle_illum);
static_assert(field_magnification_mode - field_reticle_type == CSecondVPParams::optic_magnification_mode);
static_assert(field_magnifications - field_reticle_type == CSecondVPParams::optic_magnifications);
static_assert(field_mod_lane - field_reticle_type == CSecondVPParams::optic_mod_lane);

u64 svp_schema_hash_append(u64 hash, const void* data, size_t size)
{
	const u8* bytes = static_cast<const u8*>(data);
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

u64 svp_schema_hash_u8(u64 hash, u8 value)
{
	return svp_schema_hash_append(hash, &value, sizeof(value));
}

u64 svp_schema_hash_u16(u64 hash, u16 value)
{
	const u8 bytes[] = { static_cast<u8>(value), static_cast<u8>(value >> 8) };
	return svp_schema_hash_append(hash, bytes, sizeof(bytes));
}

u64 svp_schema_hash_u32(u64 hash, u32 value)
{
	u8 bytes[4] = {};
	for (u32 i = 0; i < _countof(bytes); ++i)
		bytes[i] = static_cast<u8>(value >> (i * 8));
	return svp_schema_hash_append(hash, bytes, sizeof(bytes));
}

u64 svp_schema_hash_u64(u64 hash, u64 value)
{
	u8 bytes[8] = {};
	for (u32 i = 0; i < _countof(bytes); ++i)
		bytes[i] = static_cast<u8>(value >> (i * 8));
	return svp_schema_hash_append(hash, bytes, sizeof(bytes));
}

u64 svp_schema_hash_double(u64 hash, double value)
{
	u64 bits = 0;
	static_assert(sizeof(bits) == sizeof(value));
	CopyMemory(&bits, &value, sizeof(bits));
	return svp_schema_hash_u64(hash, bits);
}

u64 svp_schema_hash_bool(u64 hash, bool value)
{
	return svp_schema_hash_u8(hash, value ? 1 : 0);
}

u64 svp_schema_hash_text(u64 hash, LPCSTR text)
{
	const LPCSTR value = text ? text : "";
	return svp_schema_hash_append(hash, value, xr_strlen(value) + 1);
}

LPCSTR svp_native_schema_hash()
{
	static string32 value = {};
	if (value[0])
		return value;

	u64 hash = 14695981039346656037ull;
	hash = svp_schema_hash_u32(hash, CSecondVPParams::optic_schema_version);
	hash = svp_schema_hash_u32(hash, _countof(s_optic_fields));
	for (const SFieldDescriptor& field : s_optic_fields)
	{
		hash = svp_schema_hash_u8(hash, static_cast<u8>(field.id));
		hash = svp_schema_hash_text(hash, field.name);
		hash = svp_schema_hash_text(hash, CSecondVPParams::OpticFieldTypeName(field.type));
		hash = svp_schema_hash_bool(hash, field.required);
		hash = svp_schema_hash_bool(hash, field.registrable);
		hash = svp_schema_hash_bool(hash, field.source_required);
		hash = svp_schema_hash_double(hash, field.minimum);
		hash = svp_schema_hash_double(hash, field.maximum);
		hash = svp_schema_hash_u16(hash, field.string_capacity);
		hash = svp_schema_hash_u8(hash, field.array_min);
		hash = svp_schema_hash_u8(hash, field.array_max);
		hash = svp_schema_hash_bool(hash, field.finite);
		hash = svp_schema_hash_bool(hash, field.ordered);
		hash = svp_schema_hash_bool(hash, field.allow_zero);
		hash = svp_schema_hash_bool(hash, field.minimum_exclusive);
		hash = svp_schema_hash_bool(hash, field.maximum_exclusive);
		hash = svp_schema_hash_bool(hash, field.non_empty);
		hash = svp_schema_hash_u8(hash, field.member_count);
		for (u32 i = 0; i < field.member_count; ++i)
		{
			const SObjectMemberDescriptor& member = field.members[i];
			hash = svp_schema_hash_text(hash, member.name);
			hash = svp_schema_hash_text(hash, "number");
			hash = svp_schema_hash_bool(hash, member.finite);
			hash = svp_schema_hash_bool(hash, member.has_range);
			hash = svp_schema_hash_double(hash, member.minimum);
			hash = svp_schema_hash_double(hash, member.maximum);
			hash = svp_schema_hash_bool(hash, member.minimum_exclusive);
			hash = svp_schema_hash_bool(hash, member.maximum_exclusive);
		}
		hash = svp_schema_hash_u8(hash, field.enum_value_count);
		for (u32 i = 0; i < field.enum_value_count; ++i)
			hash = svp_schema_hash_text(hash, field.enum_values[i]);
		hash = svp_schema_hash_text(hash, field.element_type);
		hash = svp_schema_hash_bool(hash, field.element_non_empty);
		hash = svp_schema_hash_u16(hash, field.element_string_capacity);
		hash = svp_schema_hash_text(hash, field.constraint);
	}
	xr_sprintf(value, "%016llx", hash);
	return value;
}
}

const CSecondVPParams::OpticFieldDescriptorArray& CSecondVPParams::OpticFieldDescriptors()
{
	return s_optic_fields;
}

LPCSTR CSecondVPParams::OpticFieldTypeName(EOpticFieldType type)
{
	switch (type)
	{
	case optic_type_integer: return "integer";
	case optic_type_number: return "number";
	case optic_type_boolean: return "boolean";
	case optic_type_string: return "string";
	case optic_type_objective: return "object";
	case optic_type_mode: return "enum";
	case optic_type_magnifications: return "array";
	case optic_type_lane: return "object";
	case optic_type_sources: return "map";
	default: return "unknown";
	}
}

LPCSTR CSecondVPParams::OpticSchemaHash()
{
	return svp_native_schema_hash();
}

static void svp_hash_bytes(u64& hash, const void* data, size_t size)
{
	const u8* bytes = static_cast<const u8*>(data);
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
}

static u64 svp_hash_optic_config(const CSecondVPParams::OpticConfig& config)
{
	u64 hash = 14695981039346656037ull;
	svp_hash_bytes(hash, &config.has_objective_offset, sizeof(config.has_objective_offset));
	svp_hash_bytes(hash, &config.has_objective_mm, sizeof(config.has_objective_mm));
	svp_hash_bytes(hash, &config.has_hybrid_reflex, sizeof(config.has_hybrid_reflex));
	svp_hash_bytes(hash, &config.hybrid_reflex, sizeof(config.hybrid_reflex));
	svp_hash_bytes(hash, &config.has_physical_range, sizeof(config.has_physical_range));
	svp_hash_bytes(hash, &config.has_mod_lane, sizeof(config.has_mod_lane));
	svp_hash_bytes(hash, &config.zoom_type, sizeof(config.zoom_type));
	svp_hash_bytes(hash, &config.reticle_type, sizeof(config.reticle_type));
	svp_hash_bytes(hash, &config.weapon_id, sizeof(config.weapon_id));
	svp_hash_bytes(hash, &config.objective_offset, sizeof(config.objective_offset));
	svp_hash_bytes(hash, &config.objective_mm, sizeof(config.objective_mm));
	svp_hash_bytes(hash, &config.middle_grey, sizeof(config.middle_grey));
	svp_hash_bytes(hash, &config.adapt_speed, sizeof(config.adapt_speed));
	svp_hash_bytes(hash, &config.convergence_limit_m, sizeof(config.convergence_limit_m));
	svp_hash_bytes(hash, &config.tunneling_parallax, sizeof(config.tunneling_parallax));
	svp_hash_bytes(hash, &config.tunneling_min, sizeof(config.tunneling_min));
	svp_hash_bytes(hash, &config.tunneling_max, sizeof(config.tunneling_max));
	svp_hash_bytes(hash, &config.tracking_speed, sizeof(config.tracking_speed));
	svp_hash_bytes(hash, &config.tracking_accel_mm_s2, sizeof(config.tracking_accel_mm_s2));
	svp_hash_bytes(hash, &config.tracking_limit_mm, sizeof(config.tracking_limit_mm));
	svp_hash_bytes(hash, &config.eye_relief_low_mm, sizeof(config.eye_relief_low_mm));
	svp_hash_bytes(hash, &config.eye_relief_high_mm, sizeof(config.eye_relief_high_mm));
	svp_hash_bytes(hash, &config.exit_pupil_low_mm, sizeof(config.exit_pupil_low_mm));
	svp_hash_bytes(hash, &config.exit_pupil_high_mm, sizeof(config.exit_pupil_high_mm));
	svp_hash_bytes(hash, &config.pupil_parity, sizeof(config.pupil_parity));
	svp_hash_bytes(hash, &config.pupil_field_low, sizeof(config.pupil_field_low));
	svp_hash_bytes(hash, &config.pupil_field_high, sizeof(config.pupil_field_high));
	svp_hash_bytes(hash, &config.transmission, sizeof(config.transmission));
	svp_hash_bytes(hash, &config.twilight_strength, sizeof(config.twilight_strength));
	svp_hash_bytes(hash, &config.physical_min, sizeof(config.physical_min));
	svp_hash_bytes(hash, &config.physical_max, sizeof(config.physical_max));
	svp_hash_bytes(hash, &config.eye_coupling, sizeof(config.eye_coupling));
	svp_hash_bytes(hash, &config.reticle_illum, sizeof(config.reticle_illum));
	svp_hash_bytes(hash, &config.mod_lane, sizeof(config.mod_lane));
	svp_hash_bytes(hash, &config.magnifications.mode, sizeof(config.magnifications.mode));
	svp_hash_bytes(hash, &config.magnifications.count, sizeof(config.magnifications.count));
	svp_hash_bytes(hash, config.magnifications.values,
		sizeof(config.magnifications.values[0]) * config.magnifications.count);
	svp_hash_bytes(hash, config.context, sizeof(config.context));
	svp_hash_bytes(hash, config.weapon, sizeof(config.weapon));
	svp_hash_bytes(hash, config.scope, sizeof(config.scope));
	svp_hash_bytes(hash, config.diagnostic_scope, sizeof(config.diagnostic_scope));
	svp_hash_bytes(hash, config.identity_source, sizeof(config.identity_source));
	svp_hash_bytes(hash, config.profile_id, sizeof(config.profile_id));
	svp_hash_bytes(hash, config.spec_section, sizeof(config.spec_section));
	svp_hash_bytes(hash, config.model, sizeof(config.model));
	svp_hash_bytes(hash, config.binding, sizeof(config.binding));
	svp_hash_bytes(hash, config.binding_section, sizeof(config.binding_section));
	svp_hash_bytes(hash, config.source, sizeof(config.source));
	return hash;
}

static u32 svp_next_nonzero(u32& value)
{
	++value;
	if (!value)
		++value;
	return value;
}

static u32 svp_peek_next_nonzero(u32 value)
{
	return svp_next_nonzero(value);
}

static bool svp_optic_identity_matches(u32 context_token,
	const CSecondVPParams::OpticConfig& config,
	const CSecondVPParams::OpticConfig& current)
{
	return context_token && context_token == current.context_token &&
		!xr_strcmp(config.context, current.context) &&
		!xr_strcmp(config.weapon, current.weapon) &&
		config.weapon_id == current.weapon_id &&
		!xr_strcmp(config.scope, current.scope) &&
		!xr_strcmp(config.identity_source, current.identity_source) &&
		!xr_strcmp(config.diagnostic_scope, current.diagnostic_scope) &&
		config.zoom_type == current.zoom_type;
}

bool CSecondVPParams::ConnectOpticApi(u32 api, u32 schema)
{
	if (api != optic_api_version || schema != optic_schema_version)
		return false;

	xrCriticalSectionGuard guard(m_snapshot_lock);
	if (m_optic_api_connected.load(std::memory_order_relaxed))
		return true;

	m_optic_api_connected.store(true, std::memory_order_release);
	return true;
}

void CSecondVPParams::SetOpticScopeMode(u8 mode)
{
	const u8 previous = m_optic_scope_mode.load(std::memory_order_acquire);
	if (previous == mode)
		return;
	if (!mode)
		m_optic_scope_mode.store(0, std::memory_order_release);

	xrCriticalSectionGuard guard(m_snapshot_lock);
	ResetOpticConfigLocked();
	if (mode)
		m_optic_scope_mode.store(mode, std::memory_order_release);
}

u32 CSecondVPParams::BeginOpticContext(LPCSTR context, LPCSTR weapon, u32 weapon_id,
	LPCSTR scope, u8 zoom_type,
	LPCSTR identity_source, LPCSTR diagnostic_scope)
{
	if (!IsOpticApiEnabled())
		return 0;

	xrCriticalSectionGuard guard(m_snapshot_lock);
	if (!IsOpticApiEnabled())
		return 0;
	OpticConfig next;
	next.typed_route = true;
	next.context_token = svp_next_nonzero(m_optic_token_counter);
	next.generation = svp_next_nonzero(m_optic_generation_counter);
	next.route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	next.session = GetSVPSession();
	next.zoom_type = zoom_type;
	next.weapon_id = weapon_id;
	xr_strcpy(next.context, sizeof(next.context), context ? context : "");
	xr_strcpy(next.weapon, sizeof(next.weapon), weapon ? weapon : "");
	xr_strcpy(next.scope, sizeof(next.scope), scope ? scope : "");
	xr_strcpy(next.identity_source, sizeof(next.identity_source), identity_source ? identity_source : "");
	xr_strcpy(next.diagnostic_scope, sizeof(next.diagnostic_scope), diagnostic_scope ? diagnostic_scope : "");
	next.fingerprint = svp_hash_optic_config(next);
	m_optic_accepted = next;
	return next.context_token;
}

bool CSecondVPParams::PrepareOpticConfig(u32 context_token, const OpticConfig& config,
	OpticPublication& publication)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	const u32 session = GetSVPSession();
	if (!IsOpticApiEnabled() || !m_optic_accepted.typed_route ||
		m_optic_accepted.session != session ||
		!svp_optic_identity_matches(context_token, config, m_optic_accepted))
		return false;

	OpticConfig next = config;
	next.valid = true;
	next.typed_route = true;
	next.context_token = context_token;
	next.route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	next.frame = u32(-1);
	next.session = session;
	next.fingerprint = svp_hash_optic_config(next);
	publication.base_generation = m_optic_accepted.generation;
	publication.base_fingerprint = m_optic_accepted.fingerprint;
	if (m_optic_accepted.valid && next.fingerprint == m_optic_accepted.fingerprint)
	{
		publication.accepted = m_optic_accepted;
		return true;
	}

	next.generation = svp_peek_next_nonzero(m_optic_generation_counter);
	publication.accepted = next;
	return true;
}

bool CSecondVPParams::PublishOpticConfig(u32 context_token,
	const OpticPublication& publication)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	const u32 session = GetSVPSession();
	const OpticConfig& next = publication.accepted;
	if (!IsOpticApiEnabled() || !m_optic_accepted.typed_route ||
		m_optic_accepted.session != session ||
		m_optic_accepted.generation != publication.base_generation ||
		m_optic_accepted.fingerprint != publication.base_fingerprint ||
		!svp_optic_identity_matches(context_token, next, m_optic_accepted) ||
		!next.valid || !next.typed_route ||
		next.context_token != context_token ||
		next.route_epoch != m_optic_route_epoch.load(std::memory_order_relaxed) ||
		next.session != session || next.frame != u32(-1) ||
		next.fingerprint != svp_hash_optic_config(next))
		return false;

	if (m_optic_accepted.valid && next.fingerprint == m_optic_accepted.fingerprint)
		return next.generation == m_optic_accepted.generation;
	if (next.generation != svp_peek_next_nonzero(m_optic_generation_counter))
		return false;

	m_optic_generation_counter = next.generation;
	m_optic_accepted = next;
	return true;
}

bool CSecondVPParams::ClearOpticConfig(u32 context_token)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	if (!context_token || context_token != m_optic_accepted.context_token)
		return false;

	OpticConfig next;
	next.generation = svp_next_nonzero(m_optic_generation_counter);
	next.route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	next.fingerprint = svp_hash_optic_config(next);
	m_optic_accepted = next;
	return true;
}

void CSecondVPParams::InvalidateOpticConfig()
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	ResetOpticConfigLocked();
}

void CSecondVPParams::ResetOpticConfigLocked()
{
	u32 route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed) + 1;
	if (!route_epoch)
		route_epoch = 1;
	m_optic_route_epoch.store(route_epoch, std::memory_order_release);

	OpticConfig next;
	next.generation = svp_next_nonzero(m_optic_generation_counter);
	next.route_epoch = route_epoch;
	next.fingerprint = svp_hash_optic_config(next);
	m_optic_accepted = next;
}

bool CSecondVPParams::ReadOpticConfig(OpticConfig& config) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	config = m_optic_accepted;
	return config.valid;
}

void CSecondVPParams::LatchOpticConfig(u32 frame, u32 session)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	const u32 route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	if (m_optic_active.frame == frame)
		return;
	const bool enabled = IsOpticApiEnabled();
	m_optic_active = enabled ? m_optic_accepted : m_optic_neutral;
	m_optic_active.typed_route = enabled;
	m_optic_active.frame = frame;
	m_optic_active.session = session;
	m_optic_active.route_epoch = route_epoch;
}

const CSecondVPParams::OpticConfig& CSecondVPParams::RenderOpticConfig() const
{
	return m_optic_active;
}

void CSecondVPParams::ReadOpticConfigState(OpticConfig& accepted, OpticConfig& active, u32& route_epoch) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	accepted = m_optic_accepted;
	active = m_optic_active;
	route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
}
