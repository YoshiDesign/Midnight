#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string_view>
#include <string>
#include <functional>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>
#include <numeric>
#include <chrono>
#include <memory>
#include <cassert>
#include <limits>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <unordered_map>

#define DEBUG(what) std::cout << what << " " <<  __FILE__ << ":" << __LINE__ << std::endl