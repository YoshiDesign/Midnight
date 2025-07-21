#pragma once

#include <unordered_map>
#include <queue>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>

// Forward declarations for future implementation

namespace aveng {

    // Future texture scalability approach
    class TextureStreamingManager {
    public:
        struct TextureHandle {
            uint32_t id;
            uint32_t generation;  // For handle validation
            bool isValid() const;
        };

        struct TextureDescriptor {
            std::string path;
            uint32_t width, height;
            uint32_t mipLevels;
            uint32_t format;  // Texture format (implementation-specific)
            bool isStreaming = false;
        };

    private:
        // Current approach limitations:
        // - Fixed texture array size (MAX_TEXTURES = currentTextureCount)
        // - All textures loaded at startup
        // - No streaming or dynamic loading

        // Future scalable approach:
        static constexpr uint32_t MAX_RESIDENT_TEXTURES = 2048;
        static constexpr uint32_t TEXTURE_MEMORY_BUDGET_MB = 1024;
        
        std::unordered_map<uint32_t, TextureDescriptor> textureDatabase;
        std::unordered_map<uint32_t, void*> residentTextures;  // Generic texture pointers
        std::queue<uint32_t> streamingQueue;
        std::thread streamingThread;
        std::atomic<bool> shouldExit{false};
        std::mutex streamingMutex;

        // Bindless rendering support (future implementation)
        void* bindlessTextureSet;  // Generic descriptor set handle
        std::vector<uint32_t> freeDescriptorIndices;
        uint32_t nextDescriptorIndex = 0;

    public:
        // Modern texture management API
        TextureHandle loadTexture(const std::string& path);
        void requestTexture(TextureHandle handle, int priority = 0);
        void evictTexture(TextureHandle handle);
        uint32_t getDescriptorIndex(TextureHandle handle);
        bool isTextureResident(TextureHandle handle);
        
        // Memory management
        size_t getMemoryUsage() const;
        void setMemoryBudget(size_t budgetMB);
        void evictLeastRecentlyUsed();
        
        // Streaming
        void enableStreaming(bool enabled);
        void streamingWorker();  // Background thread
    };

    // Usage example:
    /*
    // In scene loading:
    auto textureHandle = textureManager.loadTexture("textures/wall_diffuse.png");
    
    // In rendering:
    uint32_t descriptorIndex = textureManager.getDescriptorIndex(textureHandle);
    objectUniform.textureIndex = descriptorIndex;
    
    // In shader (bindless):
    layout(set = 0, binding = 0) uniform sampler2D textures[];  // Unbounded
    vec4 color = texture(textures[nonuniformEXT(textureIndex)], uv);
    */

} 