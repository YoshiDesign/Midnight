#pragma once

#include "EngineDevice.h"

// std
#include <memory>
#include <unordered_map>
#include <vector>

#define TEXTURE_ARRAY_SIZE  2

/* ******************** References ******************** *\
* 
* A descriptor set layout object is defined by an array of zero or more descriptor bindings
* Each individual descriptor binding is specified by a descriptor type, a count (array size) 
* of the number of descriptors in the binding, a set of shader stages that can access the binding, 
* and (if using immutable samplers) an array of sampler descriptors.
* 
* // Provided by VK_VERSION_1_0
* typedef struct VkDescriptorSetLayoutBinding {
*     uint32_t              binding;              // The binding number of this entry and corresponds to a resource of the same binding number in the shader stages.
*     VkDescriptorType      descriptorType;       // Which type of resource descriptors are used for this binding.
*     uint32_t              descriptorCount;      // the number of descriptors contained in the binding, accessed in a shader as an array, except if INLINE
*     VkShaderStageFlags    stageFlags;           // A bitmask specifying which pipeline shader stages can access a resource for this binding
*     const VkSampler*      pImmutableSamplers;
* } VkDescriptorSetLayoutBinding;
* 
*     *If descriptorType is VK_DESCRIPTOR_TYPE_SAMPLER or VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
*     and descriptorCount is not 0 and pImmutableSamplers is not NULL, pImmutableSamplers must be a 
*     valid pointer to an array of descriptorCount valid VkSampler handles
* 
* From VkGuide, on the arrangement of descriptor set layouts - 
* ...you will see that some devices will only allow up to 4 descriptor sets to be bound to a given pipeline...
* The descriptor set number 0 will be used for engine-global resources, and bound once per frame.
* The descriptor set number 1 will be used for per-pass resources, and bound once per pass.
* The descriptor set number 2 will be used for material resources, and the number 3 will be used for per-object resources.
* This way, the inner render loops will only be binding descriptor sets 2 and 3, and performance will be high.
* 
*/

namespace aveng {

    class AvengDescriptorSetLayout {
    public:

        class Builder {
        public:
            Builder(EngineDevice& device);
            Builder& addBinding(uint32_t binding, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags, uint32_t count = 1);
            std::unique_ptr<AvengDescriptorSetLayout> build() const;
            

        private:
            EngineDevice& engineDevice;
            std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> assert_layout_bindings{};
            std::vector<VkDescriptorSetLayoutBinding> layout_bindings{};
        };

        AvengDescriptorSetLayout(EngineDevice& engineDevice, std::vector<VkDescriptorSetLayoutBinding> bindings, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> binding_assertions);
        ~AvengDescriptorSetLayout();
        AvengDescriptorSetLayout(const AvengDescriptorSetLayout&) = delete;
        AvengDescriptorSetLayout& operator=(const AvengDescriptorSetLayout&) = delete;

        VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        size_t getDescriptorSetLayoutCount() { return layout_bindings.size(); }

    private:
        EngineDevice& engineDevice;
        VkDescriptorSetLayout descriptorSetLayout;
        std::vector<VkDescriptorSetLayoutBinding> layout_bindings;
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> binding_assertions; // Come from Builder::assert_layout_bindings

        // This idea is implemented in AvengImageSystem because it requires image views
        //VkDescriptorImageInfo descriptorImageInfos[TEXTURE_ARRAY_SIZE]{ {},{} };

        friend class AvengDescriptorSetWriter;
    };

    class AvengDescriptorPool {
    public:
        class Builder {
        public:
            Builder(EngineDevice& device);

            Builder& addPoolSize(VkDescriptorType descriptorType, uint32_t count);
            Builder& setPoolFlags(VkDescriptorPoolCreateFlags flags);
            Builder& setMaxSets(uint32_t count);
            std::unique_ptr<AvengDescriptorPool> build() const;

        private:
            EngineDevice& engineDevice;
            std::vector<VkDescriptorPoolSize> poolSizes{};
            uint32_t maxSets = 1000;
            VkDescriptorPoolCreateFlags poolFlags = 0;
        };

        AvengDescriptorPool(
            EngineDevice& engineDevice, 
            uint32_t maxSets, 
            VkDescriptorPoolCreateFlags poolFlags, 
            const std::vector<VkDescriptorPoolSize>& poolSizes
        );

        ~AvengDescriptorPool();
        AvengDescriptorPool(const AvengDescriptorPool&) = delete;
        AvengDescriptorPool& operator=(const AvengDescriptorPool&) = delete;

        bool allocateDescriptors(
            const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet& descriptor) const;

        void freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const;

        void resetPool();

    private:
        EngineDevice& engineDevice;
        VkDescriptorPool descriptorPool;

        friend class AvengDescriptorSetWriter;
    };

    class AvengDescriptorSetWriter {
    public:
        AvengDescriptorSetWriter(AvengDescriptorSetLayout& setLayout, AvengDescriptorPool& pool);

        AvengDescriptorSetWriter& writeBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo);
        AvengDescriptorSetWriter& writeImage(uint32_t binding, VkDescriptorImageInfo* imageInfo, int nImages);

        bool build(VkDescriptorSet& set);
        void overwrite(VkDescriptorSet& set);

    private:
        AvengDescriptorSetLayout& setLayout;
        AvengDescriptorPool& pool;
        std::vector<VkWriteDescriptorSet> writes;
    };

}  // namespace Aveng