#ifndef __FLAGS_H__
#define __FLAGS_H__

#include <bitset>
#include <map>
#include <string_view>
#include "../3rd party/magic_enum/magic_enum.hpp"

template <class T>
struct _flags
{
public:
	typedef T TYPE;
	typedef _flags<T> Self;
	typedef Self& SelfRef;
	typedef const Self& SelfCRef;
public:
	T flags;

	IC TYPE get() const { return flags; }
	IC SelfRef zero()
	{
		flags = T(0);
		return *this;
	}

	IC SelfRef one()
	{
		flags = T(-1);
		return *this;
	}

	IC SelfRef invert()
	{
		flags = ~flags;
		return *this;
	}

	IC SelfRef invert(const Self& f)
	{
		flags = ~f.flags;
		return *this;
	}

	IC SelfRef invert(const T mask)
	{
		flags ^= mask;
		return *this;
	}

	IC SelfRef assign(const Self& f)
	{
		flags = f.flags;
		return *this;
	}

	IC SelfRef assign(const T mask)
	{
		flags = mask;
		return *this;
	}

	IC SelfRef set(const T mask, BOOL value)
	{
		if (value) flags |= mask;
		else flags &= ~mask;
		return *this;
	}

	IC BOOL is(const T mask) const { return mask == (flags & mask); }
	IC BOOL is_any(const T mask) const { return BOOL(!!(flags & mask)); }
	IC BOOL test(const T mask) const { return BOOL(!!(flags & mask)); }
	IC SelfRef or(const T mask)
	{
		flags |= mask;
		return *this;
	}

	IC SelfRef or(const Self& f, const T mask)
	{
		flags = f.flags | mask;
		return *this;
	}

	IC SelfRef and(const T mask)
	{
		flags &= mask;
		return *this;
	}

	IC SelfRef and(const Self& f, const T mask)
	{
		flags = f.flags & mask;
		return *this;
	}

	IC BOOL equal(const Self& f) const { return flags == f.flags; }
	IC BOOL equal(const Self& f, const T mask) const { return (flags & mask) == (f.flags & mask); }
};

typedef _flags<u8> Flags8;
typedef _flags<u8> flags8;
typedef _flags<u16> Flags16;
typedef _flags<u16> flags16;
typedef _flags<u32> Flags32;
typedef _flags<u32> flags32;
typedef _flags<u64> Flags64;
typedef _flags<u64> flags64;

// demonized: bitset flags for flag enums with more than 64 possible values
/* https://m-peko.github.io/craft-cpp/posts/different-ways-to-define-binary-flags/ */
template <typename EnumT>
class xr_bitsetflags {
	static_assert(std::is_enum_v<EnumT>, "xr_bitsetflags can only be specialized for enum types");

	using UnderlyingT = typename std::make_unsigned_t<typename std::underlying_type_t<EnumT>>;

public:
	IC xr_bitsetflags& set(EnumT e, bool value = true) noexcept {
		bits_.set(underlying(e), value);
		return *this;
	}

	IC xr_bitsetflags& reset(EnumT e) noexcept {
		set(e, false);
		return *this;
	}

	IC xr_bitsetflags& reset() noexcept {
		bits_.reset();
		return *this;
	}

	[[nodiscard]] IC bool all() const noexcept {
		return bits_.all();
	}

	[[nodiscard]] IC bool any() const noexcept {
		return bits_.any();
	}

	[[nodiscard]] IC bool none() const noexcept {
		return bits_.none();
	}

	[[nodiscard]] IC constexpr std::size_t size() const noexcept {
		return enum_size;
	}

	[[nodiscard]] IC std::size_t count() const noexcept {
		return bits_.count();
	}

	IC constexpr bool operator[](EnumT e) const {
		return bits_[underlying(e)];
	}

	IC constexpr BOOL test(EnumT e) const {
		return !!bits_[underlying(e)];
	}

	IC constexpr BOOL test(const std::size_t pos) const {
		return bits_.test(pos);
	}

	IC xr_bitsetflags& zero() noexcept
	{
		return reset();
	}

	IC xr_bitsetflags& set() noexcept
	{
		bits_.set();
		return *this;
	}

	IC xr_bitsetflags& one() noexcept
	{
		return set();
	}

	IC xr_bitsetflags& flip() noexcept
	{
		bits_.flip();
		return *this;
	}

	IC xr_bitsetflags& invert() noexcept
	{
		return flip();
	}

	IC std::map<std::string, bool> getBitsetAsMap() const {
		std::map<std::string, bool> result;
		for (std::size_t pos = 0; pos < enum_size; pos++)
		{
			EnumT value = magic_enum::enum_value<EnumT>(pos);
			std::string_view name = magic_enum::enum_name(value);
			BOOL r = test(pos);
			result.emplace(name, r);
		}
		return result;
	}

private:
	static constexpr UnderlyingT underlying(EnumT e) {
		return static_cast<UnderlyingT>(e);
	}

private:
	// Use magic_enum to get the enum size at compile time
	static constexpr std::size_t enum_size = magic_enum::enum_count<EnumT>();
	std::bitset<enum_size> bits_;
};

#endif //__FLAGS_H__
