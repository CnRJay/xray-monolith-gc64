namespace luabind::detail
{
	template<typename Container>
	struct iterator_pair_state
	{
		using Iter = typename Container::const_iterator;

		iterator_pair_state(std::shared_ptr<Container> c)
			: container(std::move(c)), start(container->begin()), end(container->end())
		{
		}

		static int step(lua_State* L)
		{
			auto* state = static_cast<iterator_pair_state*>(lua_touserdata(L, lua_upvalueindex(1)));

			if (!state || state->start == state->end)
			{
				lua_pushnil(L);
				return 1;
			}

			convert_to_lua(L, state->start->first);
			convert_to_lua(L, state->start->second);
			++state->start;
			return 2;
		}

		std::shared_ptr<Container> container;
		Iter start, end;
	};

	struct iterator_pair_converter
	{
		template <typename T>
		void apply(lua_State* L, T& c)
		{
			auto container_ptr = std::make_shared<T>(c);
			void* udata = lua_newuserdata(L, sizeof(iterator_pair_state<T>));
			new (udata) iterator_pair_state<T>(container_ptr);
			lua_pushcclosure(L, iterator_pair_state<T>::step, 1);
		}

		template <typename T>
		void apply(lua_State* L, const T& c)
		{
			auto container_ptr = std::make_shared<T>(c);
			void* udata = lua_newuserdata(L, sizeof(iterator_pair_state<T>));
			new (udata) iterator_pair_state<T>(container_ptr);
			lua_pushcclosure(L, iterator_pair_state<T>::step, 1);
		}
	};

	struct iterator_pair_policy :
		conversion_policy<0>
	{
		static void precall(lua_State*, const index_map&) {}
		static void postcall(lua_State*, const index_map&) {}

		template<typename T, Direction>
		struct generate_converter
		{
			using type = iterator_pair_converter;
		};
	};
}

namespace luabind
{
	namespace
	{
		detail::policy_cons<detail::iterator_pair_policy> return_stl_pair_iterator;
	}
}