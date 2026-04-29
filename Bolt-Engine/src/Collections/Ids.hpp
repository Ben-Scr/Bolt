#pragma once
#include <cstdint>
#include "Core/Export.hpp"
#include "Core/UUID.hpp"

namespace Bolt {
    using AssetGUID = UUID;
    using EntityID = std::uint64_t;

    template <class IdTag>
    struct BOLT_API StrongId {
        std::uint64_t value{ 0 };

        StrongId() = default;

        friend bool operator==(StrongId, StrongId) = default;
        friend bool operator<(StrongId a, StrongId b) { return a.value < b.value; }

        constexpr explicit StrongId(std::uint64_t id) noexcept : value(id) {}
    };

    struct EventIdTag {};
    using EventId = StrongId<EventIdTag>;

    struct SubscriptionIdTag {};
    using SubscriptionId = StrongId<SubscriptionIdTag>;
}
