#pragma once
#include "Event.hpp"

namespace Axiom {
    class CommandBuffer
    {
    public:
        template<typename F, typename... Args>
        void AddCommand(F&& func, Args&&... args)
        {
            m_Commands.emplace_back(
                [f = std::forward<F>(func), ... a = std::forward<Args>(args)]() mutable {
                    std::invoke(f, a...);
                }
            );
        }

        void Playback()
        {
            for (auto& cmd : m_Commands)
                cmd();

            m_Commands.clear();
        }

    private:
        std::vector<std::function<void()>> m_Commands;
    };
}