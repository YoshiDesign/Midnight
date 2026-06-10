# Midnight Engine

Midnight is a modular C++ game engine built around Vulkan.
It consists of three layers:

### The repo contains 3 major components:

1. **Midnight/** - core rendering, ECS-lite, resource management, concurrency policy and structures. Located in Midnight/ directory. *Compiles to a static library.* The engine is still in its early stages.
2. **MidnightEditor/** - ImGui-based tooling. Located in MidnightEditor/ directory. *Compiles to a static library.*
3. **X1/** - Game/Interactive Application: runtime/business logic. Located in X1/ directory. *This is the compiled target.*

### Other Components
**vendor/** - 3rd party tooling compiled with the target.

**shaders/** - (GLSL) Shaders compiled to SPIR-V using `glslc`

## Cool Features
- Easily configurable **Multi-buffering** 
- Low power consumption by default. This can easily be addressed by the swapchain if more power is required for smoother refresh rates
- Classic descriptor sets are available but **bindless descriptor set** support is available and in use for the performance benefits. Recommend sticking with this architecture unless your needs are very simple.
- **Advanced Debugging** support for validation layers: GPU Assisted and Synchronization validation can be enabled if desired (at a great performance cost, naturally)
- Unified Compute and Graphics queue when available.
- See the `manage2` branch for high performance concurrency structures as the procedural terrain generation system comes together.
- Vulkan will prioritize discreet graphics over integrated

## Notes on Dep's
Vulkan 1.3^ must be included and linked against from your machine. Every other dependency is included in the project.

HLSL is not yet supported. Shaders are pre-compiled to SPIR-V via `glslc` which is often included with the Vulkan SDK of your choice.

The use of Tiny Object Loader has been deprecated.

## Setup Instructions (TODO)
There is no makefile for this project yet as it is still in early development on Windows. The .sln contains much of the setup configuration however you'll need to set each project as a reference to its dependency project (the 3 list items at the top of this readme)
