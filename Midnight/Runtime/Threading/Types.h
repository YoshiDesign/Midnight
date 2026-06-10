#pragma once

#include <mutex>
#include <random>

#include "Runtime/Threading/CLDeque.h"

namespace mtools
{
    /**
    * For now I'm just using a different enum for any possible multithreaded task.
    * Should this become too unwieldy, we can punt to unions and a JobDomain enum.
    * (Or ditch the general-purpose scheduler in favor of bespoke tooling per feature)
    */

} // namespace aveng