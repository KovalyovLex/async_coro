#pragma once

#include <coroutine>

namespace async_coro
{
    template<typename T>
	class [[nodiscard]] generator final
	{
	public:
		class promise_type;

		using handle = std::coroutine_handle<promise_type>;

		// This structure for use in generator<void>. Dont use it explicitly, its enought to write co_yield {};
		struct void_result
		{};

		class iterator
		{
			friend class generator;

		public:
			using iterator_category = std::input_iterator_tag;
			using difference_type = std::ptrdiff_t;
			using value_type = T;
			using pointer = T*;
			using reference = T&;

			iterator() = default;

			bool operator==(const iterator& other) const
			{
				return _handle == other._handle;
			}

			bool operator!=(const iterator& other) const
			{
				return !(*this == other);
			}

			iterator& operator++()
			{
				if (_handle && !_handle.done()) {
					advance();
				}
				return *this;
			}

			iterator operator++(int) = delete;

			T& operator*() const
			{
				return *get();
			}

			T* operator->() const
			{
				return get();
			}

		private:
			explicit iterator(handle handle)
				: _handle(std::move(handle))
			{
				if (!_handle || _handle.done()) {
					_handle = {};
					return;
				}

				if (!_handle.promise().is_started()) {
					advance();
				}
			}

			void advance()
			{
				if (_handle.promise().pull()) {
					_handle = {};
				}
			}

			T* get() const
			{
				return _handle && !_handle.done() ? _handle.promise().value_ptr() : nullptr;
			}

			handle _handle;
		};

		explicit generator() noexcept
			: _handle()
		{}

		explicit generator(handle promise) noexcept
			: _handle(std::move(promise))
		{}

		generator(generator&& other) noexcept
			: _handle(std::move(other._handle))
		{
			other._handle = {};
		}

		generator(const generator& other) = delete;

		generator& operator=(const generator& other) = delete;

		~generator()
		{
			reset();
		}

		generator& operator=(generator&& other) noexcept
		{
			if (this != &other) {
				if (_handle) {
					_handle.destroy();
				}

				_handle = std::move(other._handle);
				other._handle = {};
			}

			return *this;
		}

		void swap(generator& other) noexcept
		{
			std::swap(_handle, other._handle);
		}

		// Is coroutine executed till the end or co_return
		bool is_finished() const noexcept
		{
			if (_handle) {
				return _handle.done();
			}

			return true;
		}

        // Do part of job till next co_yield in routine
		void move_next() noexcept
		{
			if (_handle && !_handle.done()) {
				_handle.promise().pull();
			}
		}

        // Destroys internal coroutine handle and all its resources 
		void reset()
		{
			if (_handle) {
				_handle.promise().destroy(_handle);
			}
		}

		T* get_value() const noexcept
		{
			return (_handle && !_handle.done()) ? _handle.promise().value_ptr() : nullptr;
		}

		template<class U = T>
		std::enable_if_t<!std::is_void_v<U>, const U&> get_value_or(const U& defaultValue) const noexcept
		{
			static_assert(std::is_same_v<T, U>, "Can use only T");
			auto* value = value();

			return value ? *value : defaultValue;
		}

		explicit operator bool() const noexcept
		{
			return !is_finished();
		}

		// Starts coroutine execution immediately
		iterator begin()
		{
			return iterator { _handle };
		}

		// Empty iterator
		iterator end()
		{
			return {};
		}

		// Starts execution of coroutine immediatelly
		bool empty()
		{
			return begin() == end();
		}

	private:
		template<class U, class = void>
		class value_holder
		{
		public:
			std::suspend_always yield_value(U& value) noexcept
			{
				_valuePtr = std::addressof(value);
				return {};
			}

			std::suspend_always yield_value(U&& value) noexcept
			{
				_valuePtr = std::addressof(value);
				return {};
			}

		protected:
			U* get_value_ptr() const noexcept
			{
				return _valuePtr;
			}

		protected:
			U* _valuePtr = nullptr;
		};

		template<class U>
		class value_holder<U, std::enable_if_t<std::is_void_v<U>>>
		{
		public:
			std::suspend_always yield_value(void_result&&) noexcept
			{
				return {};
			}

		protected:
			void* get_value_ptr() const noexcept
			{
				return nullptr;
			}
		};

		class promise_type final : public value_holder<T>
		{
		public:
			struct optional_suspend
			{
				optional_suspend(bool value) noexcept
					: _ready(!value)
				{}

				bool await_ready() const noexcept
				{
					return this->_ready;
				}

				bool await_suspend(const handle&) const noexcept
				{
					return true;
				}

				void await_resume() const noexcept
				{
				}

			private:
				bool _ready;
			};

			promise_type() noexcept
			{}

			promise_type(const promise_type&) = delete;
			promise_type(promise_type&&) = delete;

			promise_type& operator=(const promise_type&) = delete;
			promise_type& operator=(promise_type&&) = delete;

			auto get_return_object()
			{
				return generator<T> { handle::from_promise(*this) };
			}

			// Root coroutine doesnt auto start.
            // This can be used for example to transfer coroutine to another thread before execution.
			std::suspend_always initial_suspend() const noexcept
			{
				return {};
			}

			std::suspend_always final_suspend() const noexcept
			{
				return {};
			}

			void unhandled_exception() const
			{
				//TODO: support exceptions optionally
			}

			void return_void() const noexcept
			{}

			// You can co_yeld value T or another generator.
			using value_holder<T>::yield_value;

			auto yield_value(generator&& generator)
			{
				if (auto& handle = generator._handle) {
					auto& promise = handle.promise();
					if (promise.is_started() && handle.done()) {
						return optional_suspend { false };
					}
					auto top = this;
					while (top->_prev) {
						top = top->_prev;
					}
					assert((!top->_mostEmbedded && top == this) || top->_mostEmbedded == this);

					auto* newMostEmbedded = promise._mostEmbedded ? promise._mostEmbedded : &promise;
					const auto prevMostEmbedded = std::exchange(top->_mostEmbedded, newMostEmbedded);

					promise._prev = this;
					promise._isStarted = true;
					promise._mostEmbedded = nullptr;

					handle.resume();

					if (handle.done()) {
						top->_mostEmbedded = prevMostEmbedded;
					}
					else {
						return optional_suspend { true };
					}
				}

				return optional_suspend { false };
			}

			// Don't allow any use of 'co_await' inside the generator.
			template<typename U>
			std::suspend_never await_transform(U&& value) = delete;

			T* value_ptr() const noexcept
			{
				if (_mostEmbedded) {
					return _mostEmbedded->get_value_ptr();
				}
				return this->get_value_ptr();
			}

			bool is_started() const noexcept
			{
				return _isStarted;
			}

			bool pull()
			{
				bool is_finished = false;

				_isStarted = true;
				if (_mostEmbedded) {
					auto prevValue = _mostEmbedded->_prev;
					while (_mostEmbedded && _mostEmbedded->execute()) {
						_mostEmbedded = prevValue;
						if (prevValue) {
							prevValue = prevValue->_prev;
						}
						if (_mostEmbedded == this) {
							assert(prevValue == nullptr);
							_mostEmbedded = nullptr;
							break;
						}
					}

					if (!_mostEmbedded) {
						is_finished = execute();
					}
				}
				else {
					is_finished = execute();
				}

				return is_finished;
			}

			void destroy(handle& handle)
			{
				handle.destroy();
				handle = {};
			}

		private:
			bool execute()
			{
				assert(_mostEmbedded == nullptr);

				auto&& myhandle = handle::from_promise(*this);

				if (!myhandle.done()) {
					myhandle.resume();
					const bool is_finished = myhandle.done();

					return is_finished;
				}
				return true;
			}

		private:
			promise_type* _prev = nullptr;
			promise_type* _mostEmbedded = nullptr;
			bool _isStarted = false;
		};

	private:
		handle _handle;
	};
}
