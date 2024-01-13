#pragma once

#include <concepts>
#include <utility>
#include <functional>

namespace async_coro::internal
{
	struct function_v_table_base
	{
		using t_destroy_f = void(*)(void* fx) noexcept;

		t_destroy_f destroy_f;
	};


	template<typename Fx, typename R, typename... TArgs>
	concept is_invocable = std::invocable<Fx, TArgs...> && std::convertible_to<std::invoke_result_t<Fx, TArgs...>, R>;

	template<typename Fx, typename R, typename... TArgs>
	concept is_invocable_noexcept = is_invocable<Fx, R, TArgs...> &&
	requires(Fx& fx, TArgs&&... args) {
		{ std::invoke(fx, static_cast<TArgs&&>(args)...) } noexcept;
	};

	template<typename T>                                                        
    class function_impl_call { 
		static_assert(sizeof(T) < 0, "move_only_function only accepts function types as template arguments.");
	};

	class function_impl_base
	{
	protected:
		using t_destroy_f = void(*)(void*& fx) noexcept;
		using t_move_f = void(*)(void*& from, void*& to) noexcept;

		template<typename Fx>
		inline static constexpr bool is_noexecept_init = sizeof(Fx) <= sizeof(void*) && std::is_nothrow_constructible_v<Fx, Fx&&>;

		function_impl_base() noexcept = default;
		function_impl_base(function_impl_base&& other) noexcept
			: _destroy_f(other._destroy_f)
			, _move_f(other._move_f)
		{
			other._destroy_f = nullptr;
			other._move_f = nullptr;
		}

		template<typename Fx, typename TInvoke, typename ...TArgs>
		void init(Fx&& func, TInvoke& invoke_f, void*& f_ptr) noexcept(is_noexecept_init<Fx>) {
			constexpr bool is_small_function = sizeof(Fx) <= sizeof(void*);

			invoke_f = +[](void* const& fx, TArgs... args) noexcept(std::is_nothrow_invocable_v<TInvoke, void* const&, TArgs...>) {
				if constexpr (is_small_function) {
					auto& fx_t = reinterpret_cast<const Fx&>(fx);
					std::invoke(fx_t, std::forward<TArgs>(args)...);
				} else {
					auto* fx_t = static_cast<Fx*>(fx);
					std::invoke(*fx_t, std::forward<TArgs>(args)...);
				}
			};

			if constexpr (is_small_function) {
				// small object
				if constexpr (!std::is_trivially_destructible_v<Fx>) {
					_destroy_f = +[](void*& fx) noexcept {
						auto& fx_t = reinterpret_cast<Fx&>(fx);
						std::destroy_at(&fx_t);
						fx = nullptr;
					};
				}
				if constexpr (!std::is_trivially_move_constructible_v<Fx>) {
					_move_f = +[](void*& from, void*& to) noexcept {
						auto& fx1 = reinterpret_cast<Fx&>(from);
						new (&from)Fx(std::move(fx1));
					};
				}

				new (&f_ptr)Fx(std::forward<Fx>(func));
			} else {
				// large function
				_destroy_f = +[](void*& fx) noexcept {
					auto* fx_t = static_cast<Fx*>(fx);
					delete fx_t;
					fx = nullptr;
				};
				f_ptr = new Fx(std::forward<Fx>(func));
			}
		}

	protected:
		t_move_f _move_f = nullptr;
		t_destroy_f _destroy_f = nullptr;
	};

	template<typename R, typename... TArgs>
    class function_impl_call<R (TArgs...)> : public function_impl_base
	{
	protected:
		using t_invoke_f = void(*)(void* const& fx, TArgs... args);

		function_impl_call() noexcept = default;
		function_impl_call(function_impl_call&& other) noexcept
			: function_impl_base(std::move(other))
			, _invoke_f(other._invoke_f)
		{ 
			other._invoke_f = nullptr;
		}

		template<class Fx> requires(is_invocable<Fx, R, TArgs...>)
		function_impl_call(Fx&& func) noexcept(function_impl_base::is_noexecept_init<Fx>) {
			init<Fx, t_invoke_f, TArgs...>(std::forward<Fx>(func), _invoke_f, _fx);
		}

	public:
		R operator()(TArgs... args) const {
			if (_invoke_f == nullptr) {
				std::abort();
			}
			return _invoke_f(_fx, std::forward<TArgs>(args)...);
		}

	protected:
		t_invoke_f _invoke_f = nullptr;
		void* _fx = nullptr;
	};

	template<typename R, typename... TArgs>
    class function_impl_call<R (TArgs...) noexcept> : public function_impl_base
	{
	protected:
		using t_invoke_f = void(*)(void* const& fx, TArgs... args) noexcept;

		function_impl_call() noexcept = default;
		function_impl_call(function_impl_call&& other) noexcept
			: function_impl_base(std::move(other))
			, _invoke_f(other._invoke_f)
		{ 
			other._invoke_f = nullptr;
		}

		template<class Fx> requires(is_invocable_noexcept<Fx, R, TArgs...>)
		function_impl_call(Fx&& func) noexcept(function_impl_base::is_noexecept_init<Fx>) {
			init<Fx, t_invoke_f, TArgs...>(std::forward<Fx>(func), _invoke_f, _fx);
		}

	public:
		R operator()(TArgs... args) const noexcept {
			if (_invoke_f == nullptr) {
				std::abort();
			}
			return _invoke_f(_fx, std::forward<TArgs>(args)...);
		}

	protected:
		t_invoke_f _invoke_f = nullptr;
		void* _fx = nullptr;
	};

	template<typename FTy>
	class move_only_function : public function_impl_call<FTy>
	{
		using super = function_impl_call<FTy>;

	public:
		move_only_function() noexcept = default;

		move_only_function(std::nullptr_t) noexcept { }

		move_only_function(const move_only_function&) = delete;

		move_only_function(move_only_function&& other) noexcept
			: super(std::move(other))
		{
			if (this->_move_f) {
				this->_move_f(other._fx, this->_fx);
				other.crear();
			} else {
				this->_fx = other._fx;
			}
		}

		template<typename Fx>
		move_only_function(Fx&& func) // noexcept(super::is_noexecept_init<Fx>)
			: super(std::forward<Fx>(func))
		{ }

		~move_only_function() noexcept {
			if (this->_destroy_f) {
				this->_destroy_f(this->_fx);
			}
		}

		move_only_function& operator=(const move_only_function&) const = delete;
		move_only_function& operator=(move_only_function&& other) noexcept {
			if (this == &other) {
				return * this;
			}

			crear();

			if (other._move_f) {
				other._move_f(other._fx, this->_fx);
			} else {
				std::swap(this->_fx, other._fx);
			}

			this->_destroy_f = std::exchange(other._destroy_f, nullptr);
			this->_invoke_f = std::exchange(other._invoke_f, nullptr);
			this->_move_f = std::exchange(other._move_f, nullptr);
			return *this;
		}
		move_only_function& operator=(std::nullptr_t) noexcept {
			crear();
			return *this;
		}

		explicit operator bool() const noexcept {
			return this->_invoke_f != nullptr;
		}

		using super::operator();

	private:
		void crear() noexcept {
			if (this->_destroy_f) {
				this->_destroy_f(this->_fx);
			} else {
				this->_fx = nullptr;
			}
			this->_invoke_f = nullptr;
			this->_destroy_f = nullptr;
			this->_move_f = nullptr;
		}
	};
}
