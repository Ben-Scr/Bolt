#pragma once

#include "Core/Log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#include <stop_token>
#define BOLT_HAS_STD_JTHREAD 1
#else
#define BOLT_HAS_STD_JTHREAD 0
#endif

namespace Bolt {

	class OwnedTask {
		struct State {
			std::atomic_bool StopRequested = false;
			mutable std::mutex FinishedMutex;
			std::condition_variable FinishedCondition;
			bool Finished = false;
			std::exception_ptr Exception;
		};

	public:
#if BOLT_HAS_STD_JTHREAD
		using StopToken = std::stop_token;
#else
		class StopToken {
		public:
			bool stop_requested() const
			{
				return m_State && m_State->StopRequested.load(std::memory_order_acquire);
			}

		private:
			friend class OwnedTask;

			explicit StopToken(std::shared_ptr<State> state)
				: m_State(std::move(state))
			{
			}

			std::shared_ptr<State> m_State;
		};
#endif

		OwnedTask() = default;

		template<typename F>
		explicit OwnedTask(F&& task)
		{
			Start(std::forward<F>(task));
		}

		OwnedTask(const OwnedTask&) = delete;
		OwnedTask& operator=(const OwnedTask&) = delete;

		OwnedTask(OwnedTask&&) noexcept = default;

		OwnedTask& operator=(OwnedTask&& other) noexcept
		{
			if (this != &other)
			{
				StopAndJoin();
				MoveFrom(std::move(other));
			}

			return *this;
		}

		~OwnedTask()
		{
			StopAndJoin();
		}

		template<typename F>
		void Start(F&& task)
		{
			StopAndJoin();

			m_State = std::make_shared<State>();

#if BOLT_HAS_STD_JTHREAD
			m_Task = std::jthread([state = m_State, fn = std::forward<F>(task)](std::stop_token stopToken) mutable {
				RunTask(state, fn, stopToken);
			});
#else
			m_Task = std::thread([state = m_State, fn = std::forward<F>(task)]() mutable {
				RunTask(state, fn, StopToken(state));
			});
#endif
		}

		void RequestStop()
		{
			if (!m_State)
				return;

			m_State->StopRequested.store(true, std::memory_order_release);

#if BOLT_HAS_STD_JTHREAD
			if (m_Task.joinable())
				m_Task.request_stop();
#endif
		}

		void Join()
		{
			if (m_Task.joinable())
				m_Task.join();
		}

		void StopAndJoin()
		{
			RequestStop();
			Join();
			m_State.reset();
		}

		template<typename Rep, typename Period>
		bool StopAndJoinFor(const std::chrono::duration<Rep, Period>& timeout)
		{
			RequestStop();

			if (!Joinable())
				return true;

			if (!WaitFor(timeout))
				return false;

			Join();
			m_State.reset();
			return true;
		}

		template<typename Rep, typename Period>
		bool WaitFor(const std::chrono::duration<Rep, Period>& timeout) const
		{
			if (!m_State)
				return true;

			std::unique_lock<std::mutex> lock(m_State->FinishedMutex);
			return m_State->FinishedCondition.wait_for(lock, timeout, [&state = m_State] {
				return state->Finished;
			});
		}

		bool Joinable() const
		{
			return m_Task.joinable();
		}

		bool HasException() const
		{
			return GetException() != nullptr;
		}

		std::exception_ptr GetException() const
		{
			if (!m_State)
				return nullptr;

			std::scoped_lock lock(m_State->FinishedMutex);
			return m_State->Exception;
		}

	private:
		template<typename>
		inline static constexpr bool AlwaysFalse = false;

		template<typename F, typename TStopToken>
		static void RunTask(const std::shared_ptr<State>& state, F& task, TStopToken stopToken)
		{
			try
			{
				if constexpr (std::is_invocable_v<F&, TStopToken>)
				{
					task(stopToken);
				}
				else if constexpr (std::is_invocable_v<F&>)
				{
					task();
				}
				else
				{
					static_assert(AlwaysFalse<F>, "OwnedTask requires a callable with no arguments or a callable accepting OwnedTask::StopToken.");
				}
			}
			catch (...)
			{
				std::exception_ptr exception = std::current_exception();
				StoreException(state, exception);
				LogUnhandledException(exception);
			}

			MarkFinished(state);
		}

		static void StoreException(const std::shared_ptr<State>& state, std::exception_ptr exception)
		{
			std::scoped_lock lock(state->FinishedMutex);
			state->Exception = std::move(exception);
		}

		static void LogUnhandledException(const std::exception_ptr& exception) noexcept
		{
			try
			{
				if (exception)
					std::rethrow_exception(exception);
			}
			catch (const std::exception& e)
			{
				try
				{
					BT_CORE_ERROR_TAG("OwnedTask", "Background task failed: {}", e.what());
				}
				catch (...)
				{
				}
			}
			catch (...)
			{
				try
				{
					BT_CORE_ERROR_TAG("OwnedTask", "Background task failed with an unknown exception");
				}
				catch (...)
				{
				}
			}
		}

		static void MarkFinished(const std::shared_ptr<State>& state)
		{
			{
				std::scoped_lock lock(state->FinishedMutex);
				state->Finished = true;
			}

			state->FinishedCondition.notify_all();
		}

		void MoveFrom(OwnedTask&& other) noexcept
		{
			m_Task = std::move(other.m_Task);
			m_State = std::move(other.m_State);
		}

#if BOLT_HAS_STD_JTHREAD
		std::jthread m_Task;
#else
		std::thread m_Task;
#endif
		std::shared_ptr<State> m_State;
	};

} // namespace Bolt

#undef BOLT_HAS_STD_JTHREAD
