#include "../xrCore/xrCore.h"
#ifndef ENGINE_API
#define ENGINE_API
#endif
#include "../xrEngine/svp_state.h"
#include "../xrGame/svp_optic_config_script.h"
#include "../xrGame/svp_mags.h"
#include "../Layers/xrRenderPC_R4/svp_physical_optics.h"
#include "../Layers/xrRender/svp_lens_detect_math.h"

#include <luabind/luabind.hpp>
#include <luabind/detail/class_registry.hpp>
#include <lua.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

void printLuaStack()
{
}

enum Console_mark : int;

bool is_console_mark(Console_mark)
{
	return false;
}

LPCSTR build_date = __DATE__;
u32 build_id = 0;

xr_string get_modded_exes_version_string()
{
	return "svp-test";
}

LPCSTR get_modded_exes_name()
{
	return "svp-test-client";
}

namespace
{
struct Options
{
	std::string suite = "all";
	std::string format = "text";
	std::filesystem::path repo;
	std::uint64_t seed = 0x5356504150497633ull;
	std::uint32_t iterations = 20000;
	bool verbose = false;
};

struct Failure
{
	std::string suite;
	std::string test;
	std::string detail;
};

class Harness
{
public:
	explicit Harness(Options options) : m_options(std::move(options))
	{
	}

	void Run(const std::string& suite, const std::string& test,
		const std::function<void()>& body)
	{
		if (!Selected(suite))
			return;
		++m_tests;
		m_current_suite = suite;
		m_current_test = test;
		const std::size_t failures_before = m_failures.size();
		body();
		if (m_failures.size() == failures_before)
			++m_passed;
	}

	bool Check(bool condition, const char* expression, const char* file, int line)
	{
		++m_assertions;
		if (condition)
			return true;
		std::ostringstream detail;
		detail << expression << " at " << file << ':' << line;
		m_failures.push_back(
			{ m_current_suite, m_current_test, detail.str() });
		return false;
	}

	bool Reject(bool condition, const char* expression, const char* file, int line)
	{
		++m_rejections;
		return Check(condition, expression, file, line);
	}

	void AddRejections(std::uint64_t count)
	{
		m_rejections += count;
	}

	bool Near(double left, double right, double tolerance,
		const char* expression, const char* file, int line)
	{
		++m_assertions;
		if (std::isfinite(left) && std::isfinite(right) &&
			std::abs(left - right) <= tolerance)
			return true;
		std::ostringstream detail;
		detail << expression << " left=" << std::setprecision(17) << left
			<< " right=" << right << " tolerance=" << tolerance
			<< " at " << file << ':' << line;
		m_failures.push_back(
			{ m_current_suite, m_current_test, detail.str() });
		return false;
	}

	bool Fail(const std::string& message, const char* file, int line)
	{
		std::ostringstream detail;
		detail << message << " at " << file << ':' << line;
		m_failures.push_back(
			{ m_current_suite, m_current_test, detail.str() });
		return false;
	}

	bool Selected(const std::string& suite) const
	{
		return m_options.suite == "all" || m_options.suite == suite;
	}

	const Options& GetOptions() const
	{
		return m_options;
	}

	void Trace(const std::string& message) const
	{
		if (m_options.verbose)
			std::cerr << "TRACE " << m_current_suite << '.'
				<< m_current_test << ' ' << message << '\n';
	}

	int Finish() const
	{
		if (m_options.format == "json")
			WriteJson();
		else
			WriteText();
		return m_failures.empty() ? 0 : 1;
	}

private:
	static std::string JsonEscape(const std::string& value)
	{
		std::ostringstream out;
		for (const unsigned char ch : value)
		{
			switch (ch)
			{
			case '\\': out << "\\\\"; break;
			case '"': out << "\\\""; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default:
				if (ch < 0x20)
					out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
						<< static_cast<int>(ch) << std::dec;
				else
					out << ch;
				break;
			}
		}
		return out.str();
	}

	void WriteText() const
	{
		for (const Failure& failure : m_failures)
			std::cerr << "FAIL " << failure.suite << '.' << failure.test
				<< ' ' << failure.detail << '\n';
		std::cout << "svp test client "
			<< (m_failures.empty() ? "passed" : "failed")
			<< " tests=" << m_tests
			<< " passed=" << m_passed
			<< " assertions=" << m_assertions
			<< " rejections=" << m_rejections
			<< " failures=" << m_failures.size()
			<< " seed=" << m_options.seed
			<< " iterations=" << m_options.iterations << '\n';
	}

	void WriteJson() const
	{
		std::cout << "{\"ok\":" << (m_failures.empty() ? "true" : "false")
			<< ",\"tests\":" << m_tests
			<< ",\"passed\":" << m_passed
			<< ",\"assertions\":" << m_assertions
			<< ",\"rejections\":" << m_rejections
			<< ",\"seed\":" << m_options.seed
			<< ",\"iterations\":" << m_options.iterations
			<< ",\"failures\":[";
		for (std::size_t i = 0; i < m_failures.size(); ++i)
		{
			if (i)
				std::cout << ',';
			const Failure& failure = m_failures[i];
			std::cout << "{\"suite\":\"" << JsonEscape(failure.suite)
				<< "\",\"test\":\"" << JsonEscape(failure.test)
				<< "\",\"detail\":\"" << JsonEscape(failure.detail) << "\"}";
		}
		std::cout << "]}\n";
	}

	Options m_options;
	std::uint64_t m_tests = 0;
	std::uint64_t m_passed = 0;
	std::uint64_t m_assertions = 0;
	std::uint64_t m_rejections = 0;
	std::vector<Failure> m_failures;
	std::string m_current_suite;
	std::string m_current_test;
};

#define CHECK(harness, expression) do { \
	if (!(harness).Check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)) \
		return; \
} while (false)
#define CHECK_REJECT(harness, expression) do { \
	if (!(harness).Reject(static_cast<bool>(expression), #expression, __FILE__, __LINE__)) \
		return; \
} while (false)
#define CHECK_NEAR(harness, left, right, tolerance) do { \
	if (!(harness).Near((left), (right), (tolerance), \
		#left " ~= " #right, __FILE__, __LINE__)) \
		return; \
} while (false)
#define FAIL(harness, message) do { \
	(harness).Fail((message), __FILE__, __LINE__); \
	return; \
} while (false)

template <std::size_t N>
void CopyText(char (&destination)[N], const std::string& value)
{
	if (value.size() >= N)
	{
		std::cerr << "svp test client error test string exceeds destination\n";
		std::exit(2);
	}
	std::memset(destination, 0, N);
	std::memcpy(destination, value.data(), value.size());
}

bool Finite(const svp_v3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) &&
		std::isfinite(value.z);
}

bool Finite(const SvpPhysicalOptics::Vec2& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y);
}

CSecondVPParams::OpticConfig MakeConfig(
	const CSecondVPParams::OpticConfig& identity, float marker = 0.5f)
{
	CSecondVPParams::OpticConfig config = identity;
	config.reticle_type = 2;
	config.has_hybrid_reflex = true;
	config.hybrid_reflex = true;
	config.has_objective_offset = true;
	config.objective_offset.set(0.1f, -0.2f, 10.f, 1.2f);
	config.has_objective_mm = true;
	config.objective_mm = 24.f;
	config.middle_grey = marker;
	config.adapt_speed = 1.f;
	config.convergence_limit_m = 100.f;
	config.tunneling_parallax = 0.035f;
	config.tunneling_min = 0.04f;
	config.tunneling_max = 0.06f;
	config.tracking_speed = 5.f;
	config.tracking_accel_mm_s2 = 80.f;
	config.tracking_limit_mm = 7.f;
	config.eye_relief_low_mm = 80.f;
	config.eye_relief_high_mm = 80.f;
	config.exit_pupil_low_mm = 8.f;
	config.exit_pupil_high_mm = 4.f;
	config.pupil_parity = -1.f;
	config.pupil_field_low = 0.55f;
	config.pupil_field_high = 0.55f;
	config.transmission = 0.9f;
	config.twilight_strength = 0.35f;
	config.has_physical_range = true;
	config.physical_min = 1.f;
	config.physical_max = 6.f;
	config.eye_coupling = true;
	config.reticle_illum = 1.f;
	config.magnifications.mode = CSecondVPParams::optic_magnification_detent;
	config.magnifications.count = 4;
	config.magnifications.values[0] = 1.f;
	config.magnifications.values[1] = 1.5f;
	config.magnifications.values[2] = 4.f;
	config.magnifications.values[3] = 6.f;
	config.has_mod_lane = true;
	config.mod_lane.set(1.f, 2.f, 3.f, 4.f);
	CopyText(config.profile_id, "svp_test_profile");
	CopyText(config.spec_section, "svp_test_spec");
	CopyText(config.model, "geometric");
	CopyText(config.binding, "svp_test");
	CopyText(config.binding_section, "svp_test_binding");
	for (u32 i = 0; i < CSecondVPParams::optic_value_count; ++i)
		CopyText(config.source[i], "svp_test_source_" + std::to_string(i));
	return config;
}

u64 HashBytes(u64 hash, const void* data, std::size_t size)
{
	const auto* bytes = static_cast<const std::uint8_t*>(data);
	for (std::size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

u64 HashU8(u64 hash, std::uint8_t value)
{
	return HashBytes(hash, &value, sizeof(value));
}

u64 HashU16(u64 hash, std::uint16_t value)
{
	const std::uint8_t bytes[] =
	{
		static_cast<std::uint8_t>(value),
		static_cast<std::uint8_t>(value >> 8)
	};
	return HashBytes(hash, bytes, sizeof(bytes));
}

u64 HashU32(u64 hash, std::uint32_t value)
{
	std::uint8_t bytes[4] = {};
	for (std::uint32_t i = 0; i < 4; ++i)
		bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
	return HashBytes(hash, bytes, sizeof(bytes));
}

u64 HashU64(u64 hash, std::uint64_t value)
{
	std::uint8_t bytes[8] = {};
	for (std::uint32_t i = 0; i < 8; ++i)
		bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
	return HashBytes(hash, bytes, sizeof(bytes));
}

u64 HashDouble(u64 hash, double value)
{
	std::uint64_t bits = 0;
	static_assert(sizeof(bits) == sizeof(value));
	std::memcpy(&bits, &value, sizeof(bits));
	return HashU64(hash, bits);
}

u64 HashBool(u64 hash, bool value)
{
	return HashU8(hash, value ? 1 : 0);
}

u64 HashText(u64 hash, const char* value)
{
	value = value ? value : "";
	return HashBytes(hash, value, std::strlen(value) + 1);
}

std::string ClientSchemaHash()
{
	const auto& fields = CSecondVPParams::OpticFieldDescriptors();
	u64 hash = 14695981039346656037ull;
	hash = HashU32(hash, CSecondVPParams::optic_schema_version);
	hash = HashU32(hash, CSecondVPParams::optic_field_count);
	for (const auto& field : fields)
	{
		hash = HashU8(hash, static_cast<std::uint8_t>(field.id));
		hash = HashText(hash, field.name);
		hash = HashText(hash, CSecondVPParams::OpticFieldTypeName(field.type));
		hash = HashBool(hash, field.required);
		hash = HashBool(hash, field.registrable);
		hash = HashBool(hash, field.source_required);
		hash = HashDouble(hash, field.minimum);
		hash = HashDouble(hash, field.maximum);
		hash = HashU16(hash, field.string_capacity);
		hash = HashU8(hash, field.array_min);
		hash = HashU8(hash, field.array_max);
		hash = HashBool(hash, field.finite);
		hash = HashBool(hash, field.ordered);
		hash = HashBool(hash, field.allow_zero);
		hash = HashBool(hash, field.minimum_exclusive);
		hash = HashBool(hash, field.maximum_exclusive);
		hash = HashBool(hash, field.non_empty);
		hash = HashU8(hash, field.member_count);
		for (u32 i = 0; i < field.member_count; ++i)
		{
			const auto& member = field.members[i];
			hash = HashText(hash, member.name);
			hash = HashText(hash, "number");
			hash = HashBool(hash, member.finite);
			hash = HashBool(hash, member.has_range);
			hash = HashDouble(hash, member.minimum);
			hash = HashDouble(hash, member.maximum);
			hash = HashBool(hash, member.minimum_exclusive);
			hash = HashBool(hash, member.maximum_exclusive);
		}
		hash = HashU8(hash, field.enum_value_count);
		for (u32 i = 0; i < field.enum_value_count; ++i)
			hash = HashText(hash, field.enum_values[i]);
		hash = HashText(hash, field.element_type);
		hash = HashBool(hash, field.element_non_empty);
		hash = HashU16(hash, field.element_string_capacity);
		hash = HashText(hash, field.constraint);
	}
	std::ostringstream value;
	value << std::hex << std::setfill('0') << std::setw(16) << hash;
	return value.str();
}

void AddSchemaTests(Harness& harness)
{
	harness.Run("schema", "version_and_hash", [&]
	{
		CHECK(harness, svp_optic_api_version() == 3);
		CHECK(harness, CSecondVPParams::optic_api_min == 3);
		CHECK(harness, CSecondVPParams::optic_api_max == 3);
		CHECK(harness, CSecondVPParams::optic_schema_min == 3);
		CHECK(harness, CSecondVPParams::optic_schema_max == 3);
		CHECK(harness, ClientSchemaHash() == CSecondVPParams::OpticSchemaHash());
		CHECK(harness, ClientSchemaHash() == "344d9e3d18d03c3f");
	});

	harness.Run("schema", "descriptor_integrity", [&]
	{
		const auto& fields = CSecondVPParams::OpticFieldDescriptors();
		std::set<std::string> names;
		u32 sources = 0;
		for (u32 i = 0; i < CSecondVPParams::optic_field_count; ++i)
		{
			const auto& field = fields[i];
			CHECK(harness, field.id == static_cast<CSecondVPParams::EOpticFieldId>(i));
			CHECK(harness, field.name != nullptr && field.name[0] != 0);
			CHECK(harness, names.insert(field.name).second);
			CHECK(harness,
				std::strcmp(CSecondVPParams::OpticFieldTypeName(field.type), "unknown"));
			if (field.source_required)
			{
				CHECK(harness, field.registrable);
				CHECK(harness,
					field.id >= CSecondVPParams::optic_field_reticle_type);
				CHECK(harness, field.id <= CSecondVPParams::optic_field_mod_lane);
				CHECK(harness,
					static_cast<u32>(field.id -
						CSecondVPParams::optic_field_reticle_type) == sources);
				++sources;
			}
			if (field.string_capacity)
				CHECK(harness, field.non_empty);
			if (field.array_max)
			{
				CHECK(harness, field.array_min > 0);
				CHECK(harness, field.array_min <= field.array_max);
				CHECK(harness, field.element_type != nullptr);
			}
		}
		CHECK(harness, names.size() == 44);
		CHECK(harness, sources == CSecondVPParams::optic_value_count);
	});

	harness.Run("schema", "capabilities", [&]
	{
		const char* capabilities[] =
		{
			"api_info",
			"field_descriptors",
			"structured_validation",
			"typed_transport",
			"hybrid_reflex",
			"profile_inspector"
		};
		for (const char* capability : capabilities)
			CHECK(harness, svp_optic_api_has_capability(capability));
		CHECK_REJECT(harness, !svp_optic_api_has_capability("unknown"));
		CHECK_REJECT(harness, !svp_optic_api_has_capability(nullptr));
	});
}

void AddStateTests(Harness& harness)
{
	harness.Run("state", "connection_and_context", [&]
	{
		CSecondVPParams state;
		CSecondVPParams::OpticConfig read;
		CHECK(harness, !state.IsOpticApiConnected());
		CHECK(harness, !state.IsOpticApiEnabled());
		CHECK_REJECT(harness, !state.ConnectOpticApi(2, 3));
		CHECK_REJECT(harness, !state.ConnectOpticApi(3, 2));
		CHECK(harness, state.ConnectOpticApi(3, 3));
		CHECK(harness, state.ConnectOpticApi(3, 3));
		CHECK(harness, !state.IsOpticApiEnabled());
		CHECK_REJECT(harness,
			state.BeginOpticContext("a", "b", 1, "c", 0, "d", "e") == 0);
		const u32 route_before = state.GetOpticRouteEpoch();
		state.SetOpticScopeMode(2);
		CHECK(harness, state.IsOpticApiEnabled());
		CHECK(harness, state.GetOpticRouteEpoch() != route_before);
		const u32 token = state.BeginOpticContext(
			"ctx", "weapon", 42, "scope", 1, "identity", "diagnostic");
		CHECK(harness, token > 0);
		CHECK(harness, !state.ReadOpticConfig(read));
		CHECK(harness, read.typed_route);
		CHECK(harness, read.context_token == token);
		CHECK(harness, read.generation > 0);
		CHECK(harness, read.route_epoch == state.GetOpticRouteEpoch());
		CHECK(harness, !std::strcmp(read.context, "ctx"));
		CHECK(harness, !std::strcmp(read.weapon, "weapon"));
		CHECK(harness, !std::strcmp(read.scope, "scope"));
		CHECK(harness, !std::strcmp(read.identity_source, "identity"));
		CHECK(harness, !std::strcmp(read.diagnostic_scope, "diagnostic"));
	});

	harness.Run("state", "atomic_publication", [&]
	{
		CSecondVPParams state;
		CHECK(harness, state.ConnectOpticApi(3, 3));
		state.SetOpticScopeMode(2);
		const u32 token = state.BeginOpticContext(
			"ctx", "weapon", 42, "scope", 1, "identity", "diagnostic");
		CSecondVPParams::OpticConfig identity;
		CHECK(harness, !state.ReadOpticConfig(identity));
		auto config = MakeConfig(identity);
		CSecondVPParams::OpticPublication publication;
		CHECK(harness, state.PrepareOpticConfig(token, config, publication));
		CSecondVPParams::OpticConfig before;
		CHECK(harness, !state.ReadOpticConfig(before));
		CHECK(harness, before.generation == publication.base_generation);
		CHECK(harness, before.fingerprint == publication.base_fingerprint);
		CHECK(harness, publication.accepted.valid);
		CHECK(harness, publication.accepted.generation != before.generation);

		auto rejected = publication;
		++rejected.base_generation;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		rejected.base_fingerprint ^= 1;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		rejected.accepted.context_token = token + 1;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		++rejected.accepted.route_epoch;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		++rejected.accepted.session;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		rejected.accepted.frame = 0;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		rejected.accepted.fingerprint ^= 1;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		rejected.accepted.valid = false;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		rejected = publication;
		rejected.accepted.typed_route = false;
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, rejected));
		CSecondVPParams::OpticConfig after_rejections;
		CHECK(harness, !state.ReadOpticConfig(after_rejections));
		CHECK(harness, after_rejections.generation == before.generation);
		CHECK(harness, after_rejections.fingerprint == before.fingerprint);
		CHECK(harness, after_rejections.context_token == before.context_token);
		CHECK(harness, after_rejections.route_epoch == before.route_epoch);

		CHECK(harness, state.PublishOpticConfig(token, publication));
		CSecondVPParams::OpticConfig accepted;
		CHECK(harness, state.ReadOpticConfig(accepted));
		CHECK(harness, accepted.context_token == token);
		CHECK(harness, accepted.fingerprint == publication.accepted.fingerprint);
		CHECK(harness, accepted.generation == publication.accepted.generation);

		CSecondVPParams::OpticPublication repeated;
		CHECK(harness, state.PrepareOpticConfig(token, config, repeated));
		CHECK(harness, repeated.accepted.generation == accepted.generation);
		CHECK(harness, repeated.accepted.fingerprint == accepted.fingerprint);
		CHECK(harness, state.PublishOpticConfig(token, repeated));
		CSecondVPParams::OpticConfig stable;
		CHECK(harness, state.ReadOpticConfig(stable));
		CHECK(harness, stable.generation == accepted.generation);

		auto changed = MakeConfig(identity, 0.75f);
		CSecondVPParams::OpticPublication next;
		CHECK(harness, state.PrepareOpticConfig(token, changed, next));
		CHECK(harness, next.accepted.generation != stable.generation);
		CHECK(harness, next.accepted.fingerprint != stable.fingerprint);
		CHECK(harness, state.PublishOpticConfig(token, next));
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, publication));
	});

	harness.Run("state", "identity_and_reset_guards", [&]
	{
		CSecondVPParams state;
		CHECK(harness, state.ConnectOpticApi(3, 3));
		state.SetOpticScopeMode(2);
		const u32 token = state.BeginOpticContext(
			"ctx", "weapon", 42, "scope", 1, "identity", "diagnostic");
		CSecondVPParams::OpticConfig identity;
		CHECK(harness, !state.ReadOpticConfig(identity));
		auto base = MakeConfig(identity);
		const std::vector<std::function<void(CSecondVPParams::OpticConfig&)>> mutations =
		{
			[](auto& value) { CopyText(value.context, "other"); },
			[](auto& value) { CopyText(value.weapon, "other"); },
			[](auto& value) { ++value.weapon_id; },
			[](auto& value) { CopyText(value.scope, "other"); },
			[](auto& value) { CopyText(value.identity_source, "other"); },
			[](auto& value) { CopyText(value.diagnostic_scope, "other"); },
			[](auto& value) { ++value.zoom_type; }
		};
		for (const auto& mutate : mutations)
		{
			auto candidate = base;
			mutate(candidate);
			CSecondVPParams::OpticPublication publication;
			CHECK_REJECT(harness,
				!state.PrepareOpticConfig(token, candidate, publication));
		}
		CSecondVPParams::OpticPublication publication;
		CHECK_REJECT(harness,
			!state.PrepareOpticConfig(token + 1, base, publication));
		CHECK(harness, state.PrepareOpticConfig(token, base, publication));
		CHECK_REJECT(harness, !state.ClearOpticConfig(0));
		CHECK_REJECT(harness, !state.ClearOpticConfig(token + 1));
		CHECK(harness, state.ClearOpticConfig(token));
		CSecondVPParams::OpticConfig cleared;
		CHECK(harness, !state.ReadOpticConfig(cleared));
		CHECK_REJECT(harness, !state.PublishOpticConfig(token, publication));
		const u32 route = state.GetOpticRouteEpoch();
		state.InvalidateOpticConfig();
		CHECK(harness, state.GetOpticRouteEpoch() != route);
		CHECK(harness, !state.ReadOpticConfig(cleared));
		state.SetOpticScopeMode(0);
		CHECK(harness, !state.IsOpticApiEnabled());
		CHECK_REJECT(harness, state.BeginOpticContext(
			"ctx", "weapon", 42, "scope", 1, "identity", "diagnostic") == 0);
	});

	harness.Run("state", "latch_and_snapshots", [&]
	{
		CSecondVPParams state;
		CHECK(harness, state.ConnectOpticApi(3, 3));
		state.SetOpticScopeMode(2);
		const u32 token = state.BeginOpticContext(
			"ctx", "weapon", 42, "scope", 1, "identity", "diagnostic");
		CSecondVPParams::OpticConfig identity;
		CHECK(harness, !state.ReadOpticConfig(identity));
		auto config = MakeConfig(identity);
		CSecondVPParams::OpticPublication publication;
		CHECK(harness, state.PrepareOpticConfig(token, config, publication));
		CHECK(harness, state.PublishOpticConfig(token, publication));
		state.LatchOpticConfig(10, 7);
		const auto latched = state.RenderOpticConfig();
		CHECK(harness, latched.valid);
		CHECK(harness, latched.frame == 10);
		CHECK(harness, latched.session == 7);
		CHECK(harness, latched.context_token == token);

		auto changed = MakeConfig(identity, 0.8f);
		CSecondVPParams::OpticPublication next;
		CHECK(harness, state.PrepareOpticConfig(token, changed, next));
		CHECK(harness, state.PublishOpticConfig(token, next));
		state.LatchOpticConfig(10, 8);
		CHECK(harness,
			state.RenderOpticConfig().fingerprint == latched.fingerprint);
		state.LatchOpticConfig(11, 8);
		CHECK(harness,
			state.RenderOpticConfig().fingerprint == next.accepted.fingerprint);

		CSecondVPParams::WeaponPoseSnapshot pose;
		CHECK(harness, !state.ReadWeaponPose(pose));
		pose.frame = 20;
		pose.session = 4;
		pose.optic_context_token = token;
		pose.fire_ray_zero = 100.f;
		state.PublishWeaponPose(pose);
		CSecondVPParams::WeaponPoseSnapshot pose_read;
		CHECK(harness, state.ReadWeaponPose(pose_read));
		CHECK(harness, pose_read.frame == 20);
		CHECK(harness, pose_read.optic_context_token == token);
		CHECK(harness, pose_read.fire_ray_zero == 100.f);
		CHECK(harness, state.SnapshotExact(20, state.GetSVPSession(), 20));
		CHECK(harness, state.SnapshotRecent(19, state.GetSVPSession(), 20));
		CHECK(harness, !state.SnapshotRecent(18, state.GetSVPSession(), 20));
		state.ClearWeaponPose();
		CHECK(harness, !state.ReadWeaponPose(pose_read));

		CSecondVPParams::SightSnapshot sight;
		CHECK(harness, !state.ReadSight(sight));
		sight.frame = 30;
		sight.lens_radius = 0.02f;
		state.PublishSight(sight);
		CSecondVPParams::SightSnapshot sight_read;
		CHECK(harness, state.ReadSight(sight_read));
		CHECK(harness, sight_read.frame == 30);
		CHECK(harness, sight_read.lens_radius == 0.02f);
		state.ClearSight();
		CHECK(harness, !state.ReadSight(sight_read));

		for (u32 i = 0; i < 20; ++i)
		{
			CSecondVPParams::FireTrace trace = {};
			trace.time_ms = i;
			trace.pos.x = static_cast<float>(i);
			state.AppendFireTrace(trace);
		}
		CSecondVPParams::FireTrace traces[16] = {};
		state.ReadFireTraces(traces);
		for (u32 i = 0; i < 16; ++i)
		{
			const u32 expected = i < 4 ? i + 16 : i;
			CHECK(harness, traces[i].time_ms == expected);
			CHECK(harness, traces[i].pos.x == static_cast<float>(expected));
		}
	});

	harness.Run("concurrency", "publication_readers", [&]
	{
		CSecondVPParams state;
		CHECK(harness, state.ConnectOpticApi(3, 3));
		state.SetOpticScopeMode(2);
		std::atomic<bool> stop{ false };
		std::atomic<bool> failed{ false };
		const u32 loops = std::max<u32>(200, harness.GetOptions().iterations / 20);
		std::vector<std::thread> readers;
		for (u32 thread_index = 0; thread_index < 4; ++thread_index)
		{
			readers.emplace_back([&]
			{
				while (!stop.load(std::memory_order_acquire))
				{
					CSecondVPParams::OpticConfig value;
					const bool valid = state.ReadOpticConfig(value);
					if (valid && (!value.typed_route || !value.context_token ||
						!value.generation || !value.fingerprint ||
						!std::memchr(value.context, 0, sizeof(value.context)) ||
						!std::memchr(value.weapon, 0, sizeof(value.weapon))))
						failed.store(true, std::memory_order_release);
				}
			});
		}
		for (u32 i = 0; i < loops && !failed.load(std::memory_order_acquire); ++i)
		{
			const std::string context = "ctx_" + std::to_string(i);
			const u32 token = state.BeginOpticContext(
				context.c_str(), "weapon", i, "scope", 0, "identity", "diagnostic");
			if (!token)
			{
				failed.store(true, std::memory_order_release);
				break;
			}
			CSecondVPParams::OpticConfig identity;
			state.ReadOpticConfig(identity);
			auto config = MakeConfig(identity, 0.25f + static_cast<float>(i % 10) * 0.05f);
			CSecondVPParams::OpticPublication publication;
			if (!state.PrepareOpticConfig(token, config, publication) ||
				!state.PublishOpticConfig(token, publication))
			{
				failed.store(true, std::memory_order_release);
				break;
			}
		}
		stop.store(true, std::memory_order_release);
		for (auto& reader : readers)
			reader.join();
		CHECK(harness, !failed.load(std::memory_order_acquire));
	});
}

svp_mags_data Ladder(svp_mag_mode mode, std::initializer_list<float> values)
{
	svp_mags_data ladder;
	ladder.mode = mode;
	ladder.count = static_cast<u8>(values.size());
	std::copy(values.begin(), values.end(), ladder.values);
	return ladder;
}

void AddMagnificationTests(Harness& harness)
{
	harness.Run("magnification", "validation", [&]
	{
		CHECK(harness, svp_magnification_ladder_valid(Ladder(svp_mag_fixed, { 4.f })));
		CHECK(harness, svp_magnification_ladder_valid(
			Ladder(svp_mag_continuous, { 1.f, 6.f })));
		CHECK(harness, svp_magnification_ladder_valid(
			Ladder(svp_mag_detent, { 1.f, 1.5f, 4.f, 6.f })));
		CHECK_REJECT(harness,
			!svp_magnification_ladder_valid(Ladder(svp_mag_none, {})));
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(
			Ladder(svp_mag_fixed, { 1.f, 2.f })));
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(
			Ladder(svp_mag_continuous, { 1.f })));
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(
			Ladder(svp_mag_detent, { 1.f })));
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(
			Ladder(svp_mag_detent, { 1.f, 1.f })));
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(
			Ladder(svp_mag_detent, { 2.f, 1.f })));
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(
			Ladder(svp_mag_detent, { 0.f, 1.f })));
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(
			Ladder(svp_mag_detent, { 1.f, 201.f })));
		auto nonfinite = Ladder(svp_mag_detent, { 1.f, 2.f });
		nonfinite.values[1] = std::numeric_limits<float>::quiet_NaN();
		CHECK_REJECT(harness, !svp_magnification_ladder_valid(nonfinite));
	});

	harness.Run("magnification", "conversion_round_trips", [&]
	{
		const float magnifications[] = { 0.1f, 1.f, 1.5f, 4.f, 6.f, 20.f, 200.f };
		const float powers[] = { 0.25f, 1.f, 1.35f, 3.f };
		for (const float magnification : magnifications)
		{
			const float runtime = svp_magnification_to_runtime_factor(magnification);
			CHECK(harness, runtime > 0.f && std::isfinite(runtime));
			CHECK_NEAR(harness,
				svp_runtime_factor_to_magnification(runtime), magnification, 0.0001);
			for (const float power : powers)
			{
				const float weapon =
					svp_magnification_to_weapon_factor(magnification, power);
				CHECK_NEAR(harness,
					svp_weapon_factor_to_magnification(weapon, power),
					magnification, 0.0001);
			}
		}
		CHECK_REJECT(harness, svp_magnification_to_runtime_factor(0.f) == 0.f);
		CHECK_REJECT(harness, svp_magnification_to_runtime_factor(-1.f) == 0.f);
		CHECK_REJECT(harness, svp_runtime_factor_to_magnification(0.f) == 0.f);
		CHECK_REJECT(harness, svp_magnification_scroll_multiplier(0.f) == 1.f);
		CHECK_REJECT(harness, svp_magnification_scroll_multiplier(-1.f) == 1.f);
		CHECK_REJECT(harness, svp_magnification_scroll_multiplier(
			std::numeric_limits<float>::infinity()) == 1.f);
	});

	harness.Run("magnification", "detent_topology", [&]
	{
		const auto ladder = Ladder(svp_mag_detent, { 1.f, 1.5f, 4.f, 6.f });
		CHECK(harness, svp_magnification_exact_index(ladder, 4.f) == 2);
		CHECK(harness, svp_magnification_exact_index(ladder, 3.f) == -1);
		CHECK(harness, svp_magnification_nearest_index(ladder, 2.75f) == 1);
		CHECK(harness, svp_magnification_adjacent(ladder, 1.f, 1) == 1.5f);
		CHECK(harness, svp_magnification_adjacent(ladder, 1.5f, 1) == 4.f);
		CHECK(harness, svp_magnification_adjacent(ladder, 4.f, 1) == 6.f);
		CHECK(harness, svp_magnification_adjacent(ladder, 6.f, 1) == 6.f);
		CHECK(harness, svp_magnification_adjacent(ladder, 6.f, -1) == 4.f);
		CHECK(harness, svp_magnification_adjacent(ladder, 1.f, -1) == 1.f);
		CHECK(harness, svp_magnification_adjacent(ladder, 2.75f, 0) == 1.5f);
		CHECK(harness, svp_magnification_adjacent(ladder, 2.75f, 1) == 4.f);
		CHECK_REJECT(harness, svp_magnification_adjacent(
			Ladder(svp_mag_continuous, { 1.f, 6.f }), 1.f, 1) == 0.f);
	});

	harness.Run("magnification", "fingerprint", [&]
	{
		auto a = Ladder(svp_mag_detent, { 1.f, 1.5f, 4.f, 6.f });
		auto b = a;
		CHECK(harness, svp_magnification_fingerprint(a) ==
			svp_magnification_fingerprint(b));
		b.values[2] = 4.5f;
		CHECK(harness, svp_magnification_fingerprint(a) !=
			svp_magnification_fingerprint(b));
		b = a;
		b.values[10] = 99.f;
		b.fingerprint = 123;
		CHECK(harness, svp_magnification_fingerprint(a) ==
			svp_magnification_fingerprint(b));
		b = a;
		b.mode = svp_mag_continuous;
		CHECK(harness, svp_magnification_fingerprint(a) !=
			svp_magnification_fingerprint(b));
	});

	harness.Run("magnification", "randomized_properties", [&]
	{
		std::mt19937_64 random(harness.GetOptions().seed);
		std::uniform_real_distribution<float> increment(0.01f, 10.f);
		std::uniform_real_distribution<float> power(0.1f, 3.f);
		const u32 loops = harness.GetOptions().iterations;
		for (u32 iteration = 0; iteration < loops; ++iteration)
		{
			svp_mags_data ladder;
			ladder.mode = svp_mag_detent;
			ladder.count = static_cast<u8>(2 + random() % 15);
			float value = increment(random);
			for (u32 i = 0; i < ladder.count; ++i)
			{
				value = std::min(199.f, value + increment(random));
				ladder.values[i] = value;
				if (value >= 199.f && i + 1 < ladder.count)
				{
					ladder.count = static_cast<u8>(i + 1);
					break;
				}
			}
			if (ladder.count < 2 || !svp_magnification_ladder_valid(ladder))
				continue;
			const u32 index = static_cast<u32>(random() % ladder.count);
			const float magnification = ladder.values[index];
			const float scroll = power(random);
			const float weapon =
				svp_magnification_to_weapon_factor(magnification, scroll);
			CHECK_NEAR(harness,
				svp_weapon_factor_to_magnification(weapon, scroll),
				magnification, 0.001);
			const float forward =
				svp_magnification_adjacent(ladder, magnification, 1);
			const float backward =
				svp_magnification_adjacent(ladder, magnification, -1);
			CHECK(harness, forward == ladder.values[std::min<u32>(
				index + 1, ladder.count - 1)]);
			CHECK(harness, backward == ladder.values[index ? index - 1 : 0]);
		}
	});
}

std::vector<svp_v3> Ring(float z, float radius, int count,
	float center_x = 0.f, float center_y = 0.f)
{
	std::vector<svp_v3> points;
	points.reserve(static_cast<std::size_t>(count));
	constexpr double pi = 3.14159265358979323846;
	for (int i = 0; i < count; ++i)
	{
		const double angle = 2.0 * pi * static_cast<double>(i) /
			static_cast<double>(count);
		points.push_back({
			center_x + radius * static_cast<float>(std::cos(angle)),
			center_y + radius * static_cast<float>(std::sin(angle)),
			z
		});
	}
	return points;
}

void AddPhysicalOpticsTests(Harness& harness)
{
	using namespace SvpPhysicalOptics;
	harness.Run("physical-optics", "response_and_interpolation", [&]
	{
		MagnificationResponse response = { { 1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 20.f } };
		CHECK(harness, SampleMagnificationResponse(response, 0.5f) == 1.f);
		CHECK(harness, SampleMagnificationResponse(response, 1.f) == 1.f);
		CHECK(harness, SampleMagnificationResponse(response, 2.f) == 2.f);
		CHECK_NEAR(harness, SampleMagnificationResponse(response, 1.5f), 1.5f, 0.00001);
		CHECK(harness, SampleMagnificationResponse(response, 30.f) == 20.f);
		CHECK(harness, ApplyMagnificationResponse(response, 2.f, 2.f, -1.f) == 3.f);
		CHECK(harness, ApplyMagnificationResponse(response, 1.f, -1.f, 0.f) == 0.f);
		CHECK(harness, MagnificationFraction(0.f, 1.f, 6.f) == 0.f);
		CHECK(harness, MagnificationFraction(6.f, 1.f, 6.f) == 1.f);
		CHECK_NEAR(harness, MagnificationFraction(3.5f, 1.f, 6.f), 0.5f, 0.00001);
		CHECK(harness, MagnificationFraction(2.f, 2.f, 2.f) == 0.f);
		CHECK_NEAR(harness,
			InterpolateMagnification(10.f, 20.f, 3.5f, 1.f, 6.f), 15.f, 0.00001);
		CHECK(harness,
			InterpolateReciprocalMagnification(10.f, 20.f, 1.f, 1.f, 6.f) == 10.f);
		CHECK(harness,
			InterpolateReciprocalMagnification(10.f, 20.f, 6.f, 1.f, 6.f) == 20.f);
		CHECK(harness,
			InterpolateReciprocalMagnification(10.f, 20.f, 2.f, 0.f, 6.f) == 10.f);
	});

	harness.Run("physical-optics", "objective_registration", [&]
	{
		const Vec2 radius = { 0.025f, 0.025f };
		const auto centered = MapObjectiveAxisToEyepiece(
			{ 0.f, 0.f, -0.1f }, { 0.f, 0.f, 0.2f }, radius);
		CHECK(harness, centered.valid);
		CHECK(harness, centered.inside_aperture);
		CHECK_NEAR(harness, centered.fraction, 1.f / 3.f, 0.00001);
		CHECK_NEAR(harness, centered.principal.x, 0.f, 0.00001);
		CHECK_NEAR(harness, centered.principal.y, 0.f, 0.00001);
		const auto shifted = MapObjectiveAxisToEyepiece(
			{ 0.01f, -0.006f, -0.1f }, { 0.004f, 0.003f, 0.2f }, radius);
		CHECK(harness, shifted.valid);
		CHECK_NEAR(harness, shifted.hit.x, 0.008f, 0.00001);
		CHECK_NEAR(harness, shifted.hit.y, -0.003f, 0.00001);
		CHECK_NEAR(harness, shifted.principal.x, 0.32f, 0.00001);
		CHECK_NEAR(harness, shifted.principal.y, -0.12f, 0.00001);
		const auto scaled = MapObjectiveAxisToEyepiece(
			{ 0.1f, -0.06f, -1.f }, { 0.04f, 0.03f, 2.f }, { 0.25f, 0.25f });
		CHECK_NEAR(harness, scaled.principal.x, shifted.principal.x, 0.00001);
		CHECK_NEAR(harness, scaled.principal.y, shifted.principal.y, 0.00001);
		CHECK_REJECT(harness, !MapObjectiveAxisToEyepiece(
			{ 0.f, 0.f, 0.1f }, { 0.f, 0.f, 0.2f }, radius).valid);
		CHECK_REJECT(harness, !MapObjectiveAxisToEyepiece(
			{ 0.f, 0.f, -0.1f }, { 0.f, 0.f, -0.2f }, radius).valid);
		CHECK_REJECT(harness, !MapObjectiveAxisToEyepiece(
			{ 0.f, 0.f, -0.1f }, { 0.f, 0.f, 0.2f }, { 0.f, 0.025f }).valid);
		CHECK_REJECT(harness, !MapObjectiveAxisToEyepiece(
			{ std::numeric_limits<float>::quiet_NaN(), 0.f, -0.1f },
			{ 0.f, 0.f, 0.2f }, radius).valid);
	});

	harness.Run("physical-optics", "eye_tracking", [&]
	{
		const Vec2 limited = LimitEyeOffset({ 3.f, 4.f }, 2.f);
		CHECK_NEAR(harness, limited.x, 1.2f, 0.00001);
		CHECK_NEAR(harness, limited.y, 1.6f, 0.00001);
		const Vec2 zero = LimitEyeOffset({ 3.f, 4.f }, -1.f);
		CHECK_NEAR(harness, zero.x, 0.f, 0.00001);
		CHECK_NEAR(harness, zero.y, 0.f, 0.00001);

		Vec2 velocity = {};
		AccelerateEye(velocity, { 3.f, 4.f }, 2.f);
		CHECK_NEAR(harness, velocity.x, 1.2f, 0.00001);
		CHECK_NEAR(harness, velocity.y, 1.6f, 0.00001);
		AccelerateEye(velocity, { 3.f, 4.f }, 10.f);
		CHECK_NEAR(harness, velocity.x, 3.f, 0.00001);
		CHECK_NEAR(harness, velocity.y, 4.f, 0.00001);

		EyeTrackingState state;
		UpdateEyeTracking(state, { 5.f, -2.f }, false, 1, 10, 0.016f, 5.f, 80.f);
		CHECK(harness, state.valid);
		CHECK_NEAR(harness, state.offset.x, 5.f, 0.00001);
		CHECK_NEAR(harness, state.offset.y, -2.f, 0.00001);
		const Vec2 same_frame = state.offset;
		UpdateEyeTracking(state, { 0.f, 0.f }, false, 1, 10, 0.016f, 5.f, 80.f);
		CHECK(harness, state.offset.x == same_frame.x && state.offset.y == same_frame.y);
		UpdateEyeTracking(state, { 0.f, 0.f }, true, 1, 11, 0.016f, 5.f, 80.f);
		CHECK(harness, state.velocity.x == 0.f && state.velocity.y == 0.f);
		CHECK(harness, state.offset.x == same_frame.x && state.offset.y == same_frame.y);
		UpdateEyeTracking(state, { 1.f, 1.f }, false, 2, 12, 0.016f, 5.f, 80.f);
		CHECK_NEAR(harness, state.offset.x, 1.f, 0.00001);
		CHECK_NEAR(harness, state.offset.y, 1.f, 0.00001);
	});

	harness.Run("physical-optics", "randomized_invariants", [&]
	{
		std::mt19937_64 random(harness.GetOptions().seed ^ 0x50485953ull);
		std::uniform_real_distribution<float> lateral(-0.05f, 0.05f);
		std::uniform_real_distribution<float> rear(-1.f, -0.001f);
		std::uniform_real_distribution<float> front(0.001f, 1.f);
		std::uniform_real_distribution<float> radius(0.001f, 0.1f);
		for (u32 i = 0; i < harness.GetOptions().iterations; ++i)
		{
			const Vec3 eye = { lateral(random), lateral(random), rear(random) };
			const Vec3 objective = { lateral(random), lateral(random), front(random) };
			const Vec2 aperture = { radius(random), radius(random) };
			const auto registration =
				MapObjectiveAxisToEyepiece(eye, objective, aperture);
			CHECK(harness, registration.valid);
			CHECK(harness, Finite(registration.hit));
			CHECK(harness, Finite(registration.principal));
			CHECK(harness, registration.fraction >= 0.f && registration.fraction <= 1.f);
			const bool inside = registration.principal.x * registration.principal.x +
				registration.principal.y * registration.principal.y <= 1.f;
			CHECK(harness, registration.inside_aperture == inside);
		}
	});
}

void AddLensDetectionTests(Harness& harness)
{
	harness.Run("lens-detection", "built_in_fixture", [&]
	{
		CHECK(harness, svp_lens_detect_selftest());
	});

	harness.Run("lens-detection", "fit_split_and_detect", [&]
	{
		auto rear = Ring(0.f, 0.02f, 64);
		auto front = Ring(0.12f, 0.03f, 64, 0.001f, -0.002f);
		SDiscFit rear_fit = svp_fit_disc(rear.data(), static_cast<int>(rear.size()));
		CHECK(harness, rear_fit.valid);
		CHECK_NEAR(harness, rear_fit.radius, 0.02f, 0.0001);
		CHECK(harness, rear_fit.normal.z > 0.999f);
		CHECK_REJECT(harness, !svp_fit_disc(nullptr, 64).valid);
		CHECK_REJECT(harness,
			!svp_fit_disc(rear.data(), FIT_MIN_POINTS - 1).valid);
		auto oversized = Ring(0.f, FIT_MAX_RADIUS + 0.01f, 64);
		CHECK_REJECT(harness, !svp_fit_disc(
			oversized.data(), static_cast<int>(oversized.size())).valid);

		std::vector<svp_v3> combined = rear;
		combined.insert(combined.end(), front.begin(), front.end());
		std::vector<std::vector<svp_v3>> clusters;
		svp_split_along_axis(combined.data(), static_cast<int>(combined.size()),
			{ 0.f, 0.f, 1.f }, clusters);
		CHECK(harness, clusters.size() == 2);
		const auto result = svp_detect_lens_discs(
			combined, { 0.f, 0.f, 1.f }, 7);
		CHECK(harness, result.ok);
		CHECK(harness, result.eyepiece.valid);
		CHECK(harness, result.objective.valid);
		CHECK(harness, result.source == 7);
		CHECK(harness, result.vert_count == static_cast<int>(combined.size()));
		CHECK_NEAR(harness, result.eyepiece.center.z, 0.f, 0.0001);
		CHECK_NEAR(harness, result.objective.center.z, 0.12f, 0.0001);
	});

	harness.Run("lens-detection", "axis_offset_and_units", [&]
	{
		const svp_v3 positive = svp_axis_seed_for({ 0.f, 0.f, 1.f });
		const svp_v3 negative = svp_axis_seed_for({ 0.f, 0.f, -1.f });
		const svp_v3 fallback = svp_axis_seed_for({ 1.f, 0.f, 0.f });
		CHECK_NEAR(harness, positive.z, 1.f, 0.00001);
		CHECK_NEAR(harness, negative.z, 1.f, 0.00001);
		CHECK_NEAR(harness, fallback.z, 1.f, 0.00001);
		CHECK_NEAR(harness, svp_mm_suggestion(0.017675f), 35.35f, 0.0001);
		const auto offset = svp_lens_offset_from_centers(
			{ 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f }, 0.02f,
			{ 0.002f, -0.004f, 0.2f }, 0.03f);
		CHECK_NEAR(harness, offset.z, 10.f, 0.0001);
		CHECK_NEAR(harness, offset.w, 1.5f, 0.0001);
		const auto scaled = svp_lens_offset_from_centers(
			{ 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f }, 0.2f,
			{ 0.02f, -0.04f, 2.f }, 0.3f);
		CHECK_NEAR(harness, offset.x, scaled.x, 0.0001);
		CHECK_NEAR(harness, offset.y, scaled.y, 0.0001);
		CHECK_NEAR(harness, offset.z, scaled.z, 0.0001);
		CHECK_NEAR(harness, offset.w, scaled.w, 0.0001);
	});

	harness.Run("lens-detection", "tube_march", [&]
	{
		std::vector<svp_v3> tube;
		for (int slab = 0; slab <= 20; ++slab)
		{
			auto ring = Ring(static_cast<float>(slab) * 0.005f, 0.02f, 16);
			tube.insert(tube.end(), ring.begin(), ring.end());
		}
		SDiscFit objective = {};
		CHECK(harness, svp_tube_march_objective(
			tube, { 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }, 0.01f, objective));
		CHECK(harness, objective.valid);
		CHECK_NEAR(harness, objective.center.z, 0.1f, 0.006);
		CHECK_NEAR(harness, objective.radius, 0.02f, 0.001);
		CHECK_REJECT(harness, !svp_tube_march_objective(
			{}, { 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }, 0.01f, objective));
		CHECK_REJECT(harness, !svp_tube_march_objective(
			tube, { 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }, 0.f, objective));
	});

	harness.Run("lens-detection", "randomized_stability", [&]
	{
		std::mt19937_64 random(harness.GetOptions().seed ^ 0x4c454e53ull);
		std::uniform_real_distribution<float> value(-1.f, 1.f);
		const u32 loops = std::max<u32>(1000, harness.GetOptions().iterations / 4);
		for (u32 iteration = 0; iteration < loops; ++iteration)
		{
			const int count = static_cast<int>(random() % 96);
			std::vector<svp_v3> points;
			points.reserve(static_cast<std::size_t>(count));
			for (int i = 0; i < count; ++i)
				points.push_back({ value(random), value(random), value(random) });
			const SDiscFit fit = svp_fit_disc(
				points.empty() ? nullptr : points.data(), count);
			if (fit.valid)
			{
				CHECK(harness, Finite(fit.center));
				CHECK(harness, Finite(fit.normal));
				CHECK(harness, std::isfinite(fit.radius) && fit.radius >= 0.f);
				CHECK(harness, std::isfinite(fit.rms) && fit.rms >= 0.f);
			}
			std::vector<std::vector<svp_v3>> clusters;
			svp_split_along_axis(points.empty() ? nullptr : points.data(), count,
				{ value(random), value(random), value(random) }, clusters);
			std::size_t clustered = 0;
			for (const auto& cluster : clusters)
				clustered += cluster.size();
			CHECK(harness, clustered == points.size());
		}
	});
}

std::vector<std::string>* g_lua_logs = nullptr;

int LuaTestLog(lua_State* state)
{
	const char* message = luaL_checkstring(state, 1);
	if (g_lua_logs)
		g_lua_logs->emplace_back(message ? message : "");
	return 0;
}

std::string LuaQuote(const std::filesystem::path& path)
{
	const std::string input = path.string();
	std::string output = "\"";
	for (const char ch : input)
	{
		if (ch == '\\' || ch == '"')
			output.push_back('\\');
		output.push_back(ch);
	}
	output.push_back('"');
	return output;
}

bool LuaDoString(lua_State* state, const std::string& text,
	std::string& detail)
{
	if (luaL_dostring(state, text.c_str()) == 0)
		return true;
	const char* error = lua_tostring(state, -1);
	detail = error ? error : "Lua execution failed";
	lua_pop(state, 1);
	return false;
}

bool LuaDoFile(lua_State* state, const std::filesystem::path& path,
	std::string& detail)
{
	if (luaL_dofile(state, path.string().c_str()) == 0)
		return true;
	const char* error = lua_tostring(state, -1);
	detail = path.string() + " " +
		(error ? error : "Lua file failed");
	lua_pop(state, 1);
	return false;
}

void RegisterOpticApi(lua_State* state)
{
	using namespace luabind;
	module(state)
	[
		def("svp_optic_api_version", &svp_optic_api_version),
		def("svp_optic_api_info", &svp_optic_api_info),
		def("svp_optic_api_has_capability", &svp_optic_api_has_capability),
		def("svp_optic_api_connect", &svp_optic_api_connect),
		def("svp_optic_api_describe", &svp_optic_api_describe),
		def("svp_validate_optic_fields", &svp_validate_optic_fields),
		def("svp_validate_optic_profile", &svp_validate_optic_profile),
		def("svp_optic_route_epoch", &svp_optic_route_epoch),
		def("svp_begin_optic_context", &svp_begin_optic_context),
		def("svp_apply_optic_profile", &svp_apply_optic_profile),
		def("svp_clear_optic_profile", &svp_clear_optic_profile),
		def("svp_current_optic_profile", &svp_current_optic_profile)
	];
}

void* __cdecl TestLuabindAllocator(
	luabind::memory_allocation_function_parameter,
	void const* pointer, size_t size)
{
	if (!size)
	{
		LPVOID storage = const_cast<LPVOID>(pointer);
		xr_free(storage);
		return nullptr;
	}
	if (!pointer)
		return Memory.mem_alloc(size);
	return Memory.mem_realloc(const_cast<void*>(pointer), size);
}

void AddOpticApiTests(Harness& harness)
{
	harness.Run("optic-api", "lua_contract_fixture", [&]
	{
		const auto profile_path =
			harness.GetOptions().repo / "gamedata/scripts/pip_optic_profile.script";
		const auto fixture_path =
			harness.GetOptions().repo / "gamedata/scripts/pip_optic_api_fixture.script";
		CHECK(harness, std::filesystem::is_regular_file(profile_path));
		CHECK(harness, std::filesystem::is_regular_file(fixture_path));
		harness.Trace("files");

		lua_State* state = luaL_newstate();
		CHECK(harness, state != nullptr);
		harness.Trace("state");
		struct LuaCloser
		{
			lua_State* state;
			~LuaCloser()
			{
				svp_test_client_attach(nullptr);
				lua_close(state);
			}
		} closer{ state };
		luaL_openlibs(state);
		harness.Trace("libs");
		luabind::allocator = &TestLuabindAllocator;
		luabind::allocator_parameter = nullptr;
		CHECK(harness,
			luabind::detail::class_registry::get_registry(state) == nullptr);
		harness.Trace("registry");
		luabind::open(state);
		harness.Trace("luabind");
		svp_test_client_attach(state);
		svp_test_client_set_scope_mode(2);
		RegisterOpticApi(state);
		harness.Trace("api");

		std::vector<std::string> logs;
		g_lua_logs = &logs;
		struct LuaLogReset
		{
			~LuaLogReset() { g_lua_logs = nullptr; }
		} log_reset;
		lua_pushcfunction(state, LuaTestLog);
		lua_setglobal(state, "svp_test_log");
		std::string lua_error;
		if (!LuaDoString(state, R"lua(
function printf(format, ...)
    svp_test_log(string.format(format, ...))
end
local function make_ini(path)
    local values = {}
    if path == "pip_optic_profiles.ltx" then
        values.default = { reticle_illum = 1 }
    end
    local result = {}
    function result:section_exist(section)
        return values[section] ~= nil
    end
    function result:line_exist()
        return false
    end
    function result:r_float_ex(section, key)
        return values[section] and values[section][key] or nil
    end
    function result:r_string_ex()
        return nil
    end
    function result:r_bool_ex(_, _, fallback)
        return fallback
    end
    function result:dltx_get_filename_of_line()
        return nil
    end
    return result
end
function ini_file(path)
    return make_ini(path)
end
ini_sys = make_ini("system")
function get_console()
    return { get_integer = function() return 0 end }
end
function getFS()
    return { exist = function() return false end }
end
function exec_console_cmd()
end
)lua", lua_error))
			FAIL(harness, lua_error);
		harness.Trace("stubs");
		const std::string load_profile =
			"pip_optic_profile = {}\n"
			"setmetatable(pip_optic_profile, { __index = _G })\n"
			"local chunk = assert(loadfile(" + LuaQuote(profile_path) + "))\n"
			"setfenv(chunk, pip_optic_profile)\n"
			"chunk()\n";
		if (!LuaDoString(state, load_profile, lua_error))
			FAIL(harness, lua_error);
		harness.Trace("profile");
		if (!LuaDoFile(state, fixture_path, lua_error))
			FAIL(harness, lua_error);
		harness.Trace("fixture");

		lua_getglobal(state, "run");
		if (!lua_isfunction(state, -1))
			FAIL(harness, "fixture run function missing");
		if (lua_pcall(state, 0, 1, 0) != 0)
		{
			const char* error = lua_tostring(state, -1);
			const std::string detail =
				error ? error : "fixture run failed";
			lua_pop(state, 1);
			FAIL(harness, detail);
		}
		harness.Trace("run");
		const bool passed = lua_toboolean(state, -1) != 0;
		lua_pop(state, 1);
		if (!passed)
		{
			std::ostringstream detail;
			detail << "Lua optic fixture failed";
			for (const std::string& line : logs)
				if (line.find("pass=false") != std::string::npos ||
					line.find("complete") != std::string::npos)
					detail << " | " << line;
			FAIL(harness, detail.str());
		}
		std::size_t cases = 0;
		std::size_t rejections = 0;
		for (const std::string& line : logs)
			if (line.find("[SVP-API-FIXTURE] case=") == 0)
			{
				++cases;
				if (line.find(" kind=rejection ") != std::string::npos)
					++rejections;
			}
		harness.AddRejections(rejections);
		std::ostringstream fixture_counts;
		fixture_counts << "cases=" << cases << " rejections=" << rejections;
		harness.Trace(fixture_counts.str());
		CHECK(harness, cases == 256);
		CHECK(harness, rejections == 129);
	});

	harness.Run("optic-api", "route_epoch_reset", [&]
	{
		svp_test_client_set_scope_mode(2);
		const u32 before = svp_optic_route_epoch();
		CHECK(harness, before != 0);
		svp_test_client_set_scope_mode(0);
		const u32 disabled = svp_optic_route_epoch();
		CHECK(harness, disabled != 0 && disabled != before);
		svp_test_client_set_scope_mode(2);
		const u32 enabled = svp_optic_route_epoch();
		CHECK(harness, enabled != 0 && enabled != disabled);
	});
}

bool ParseUnsigned(const std::string& value, std::uint64_t maximum,
	std::uint64_t& parsed)
{
	if (value.empty() || value[0] == '-')
		return false;
	errno = 0;
	char* end = nullptr;
	const unsigned long long result =
		std::strtoull(value.c_str(), &end, 0);
	if (errno == ERANGE || end == value.c_str() || *end != 0 ||
		result > maximum)
		return false;
	parsed = static_cast<std::uint64_t>(result);
	return true;
}

bool ParseOptions(int argc, char** argv, Options& options,
	std::string& error, bool& help)
{
	std::error_code path_error;
	options.repo = std::filesystem::current_path(path_error);
	if (path_error)
	{
		error = "cannot read current directory " + path_error.message();
		return false;
	}
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		auto require_value = [&](const char* name, std::string& value)
		{
			if (i + 1 >= argc)
			{
				error = std::string("missing value for ") + name;
				return false;
			}
			value = argv[++i];
			return true;
		};
		std::string value;
		if (argument == "--suite")
		{
			if (!require_value("--suite", options.suite))
				return false;
		}
		else if (argument == "--format")
		{
			if (!require_value("--format", options.format))
				return false;
		}
		else if (argument == "--repo")
		{
			if (!require_value("--repo", value))
				return false;
			options.repo = value;
		}
		else if (argument == "--seed")
		{
			if (!require_value("--seed", value) ||
				!ParseUnsigned(value,
					std::numeric_limits<std::uint64_t>::max(),
					options.seed))
			{
				error = "invalid seed";
				return false;
			}
		}
		else if (argument == "--iterations")
		{
			std::uint64_t parsed = 0;
			if (!require_value("--iterations", value) ||
				!ParseUnsigned(value,
					std::numeric_limits<std::uint32_t>::max(), parsed) ||
				!parsed)
			{
				error = "invalid iterations";
				return false;
			}
			options.iterations = static_cast<std::uint32_t>(parsed);
		}
		else if (argument == "--verbose")
			options.verbose = true;
		else if (argument == "--help")
		{
			help = true;
			return true;
		}
		else
		{
			error = "unknown argument " + argument;
			return false;
		}
	}
	const std::set<std::string> suites =
	{
		"all", "schema", "state", "concurrency", "magnification",
		"physical-optics", "lens-detection", "optic-api"
	};
	if (!suites.count(options.suite))
	{
		error = "unknown suite " + options.suite;
		return false;
	}
	if (options.format != "text" && options.format != "json")
	{
		error = "unknown format " + options.format;
		return false;
	}
	options.repo =
		std::filesystem::absolute(options.repo, path_error).lexically_normal();
	if (path_error)
	{
		error = "cannot resolve repository path " + path_error.message();
		return false;
	}
	return true;
}
}

int main(int argc, char** argv)
{
	Options options;
	std::string error;
	bool help = false;
	if (!ParseOptions(argc, argv, options, error, help))
	{
		std::cerr << "svp test client error " << error << '\n';
		return 2;
	}
	if (help)
	{
		std::cout
			<< "svp-test-client [--suite name] [--format text|json]"
			<< " [--repo path] [--seed value] [--iterations count] [--verbose]\n"
			<< "suites schema state concurrency magnification physical-optics"
			<< " lens-detection optic-api all\n";
		return 0;
	}
	Harness harness(std::move(options));
	AddSchemaTests(harness);
	AddStateTests(harness);
	AddMagnificationTests(harness);
	AddPhysicalOpticsTests(harness);
	AddLensDetectionTests(harness);
	AddOpticApiTests(harness);
	return harness.Finish();
}
