# Midnight Engine Animation System

This is a complete skeletal animation system built with Assimp for the Midnight Engine. It supports both static meshes and animated models with a robust instance management system.

## 🏗️ **Architecture Overview**

### Core Components

- **`AssimpModel`** - Handles model loading, mesh processing, bone hierarchies, and animation clips
- **`AssimpInstance`** - Manages individual model instances with transformation and animation state
- **`AnimationManager`** - Central manager using callback patterns for model/instance lifecycle
- **`AssimpAnimClip`** - Stores animation keyframes and provides interpolation
- **`AssimpAnimChannel`** - Individual bone animation data with position/rotation/scale keyframes
- **`AssimpMesh`** - Mesh data with bone weights (up to 4 bones per vertex)
- **`AssimpNode`** - Hierarchical scene node structure
- **`AssimpBone`** - Bone definitions with offset matrices

### Key Features

✅ **Complete Assimp Integration** - Loads both static and animated models  
✅ **Coordinate System Conversion** - Handles Assimp Y-up to Midnight Z-forward conversion  
✅ **Skeletal Animation** - Full bone hierarchy with up to 4 bone influences per vertex  
✅ **Animation Interpolation** - Smooth keyframe interpolation (position, rotation, scale)  
✅ **Instance Management** - Efficient callback-based instance creation/deletion  
✅ **Debug Information** - Comprehensive RenderData tracking for performance analysis  
✅ **Memory Management** - Proper cleanup and resource management

## 📋 **Usage Example**

```cpp
#include "Animation/AnimationManager.h"

// Initialize the animation system
aveng::AnimationManager animManager;
aveng::RenderData renderData{};

// Load an animated model
if (animManager.loadModel("models/character.fbx", renderData)) {

    // Create instances at different positions
    auto instance1 = animManager.createInstance("models/character.fbx",
                                               glm::vec3(0, 0, 0));
    auto instance2 = animManager.createInstance("models/character.fbx",
                                               glm::vec3(5, 0, 0));

    // Control animations
    instance1->setAnimation("walk");
    instance2->setAnimation("idle");

    // Update animations each frame
    float deltaTime = 0.016f; // 60 FPS
    animManager.updateAnimations(deltaTime);

    // Get debug information
    animManager.updateRenderData(renderData);
    printf("Loaded models: %d, Active instances: %d, Total bones: %d\n",
           renderData.rdLoadedModels, renderData.rdActiveInstances, renderData.rdTotalBones);
}
```

## 🎯 **Key Data Structures**

### Animated Vertex Format

```cpp
struct AnimatedVertex {
    glm::vec3 position;      // Vertex position
    glm::vec3 color;         // Vertex color
    glm::vec3 normal;        // Surface normal
    glm::vec2 texCoord;      // Texture coordinates
    glm::ivec4 boneIds;      // Up to 4 bone influences
    glm::vec4 boneWeights;   // Corresponding bone weights (normalized)
};
```

### Animation Debug Data

```cpp
struct RenderData {
    // Animation system debug data
    int rdLoadedModels = 0;
    int rdAnimatedModels = 0;
    int rdTotalBones = 0;
    int rdTotalNodes = 0;
    int rdTotalAnimationClips = 0;
    int rdActiveInstances = 0;
    float rdAnimationUpdateTime = 0.0f;
    int rdCurrentPipelineMode = 0; // 0=static, 1=animated, etc.
};
```

## 🔧 **Coordinate System Handling**

The system automatically converts between Assimp's coordinate system (right-handed, Y-up) and Midnight Engine's coordinate system (right-handed, Z-forward, -Y-up):

```cpp
// Assimp: +Y = up, +Z = forward
// Midnight: -Y = up, +Z = into screen

// Conversion is handled automatically in Tools::convertAiToGLM()
glm::vec3 position = Tools::convertAiToGLM(assimpVector);
glm::quat rotation = Tools::convertAiToGLM(assimpQuaternion);
```

## 🎮 **Animation Control**

```cpp
// Switch animations by name
instance->setAnimation("run");

// Switch by index
instance->setAnimationByIndex(0);

// Control playback speed
auto& settings = instance->getInstanceSettings();
settings.isAnimSpeedFactor = 2.0f; // 2x speed

// Get current animation info
auto currentAnim = instance->getCurrentAnimation();
if (currentAnim) {
    float duration = currentAnim->getClipDuration();
    std::string name = currentAnim->getClipName();
}
```

## 🏃‍♂️ **Performance Considerations**

- **Bone Limit**: 4 bones per vertex (standard for real-time rendering)
- **Animation Updates**: Only update visible/active instances
- **Memory Efficient**: Shared models with instanced data
- **Coordinate Conversion**: Handled once during loading, not per-frame

## 🚧 **Future Integration Points**

This system is designed to integrate with:

- **Vulkan Rendering Pipeline** - Animated vertex shaders
- **Scene System** - Integration with AvengSceneLoader
- **Component System** - ECS integration for game objects
- **Physics System** - Bone-based collision detection
- **Audio System** - Animation event callbacks

## 📁 **File Structure**

```
Midnight/Core/Animation/
├── AssimpModel.h/cpp           # Main model class
├── AssimpInstance.h/cpp        # Instance management
├── AnimationManager.h/cpp      # Central manager with callbacks
├── AssimpAnimClip.h/cpp        # Animation clip data
├── AssimpAnimChannel.h/cpp     # Bone animation channels
├── AssimpMesh.h/cpp           # Mesh with bone weights
├── AssimpNode.h/cpp           # Scene node hierarchy
├── AssimpBone.h/cpp           # Bone definitions
├── Tools.h                    # Coordinate conversion utilities
├── Logger.h/cpp               # Debug logging system
├── PlaceholderBuffers.h       # Placeholder Vulkan types
├── ModelAndInstanceData.h     # Callback pattern definitions
└── InstanceSettings.h         # Instance configuration
```

The system is now ready for both static mesh loading and full skeletal animation support!
