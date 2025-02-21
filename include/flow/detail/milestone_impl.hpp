#pragma once

#include <container/Vector.hpp>

#include <type_traits>

namespace flow {
    class milestone_base;
}

namespace flow::detail {
    template<typename NameType>
    void log_flow_milestone() {
        TRACE("flow.milestone({})", NameType{});
    }
}





