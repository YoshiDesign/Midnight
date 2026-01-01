#pragma once
#include "avpch.h"
#include "Core/Modeling/InstanceSettings.h"
#include "Core/Modeling/ModelRegistry.h"

namespace aveng {
    /* Important: this is implemented in our template-trait-bundle
       to simplify instance creation. See ModelAndInstanceData */

    struct InstanceCommon {
        ModelId   modelId{ 0 };
        glm::mat4 modelRoot{ 1.f };        // meta.root, set at creation
        InstanceTransform xf{};

        // Cached final matrix; recompute only when dirty
        mutable glm::mat4 cachedModelMatrix{ 1.f };
        mutable bool dirty = true;

        void init(ModelId id, const glm::mat4& root, const InstanceTransform& t) {
            modelId = id;
            modelRoot = root;
            xf = t;
            dirty = true;
        }

        // Mark dirty when transform changes
        void setPos(glm::vec3 p) { xf.pos = p; dirty = true; }
        void setRot(glm::vec3 r) { xf.rotEuler = r; dirty = true; }
        void setScale(float s) { xf.scale = s; dirty = true; }

        InstanceTransform& getTransform() { return xf; }

        const glm::mat4& modelMatrix() const {
            if (!dirty) return cachedModelMatrix;

            // your current math, but only when needed:
            glm::mat4 T = glm::translate(glm::mat4(1.f), xf.pos);

            // keep your convention; later: store quat and avoid euler->quat per call
            glm::quat q = glm::quat(glm::radians(xf.rotEuler));
            glm::mat4 R = glm::mat4_cast(q);

            glm::mat4 S = glm::scale(glm::mat4(1.f), glm::vec3(xf.scale));

            // Convention choice: instanceWorld * root  OR  root * instanceWorld
            // Pick one and lock it everywhere:
            cachedModelMatrix = (T * R * S) * modelRoot;

            dirty = false;
            return cachedModelMatrix;
        }
    };

}