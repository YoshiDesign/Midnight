#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
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

#define DEBUG(what) std::cout << what << " " <<  __FILE__ << ":" << __LINE__ << std::endl