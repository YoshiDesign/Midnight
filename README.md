# Midnight Engine

Midnight is a modular C++ game engine built around Vulkan.
It consists of three layers:

- Engine: core rendering, ECS-lite, resource management. Located in Midnight/ directory
- Editor: ImGui-based tooling. Located in MidnightEditor/ directory
- Game: runtime game logic. Located in X1/ directory

The Engine must never depend on Editor code.
The Editor may depend on the Engine.
The Game depends only on the Engine.
