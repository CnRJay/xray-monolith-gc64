#pragma once

// extra functions
// Base case for hash_combine with one argument
IC size_t hash_combine(size_t value) noexcept
{
	return value;
}

// Recursive case for hash_combine with multiple arguments
// Usage example: size_t h = hash_combine(0, h1, h2);
template<typename... Args>
IC size_t hash_combine(size_t seed, size_t value, Args... args) noexcept
{
	if constexpr (sizeof(size_t) >= 8)
		seed ^= value + 0x517cc1b727220a95 + (seed << 6) + (seed >> 2);
	else
		seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	return hash_combine(seed, args...);
}
