#pragma once

#include <utility>
#include <functional>
#include <type_traits>
#include <async_coro/internal/always_false.h>

namespace async_coro
{
	namespace internal
	{
		template<typename T>                                                        
		class function_impl_call
		{ 
			static_assert(always_false<T>::value, "move_only_function only accepts function types as template arguments.");
		};

		template<typename R, typename... TArgs>
		class function_impl_call<R (TArgs...)>
		{
		protected:
			using t_invoke_f = R(*)(void* const& fx, TArgs&&... args);

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
			R operator()(TArgs&&... args) const {
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
			using t_invoke_f = R(*)(void* const& fx, TArgs&&... args) noexcept;
			
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
			R operator()(TArgs&&... args) const noexcept {
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

		using t_move_or_destroy_f = void(*)(move_only_function& self, move_only_function* other, deinit_op op) noexcept;

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
			if (_move_or_destroy) {
				_move_or_destroy(*this, &other, action_move);
			}
			other.clear();
		}

		template<typename Fx, typename = std::enable_if_t<super::is_invocable<Fx>::value>>
		move_only_function(Fx&& func) noexcept(is_noexecept_init<Fx>)
			: move_only_function(no_init { })
		{
			init(std::forward<Fx>(func));
		}

		~move_only_function() noexcept {
			if (_move_or_destroy) {
				_move_or_destroy(*this, nullptr, action_destroy);
			}
		}

		move_only_function& operator=(const move_only_function&) const = delete;
		move_only_function& operator=(move_only_function&& other) noexcept {
			if (this == &other) {
				return * this;
			}

			clear();

			if (other._move_or_destroy) {
				other._move_or_destroy(*this, &other, action_move);
			} else {
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

		template<typename Fx, typename = std::enable_if_t<super::is_invocable<Fx>::value && !std::is_same_v<Fx, move_only_function>>>
		move_only_function& operator=(Fx&& func) noexcept(is_noexecept_init<Fx>)
		{
			clear();
			init(std::forward<Fx>(func));
			return *this;
		}

		explicit operator bool() const noexcept {
			return this->_invoke != nullptr;
		}

		using super::operator();

	private:
		template<typename Fx, typename = std::enable_if_t<super::is_invocable<Fx>::value>>
		void init(Fx&& func) noexcept(is_noexecept_init<Fx>)
		{
			static_assert(std::is_nothrow_destructible_v<Fx>, "lambda should have noexcept destructor");

			using TFunc = std::remove_cvref_t<Fx>;
			constexpr bool is_small_function = is_small_f<TFunc>;

			this->_invoke = make_invoke<TFunc>(static_cast<super::t_invoke_f>(nullptr));

			if constexpr (is_small_function) {
				// small object
				if constexpr (!std::is_trivially_destructible_v<TFunc> || !std::is_trivially_move_constructible_v<TFunc>) {
					this->_move_or_destroy = +[](move_only_function& self, move_only_function* other, deinit_op op) noexcept {
						if (op == action_destroy) {
							if constexpr (!std::is_trivially_destructible_v<TFunc>) {
								auto* fx = reinterpret_cast<TFunc*>(&self._fx);
								std::destroy_at(fx);
							}
							self._fx = nullptr;
						} else {
							// action_move
							if constexpr (!std::is_trivially_move_constructible_v<TFunc>) {
								static_assert(std::is_nothrow_move_constructible_v<TFunc>, "lambda should have noexcept move contructor");

								auto* to = reinterpret_cast<TFunc*>(&self._fx);
								auto& from = reinterpret_cast<TFunc&>(other->_fx);
								new (to) TFunc(std::move(from));
								other->_fx = nullptr;
							}
						}
					};
				} else {
					this->_move_or_destroy = nullptr;
				}

				new (&this->_fx) Fx(std::forward<Fx>(func));
			} else {
				// large function
				_move_or_destroy = +[](move_only_function& self, move_only_function* other, auto op) noexcept {
					if (op == action_destroy) {
						if (self._fx) {
							auto* fx = static_cast<TFunc*>(self._fx);
							delete fx;
							self._fx = nullptr;
						}
					} else {
						// action_move
						self._fx = std::exchange(other->_fx, nullptr);
					}
				};
				this->_fx = new TFunc(std::forward<Fx>(func));
			}
		}

		void clear() noexcept {
			if (_move_or_destroy) {
				_move_or_destroy(*this, nullptr, action_destroy);
			} else {
				this->_fx = nullptr;
			}
			_move_or_destroy = nullptr;
			this->_invoke = nullptr;
		}

		template<typename TFunc, typename R, typename... TArgs>
		static auto make_invoke(R(*)(void* const&, TArgs...)noexcept(super::is_noexcept_invoke)) noexcept {
			return static_cast<super::t_invoke_f>([](void* const& fx, TArgs&&... args) noexcept(super::is_noexcept_invoke) {
				if constexpr (is_small_f<TFunc>) {
					auto& fx_t = reinterpret_cast<const TFunc&>(fx);
					return std::invoke(fx_t, std::forward<TArgs>(args)...);
				} else {
					auto* fx_t = static_cast<TFunc*>(fx);
					return std::invoke(*fx_t, std::forward<TArgs>(args)...);
				}
			});
		}

	private:
		t_move_or_destroy_f _move_or_destroy;
	};
}
