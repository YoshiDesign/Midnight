# Instanced Rendering Fix

## Summary

Fixed the instanced rendering implementation to properly use GPU instancing for rendering multiple instances of the same model in a single draw call.

## Changes Made

### 1. AvengModel Class (Midnight/Core/aveng_model.h & .cpp)

#### Added Methods:

- `bindInstanced(VkCommandBuffer, VkBuffer)` - Binds both vertex buffer (binding 0) and instance buffer (binding 1)
- `getInstancedBindingDescriptions()` - Returns binding descriptions for instanced rendering
  - Binding 0: Per-vertex data (VK_VERTEX_INPUT_RATE_VERTEX)
  - Binding 1: Per-instance data (VK_VERTEX_INPUT_RATE_INSTANCE)
- `getInstancedAttributeDescriptions()` - Returns attribute descriptions for instanced rendering
  - Locations 0-3: Vertex attributes (position, color, normal, texCoord)
  - Locations 4-7: Instance modelMatrix (mat4 = 4 vec4s)
  - Locations 8-11: Instance normalMatrix (mat4 = 4 vec4s)
  - Location 12: Instance textureIndex (int)

### 2. Shaders

#### Created New Instanced Shaders:

- `shaders/simple_shader_instanced.vert` - Vertex shader that reads instance data from vertex attributes
- `shaders/simple_shader_instanced.frag` - Fragment shader that receives texture index from vertex shader

#### Key Differences from Standard Shaders:

- Vertex shader uses per-instance attributes instead of push constants for modelMatrix and normalMatrix
- Fragment shader receives texture index through vertex stage instead of uniform buffer
- Uses `gl_InstanceIndex` implicitly through vertex attribute system

### 3. Renderer (Midnight/Core/Renderer/Renderer.cpp)

#### Fixed `renderObjectsInstanced()`:

**Before (BROKEN):**

- Looped through each instance
- Set push constants per instance (only last one was used!)
- Bound descriptor sets per instance (unnecessary overhead)
- Called drawInstanced once with all instances (but only last instance's data was active)

**After (CORRECT):**

- Groups objects by model into batches
- Updates instance buffer ONCE per batch
- Binds instance buffer using `bindInstanced()`
- Calls `drawInstanced()` ONCE per batch with correct instance count
- Shader reads instance-specific data (modelMatrix, normalMatrix, textureIndex) from instance buffer

### 4. Pipeline Configuration

#### Updated PipelineConfigManager.cpp:

- Added detection for "INSTANCED" in pipeline name
- Automatically uses `AvengModel::getInstancedBindingDescriptions()` and `getInstancedAttributeDescriptions()` for instanced pipelines

#### Added to PipelineConfig.json:

- STANDARD_INSTANCED (ID: 10)
- WIREFRAME_INSTANCED (ID: 11)
- DISTORTED_INSTANCED (ID: 12)

#### Updated renderObjectsInstanced():

- Now uses instanced pipeline IDs (currentMode + 10)
- Falls back to standard rendering if instanced pipeline not found

### 5. Shader Compilation (compile.bat)

Added compilation commands for instanced shaders:

```batch
C:\VulkanSDK\1.4.309.0\Bin\glslc.exe shaders\simple_shader_instanced.vert -o shaders\simple_shader_instanced.vert.spv
C:\VulkanSDK\1.4.309.0\Bin\glslc.exe shaders\simple_shader_instanced.frag -o shaders\simple_shader_instanced.frag.spv
```

## How Instanced Rendering Works Now

1. **Batching Phase:**

   - Objects are grouped by model into RenderBatch structures
   - Each batch contains instance data (modelMatrix, normalMatrix, textureIndex) for all instances

2. **Buffer Update:**

   - Instance buffer is created/updated with all instance data for the batch

3. **Binding Phase:**

   - Instanced pipeline is bound (uses instanced vertex attribute layout)
   - Global descriptor set is bound (camera matrices, lights)
   - Model vertex buffer is bound to binding 0
   - Instance buffer is bound to binding 1

4. **Drawing Phase:**
   - Single `vkCmdDrawIndexed()` call with instanceCount parameter
   - GPU automatically increments `gl_InstanceIndex` for each instance
   - Vertex shader reads instance data from instance buffer using implicit instance index
   - Each vertex is transformed by its instance's modelMatrix and normalMatrix

## Performance Benefits

- **Before:** N objects = N draw calls + N descriptor bindings + N push constant updates
- **After:** N objects of same model = 1 draw call + 1 instance buffer update
- Massive reduction in CPU overhead for scenes with many instances of the same model

## Important Notes

### **MUST DO BEFORE RUNNING:**

Run `compile.bat` to compile the instanced shaders, or manually run:

```batch
C:\VulkanSDK\1.4.309.0\Bin\glslc.exe shaders\simple_shader_instanced.vert -o shaders\simple_shader_instanced.vert.spv
C:\VulkanSDK\1.4.309.0\Bin\glslc.exe shaders\simple_shader_instanced.frag -o shaders\simple_shader_instanced.frag.spv
```

### InstanceData Structure

The InstanceData struct in `Midnight/Core/data.h` must match the vertex attribute layout:

```cpp
struct InstanceData {
    alignas(16) glm::mat4 modelMatrix;     // locations 4-7
    alignas(16) glm::mat4 normalMatrix;    // locations 8-11
    alignas(16) int textureIndex;          // location 12
    alignas(16) int padding[3];            // Ensure proper alignment
};
```

### Pipeline Selection

Instanced rendering is controlled by:

1. `Renderer::setInstancedRenderingEnabled(bool)` - Enable/disable instanced rendering
2. When enabled, `renderObjectsInstanced()` automatically uses instanced pipelines (mode + 10)
3. Falls back to standard rendering if instanced pipelines unavailable

## Testing

After compiling shaders, instanced rendering should:

- Display all instances of each model
- Each instance should have correct transformation
- Each instance should use correct texture
- Performance should improve with more instances of the same model

## Future Improvements

1. Consider adding ANIMATED_INSTANCED pipeline for animated instanced models
2. Could optimize further by using GPU-side culling with instanced rendering
3. Could add support for varying instance attributes (e.g., per-instance color tint)
