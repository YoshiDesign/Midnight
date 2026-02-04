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

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iostream>
#include <variant>
#include <vector>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <string>
#include <span>
#include <memory>
#include <limits>
#include <cstdlib>
#include <chrono>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "Utils/aveng_utils.h"

#define DEBUG(what) std::cout << what << " " <<  __FILE__ << ":" << __LINE__ << std::endl