#pragma once

#include <cstdint>

namespace async_coro
{
	enum class execution_thread_type : std::uint8_t
	{
		undefined,
		main_thread,
		worker_thread
	};

    class scheduler {
	public:
		// Executes planned tasks for main thread
		void update();
    };
}
