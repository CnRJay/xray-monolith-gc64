#pragma once
#include <memory>

#ifdef DEBUG_MEMORY_NAME
template <class T, typename... Args>
IC T* xr_new(Args&&... args)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new(ptr) T(std::forward<Args>(args)...);
}

#else // DEBUG_MEMORY_NAME

// Update xr_new to look for this marker
template <typename T, typename = void>
struct handles_own_init : std::false_type {};

template <typename T>
struct handles_own_init<T, std::void_t<typename T::NoZeroMemoryNew>> : T::NoZeroMemoryNew {};

// Update xr_new to check for `using NoZeroMemoryNew` in class
template <class T, typename... Args>
IC T* xr_new(Args&&... args)
{
    size_t size = sizeof(T);
    constexpr bool bZero = !handles_own_init<T>::value;
    if constexpr (bZero)
    {
        /*if (size > 1024)
            Msg("~ [MEM] allocating object of %s, %zu", typeid(T).name(), size);*/
    }
    T* ptr = (T*)Memory.mem_alloc(size, bZero);
    return new(ptr) T(std::forward<Args>(args)...);
}

#endif // DEBUG_MEMORY_NAME

#include <fast_dynamic_cast/fast_dynamic_cast.hpp>

template <bool _is_pm, typename T>
struct xr_special_free
{
	IC void operator()(T*& ptr)
	{
		if (ptr == nullptr)
		{
			return;
		}

		if constexpr (_is_pm)
		{
			void* _real_ptr = fast_dynamic_cast<void*>(ptr);
			ptr->~T();
			Memory.mem_free(_real_ptr);
		}
		else
		{
			ptr->~T();
			Memory.mem_free(ptr);
		}
	}
};

template <typename T>
struct xr_special_free<false, T>
{
	IC void operator()(T*& ptr)
	{
		if (ptr == nullptr)
		{
			return;
		}
		
		ptr->~T();
		Memory.mem_free(ptr);
	}
};

template <class T>
IC void xr_delete(T*& ptr)
{
	if (ptr)
	{
		xr_special_free<std::is_polymorphic<T>::value, T>()(ptr);
		ptr = nullptr;
	}
}
template <class T>
IC void xr_delete(T* const& ptr)
{
	if (ptr)
	{
		xr_special_free<std::is_polymorphic<T>::value, T>()(const_cast<T*&>(ptr));
		const_cast<T*&>(ptr) = nullptr;
	}
}

#ifdef DEBUG_MEMORY_MANAGER
void XRCORE_API mem_alloc_gather_stats(const bool& value);
void XRCORE_API mem_alloc_gather_stats_frequency(const float& value);
void XRCORE_API mem_alloc_show_stats();
void XRCORE_API mem_alloc_clear_stats();
#endif // DEBUG_MEMORY_MANAGER

template <typename T>
struct xr_allocator_shared_helper {
    using value_type = T;
    xr_allocator_shared_helper() = default;
    template <class U> xr_allocator_shared_helper(const xr_allocator_shared_helper<U>&) {}
    T* allocate(std::size_t n) {
        return static_cast<T*>(Memory.mem_alloc(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept {
        Memory.mem_free(p);
    }
};

template<typename T>
using xr_weak_ptr = std::weak_ptr<T>;

template<typename T>
using xr_shared_ptr = std::shared_ptr<T>;

template<typename T>
using xr_unique_ptr = std::unique_ptr<T, xr_special_free<false, T>>;

template <class T, class... Args>
xr_shared_ptr<T> xr_make_shared(Args&&... args)
{
	xr_allocator_shared_helper<T> alloc;
	return std::allocate_shared<T>(alloc, std::forward<Args>(args)...);
}

// Support for objects already created with a custom deleter
// Useful for the "Null Deleter" or specialized engine cleanup
template <class T, class D>
xr_shared_ptr<T> xr_make_shared_with_deleter(T* ptr, D deleter)
{
    xr_allocator_shared_helper<T> alloc;
    // We use the constructor here because allocate_shared doesn't take deleters
    return xr_shared_ptr<T>(ptr, deleter, alloc);
}

template <typename T, typename... ARGS>
xr_unique_ptr<T> xr_make_unique(ARGS&&... args)
{
	void* TypeMem = Memory.mem_alloc(sizeof(T));
	new (TypeMem)T(std::forward<ARGS>(args)...);
	return xr_unique_ptr<T>(static_cast<T*>(TypeMem), xr_special_free<false, T>{});
}
