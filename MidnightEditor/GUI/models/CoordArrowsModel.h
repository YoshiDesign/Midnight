/* coordinate arrows */
#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "CoreVK/VkRenderData.h"

namespace aveng {
    class CoordArrowsModel {
    public:
        VkLineMesh getVertexData();

    private:
        void init();
        VkLineMesh mVertexData{};
    };
}