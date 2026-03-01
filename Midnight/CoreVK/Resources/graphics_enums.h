#pragma once
#include <cstdint>

namespace ResourceTypes {

    enum Enum {
        Immutable, Dynamic, Stream, Count
    };

    enum Mask {
        Immutable_mask = 1 << 0, Dynamic_mask = 1 << 1, Stream_mask = 1 << 2, Count_mask = 1 << 3
    };

    static const char* s_value_names[] = {
        "Immutable", "Dynamic", "Stream", "Count"
    };

    static const char* ToString(Enum e) {
        return ((uint32_t)e < Enum::Count ? s_value_names[(int)e] : "unsupported");
    }

} // namespace ResourceType

namespace TextureType {
    enum Enum {
        Texture1D, Texture2D, Texture3D, Texture_1D_Array, Texture_2D_Array, Texture_Cube_Array, Count
    };

    enum Mask {
        Texture1D_mask = 1 << 0, Texture2D_mask = 1 << 1, Texture3D_mask = 1 << 2, Texture_1D_Array_mask = 1 << 3, Texture_2D_Array_mask = 1 << 4, Texture_Cube_Array_mask = 1 << 5, Count_mask = 1 << 6
    };

    static const char* s_value_names[] = {
        "Texture1D", "Texture2D", "Texture3D", "Texture_1D_Array", "Texture_2D_Array", "Texture_Cube_Array", "Count"
    };

    static const char* ToString(Enum e) {
        return ((uint32_t)e < Enum::Count ? s_value_names[(int)e] : "unsupported");
    }
} // namespace TextureType
