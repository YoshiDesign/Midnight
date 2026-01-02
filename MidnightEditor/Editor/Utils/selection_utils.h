#pragma once
#include "avpch.h"
#include "Core/Modeling/ModelAndInstanceData.h"
namespace aveng {

    /* These 3 structs are ImGUI specific */

    struct UiAnyHandle { // Unused at the moment, but serves to remind me how I could extend the UI API boundary
        PoolKind kind{};
        uint32_t index{};
        uint32_t generation{};
    };

    struct UiInstanceRow {
        AnyInstanceHandle handle{};
        ModelId modelId{};
        bool animated{};
        // Whatever you want to show in the list:
        // e.g. position, name, clip index, etc.
        glm::vec3 position{};
        glm::vec3 rotation{};
        float     scale{};
        // Optional: cached display strings (or compute on the fly)
        // std::string label;
    };

    struct UiModelRow {
        ModelId id{};
        AssetKey key{};
        bool animated{};
    };

    inline bool containsHandle(std::span<const AnyInstanceHandle> v, AnyInstanceHandle h) {
        return std::find(v.begin(), v.end(), h) != v.end();
    }

    /* Remove a handle from a vector of handles */
    inline void eraseHandle(std::vector<AnyInstanceHandle>& v, AnyInstanceHandle h) {
        v.erase(std::remove(v.begin(), v.end(), h), v.end());
    }

    /* Add handle to vec if it doesn't exist */
    inline void addUnique(std::vector<AnyInstanceHandle>& v, AnyInstanceHandle h) {
        if (!containsHandle(v, h)) v.push_back(h);
    }

}