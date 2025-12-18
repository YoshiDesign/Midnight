# Midnight Engine

Midnight is a modular C++ game engine built around Vulkan.
It consists of three layers:

- Engine: core rendering, ECS-lite, resource management. Located in Midnight/ directory
- Editor: ImGui-based tooling. Located in MidnightEditor/ directory
- Game: runtime game logic. Located in X1/ directory

The Engine must never depend on Editor code.
The Editor may depend on Engine.
The Game depends only on Engine.

The engine implements double-buffering, so there are 2 sets of most resources; one per frameBuffer. The SwapChain also utilizes an imageIndex in a similar fashion for resources such as framebuffers, which depend on the current available image, not the current frame.
