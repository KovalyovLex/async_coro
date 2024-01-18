#pragma once

#include <type_traits>

namespace async_coro::internal
{
	template<typename... T>
	struct always_false : std::false_type {};
}
