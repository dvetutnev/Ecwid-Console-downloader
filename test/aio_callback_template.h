#pragma once

#include <functional>

template< typename Event, typename Resource >
using Callback = std::function< void(Event&, Resource&) >;
