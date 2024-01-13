#pragma once

#include <utility>
#include <functional>
#include <type_traits>

namespace async_coro
{
	namespace internal
	{
		template<typename T>                                                        
		class function_impl_call
		{ 
			static_assert(sizeof(T) < 0, "move_only_function only accepts function types as template arguments.");
		};

		template<typename R, typename... TArgs>
		class function_impl_call<R (TArgs...)>
		{
		protected:
			using t_invoke_f = void(*)(void* const& fx, TArgs... args);

			inline static constexpr bool is_noexcept_invoke = false;

			template<typename Fx>
			using is_invocable = std::is_invocable_r<R, Fx, TArgs...>;

			function_impl_call() noexcept
			{ }
			function_impl_call(t_invoke_f f, void* fx) noexcept 
				: _invoke(f)
				, _fx(fx)
			{ }
			function_impl_call(function_impl_call&&) noexcept = default;

		public:
			R operator()(TArgs... args) const {
				if (_invoke == nullptr) [[unlikely]] {
					std::abort();
				}
				return _invoke(_fx, std::forward<TArgs>(args)...);
			}

		protected:
			t_invoke_f _invoke;
			void* _fx;
		};

		template<typename R, typename... TArgs>
		class function_impl_call<R (TArgs...) noexcept>
		{
		protected:
			using t_invoke_f = void(*)(void* const& fx, TArgs... args) noexcept;
			
			inline static constexpr bool is_noexcept_invoke = true;

			template<typename Fx>
			using is_invocable = std::is_nothrow_invocable_r<R, Fx, TArgs...>;

			function_impl_call() noexcept
			{ }
			function_impl_call(t_invoke_f f, void* fx) noexcept 
				: _invoke(f)
				, _fx(fx)
			{ }
			function_impl_call(function_impl_call&&) noexcept = default;

		public:
			R operator()(TArgs... args) const noexcept {
				if (_invoke == nullptr) [[unlikely]] {
					std::abort();
				}
				return _invoke(_fx, std::forward<TArgs>(args)...);
			}

		protected:
			t_invoke_f _invoke;
			void* _fx;
		};
	}
	

	template<typename FTy>
	class move_only_function : public internal::function_impl_call<FTy>
	{
		using super = internal::function_impl_call<FTy>;

		enum deinit_op {
			action_move,
			action_destroy,
		};

		using t_move_or_destroy_f = bool(*)(move_only_function& self, move_only_function* other, deinit_op op) noexcept;

		template<typename Fx>
		inline static constexpr bool is_small_f = sizeof(Fx) <= sizeof(void*) && alignof(Fx) <= alignof(void*);

		template<typename Fx>
		inline static constexpr bool is_noexecept_init = is_small_f<std::remove_cvref_t<Fx>> && std::is_nothrow_constructible_v<Fx, Fx&&>;

		struct no_init { };

		move_only_function(no_init) noexcept { }

	public:
		move_only_function() noexcept
			: super(nullptr, nullptr)
			, _move_or_destroy(nullptr)
		{ }

		move_only_function(std::nullptr_t) noexcept 
			: move_only_function()
		{ }

		move_only_function(const move_only_function&) = delete;

		move_only_function(move_only_function&& other) noexcept
			: super(std::move(other))
			, _move_or_destroy(other._move_or_destroy)
		{
			other._invoke = nullptr;
			if (_move_or_destroy && _move_or_destroy(*this, &other, action_move)) {
				other.clear();
			} else {
				this->_fx = other._fx;
			}
		}

		template<typename Fx, typename = std::enable_if_t<super::is_invocable<Fx>::value>>
		move_only_function(Fx&& func) noexcept(is_noexecept_init<Fx>)
			: move_only_function(no_init { })
		{
			constexpr bool is_small_function = is_small_f<std::remove_cvref_t<Fx>>;

			this->_invoke = static_cast<super::t_invoke_f>([](void* const& fx, auto&&... args) noexcept(super::is_noexcept_invoke) {
				if constexpr (is_small_function) {
					auto& fx_t = reinterpret_cast<const Fx&>(fx);
					std::invoke(fx_t, std::forward<decltype(args)>(args)...);
				} else {
					auto* fx_t = static_cast<Fx*>(fx);
					std::invoke(*fx_t, std::forward<decltype(args)>(args)...);
				}
			});

			if constexpr (is_small_function) {
				// small object
				if constexpr (!std::is_trivially_destructible_v<Fx> || !std::is_trivially_move_constructible_v<Fx>) {
					this->_move_or_destroy = +[](move_only_function& self, move_only_function* other, auto op) {
						if (op == action_destroy) {
							if constexpr (!std::is_trivially_destructible_v<Fx>) {
								static_assert(std::is_nothrow_destructible_v<Fx>, "lambda should have noexcept destructor");
								
								auto* fx = reinterpret_cast<Fx*>(&self._fx);
								std::destroy_at(fx);
								self._fx = nullptr;
							}
						} else {
							// action_move
							if constexpr (!std::is_trivially_move_constructible_v<Fx>) {
								static_assert(std::is_nothrow_move_constructible_v<Fx>, "lambda should have noexcept move contructor");

								auto* to = reinterpret_cast<Fx*>(&self._fx);
								auto& from = reinterpret_cast<Fx&>(other->_fx);
								new (to) Fx(std::move(from));
							}
						}
						return true;
					};
				} else {
					this->_move_or_destroy = nullptr;
				}

				new (&this->_fx) Fx(std::forward<Fx>(func));
			} else {
				// large function
				this->_move_or_destroy = +[](move_only_function& self, move_only_function*, auto op) noexcept {
					if (op == action_destroy){
						auto* fx = static_cast<Fx*>(self._fx);
						delete fx;
						self._fx = nullptr;
						return true;
					}
					return false;
				};
				this->_fx = new Fx(std::forward<Fx>(func));
			}
		}

		~move_only_function() noexcept {
			if (this->_move_or_destroy) {
				this->_move_or_destroy(*this, nullptr, action_destroy);
			}
		}

		move_only_function& operator=(const move_only_function&) const = delete;
		move_only_function& operator=(move_only_function&& other) noexcept {
			if (this == &other) {
				return * this;
			}

			clear();

			if (!other._move_or_destroy || !other._move_or_destroy(*this, &other, action_move)) {
				this->_fx = std::exchange(other._fx, nullptr);
			}

			this->_invoke = std::exchange(other._invoke, nullptr);
			_move_or_destroy = std::exchange(other._move_or_destroy, nullptr);
			
			return *this;
		}
		move_only_function& operator=(std::nullptr_t) noexcept {
			clear();
			return *this;
		}

		explicit operator bool() const noexcept {
			return this->_invoke != nullptr;
		}

		using super::operator();

	private:
		void clear() noexcept {
			if (_move_or_destroy) {
				_move_or_destroy(*this, nullptr, action_destroy);
			} else {
				this->_fx = nullptr;
			}
			_move_or_destroy = nullptr;
			this->_invoke = nullptr;
		}

	private:
		t_move_or_destroy_f _move_or_destroy;
	};
}
