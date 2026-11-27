// Copyright (c) 2003 Daniel Wallin and Arvid Norberg

// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <type_traits>
#include <utility>
#include <vector>
#include <string>

#include <luabind/config.hpp>
#include <luabind/detail/class_rep.hpp>

namespace luabind
{
	struct value;
	struct value_vector;

	struct value
	{
		friend class vector_class<value>;

		template<class T>
		value(const char* name, T v)
			: name_(name)
			, val_(static_cast<int>(v))
		{
		}

		const char* name_;
		int val_;

		inline value_vector operator,(const value& rhs) const;

	private:
		value() {}
	};

	struct value_vector : public vector_class<value>
	{
		// a bug in intel's compiler forces us to declare these constructors explicitly.
		value_vector();
		virtual ~value_vector();
		value_vector(const value_vector& v);
		value_vector& operator,(const value& rhs);
	};

	inline value_vector value::operator,(const value& rhs) const
	{
		value_vector v;
		v.push_back(*this);
		v.push_back(rhs);
		return v;
	}

	inline value_vector::value_vector()
		: vector_class<value>()
	{
	}

	inline value_vector::~value_vector() {}

	inline value_vector::value_vector(const value_vector& rhs)
		: vector_class<value>(rhs)
	{
	}

	inline value_vector& value_vector::operator,(const value& rhs)
	{
		push_back(rhs);
		return *this;
	}

	namespace detail
	{
		template<typename T>
		struct is_value_type : std::false_type {};

		template<> struct is_value_type<value> : std::true_type {};
		template<> struct is_value_type<value_vector> : std::true_type {};

		template<typename T>
		inline constexpr bool is_value_type_v = is_value_type<std::decay_t<T>>::value;

		template<typename... Ts>
		using all_values_t = std::conjunction<std::bool_constant<is_value_type_v<Ts>>...>;

		template<typename From>
		struct enum_maker
		{
			explicit enum_maker(From&& from) : from_(std::move(from)) {}

			enum_maker(const enum_maker&) = delete;
			enum_maker& operator= (const enum_maker&) = delete;

			enum_maker(enum_maker&& that) noexcept
				: from_(std::move(that.from_))
			{
			}

			enum_maker& operator= (enum_maker&& that) noexcept
			{
				from_ = std::move(that.from_);
				return *this;
			}

			From operator[](const value& val)&&
			{
				add_value(val);
				return std::move(from_);
			}

			From operator[](const value_vector& values)&&
			{
				add_value(values);
				return std::move(from_);
			}

			From& operator[](const value& val)&
			{
				add_value(val);
				return from_;
			}

			From& operator[](const value_vector& values)&
			{
				add_value(values);
				return from_;
			}


			template<typename... Args,
				typename = std::enable_if_t<all_values_t<Args...>::value>>
				From operator[](Args&&... args)&&
			{
				// expands in left-to-right order
				(add_value(std::forward<Args>(args)), ...);
				return std::move(from_);
			}

			template<typename... Args,
				typename = std::enable_if_t<all_values_t<Args...>::value>>
				From & operator[](Args&&... args)&
			{
				(add_value(std::forward<Args>(args)), ...);
				return from_;
			}

		private:
			void add_value(const value& v)
			{
				from_.add_static_constant(v.name_, v.val_);
			}

			void add_value(const value_vector& vv)
			{
				for (auto const& v : vv)
					from_.add_static_constant(v.name_, v.val_);
			}

		private:
			From from_;
		};
	}
}
