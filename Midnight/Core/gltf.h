#pragma once
#include <cstdint>
#define INT32_MAX        2147483647i32
namespace aveng {

    static const int32_t INVALID_INT_VALUE = 2147483647;
    static_assert(INVALID_INT_VALUE == INT32_MAX, "Mismatch between invalid int and int32_t max");
    static const float INVALID_FLOAT_VALUE = 3.402823466e+38F;

    struct Asset {
        //// StringBuffer                         copyright;
        //// StringBuffer                         generator;
        //// StringBuffer                         minVersion;
        //// StringBuffer                         version;
    };

    struct CameraOrthographic {
        float xmag;
        float ymag;
        float zfar;
        float znear;
    };

    struct AccessorSparse {
        int32_t count;
        int32_t indices;
        int32_t values;
    };

    struct Camera {
        int32_t orthographic;
        int32_t perspective;
        // perspective
        // orthographic
        // // StringBuffer                type;
    };

    struct AnimationChannel {

        enum TargetType {
            Translation, Rotation, Scale, Weights, Count
        };

        int32_t sampler;
        int32_t target_node;
        TargetType target_type;
    };

    struct AnimationSampler {
        int32_t input_keyframe_buffer_index;  //"The index of an accessor containing keyframe timestamps."
        int32_t output_keyframe_buffer_index; // "The index of an accessor, containing keyframe output values."

        enum Interpolation {
            Linear, Step, CubicSpline, Count
        };
        // LINEAR The animated values are linearly interpolated between keyframes. When targeting a rotation, spherical linear interpolation (slerp) **SHOULD** be used to interpolate quaternions. The float of output elements **MUST** equal the float of input elements.
        // STEP The animated values remain constant to the output of the first keyframe, until the next keyframe. The float of output elements **MUST** equal the float of input elements.
        // CUBICSPLINE The animation's interpolation is computed using a cubic spline with specified tangents. The float of output elements **MUST** equal three times the float of input elements. For each input element, the output stores three elements, an in-tangent, a spline vertex, and an out-tangent. There **MUST** be at least two keyframes when using this interpolation.
        Interpolation interpolation;
    };

    struct Skin {
        int32_t  inverse_bind_matrices_buffer_index;
        int32_t  skeleton_root_node_index;
        uint32_t joints_count;
        int32_t* joints;
    };

    struct BufferView {
        enum Target {
            ARRAY_BUFFER = 34962 /* Vertex Data */, ELEMENT_ARRAY_BUFFER = 34963 /* Index Data */
        };

        int32_t buffer;
        int32_t byte_length;
        int32_t byte_offset;
        int32_t byte_stride;
        int32_t target;
        // StringBuffer                name;
    };

    struct Image {
        int32_t                         buffer_view;
        // image/jpeg
        // image/png
        // StringBuffer                mime_type;
        // StringBuffer                uri;
    };

    struct Node {
        int32_t camera;
        uint32_t children_count;
        int32_t* children;
        uint32_t matrix_count;
        float* matrix;
        int32_t mesh;
        uint32_t rotation_count;
        float* rotation;
        uint32_t scale_count;
        float* scale;
        int32_t skin;
        uint32_t translation_count;
        float* translation;
        uint32_t weights_count;
        float* weights;
        // StringBuffer                name;
    };

    struct TextureInfo {
        int32_t                         index;
        int32_t                         texCoord;
    };

    struct MaterialPBRMetallicRoughness {
        uint32_t base_color_factor_count;
        float* base_color_factor;
        TextureInfo* base_color_texture;
        float metallic_factor;
        TextureInfo* metallic_roughness_texture;
        float roughness_factor;
    };

    struct MeshPrimitive {
        struct Attribute {
            // StringBuffer            key;
            int32_t accessor_index;
        };

        uint32_t attribute_count;
        Attribute* attributes;
        int32_t indices;
        int32_t material;
        // 0 POINTS
        // 1 LINES
        // 2 LINE_LOOP
        // 3 LINE_STRIP
        // 4 TRIANGLES
        // 5 TRIANGLE_STRIP
        // 6 TRIANGLE_FAN
        int32_t mode;
        // uint32_t targets_count;
        // object* targets; // TODO(marco): this is a json object
    };

    struct AccessorSparseIndices {
        int32_t buffer_view;
        int32_t byte_offset;
        // 5121 UNSIGNED_BYTE
        // 5123 UNSIGNED_SHORT
        // 5125 UNSIGNED_INT
        int32_t component_type;
    };

    struct Accessor {
        enum ComponentType {
            BYTE = 5120, UNSIGNED_BYTE = 5121, SHORT = 5122, UNSIGNED_SHORT = 5123, UNSIGNED_INT = 5125, FLOAT = 5126
        };

        enum Type {
            Scalar, Vec2, Vec3, Vec4, Mat2, Mat3, Mat4
        };

        int32_t     buffer_view;
        int32_t     byte_offset;

        int32_t     component_type;
        int32_t     count;
        uint32_t    max_count;
        float*      max;
        uint32_t    min_count;
        float*      min;
        bool        normalized;
        int32_t     sparse;
        Type        type;
    };

    struct Texture {
        int32_t sampler;
        int32_t source;
        // StringBuffer                name;
    };

    struct MaterialNormalTextureInfo {
        int32_t index;
        int32_t tex_coord;
        float   scale;
    };

    struct Mesh {
        uint32_t        primitives_count;
        MeshPrimitive*  primitives;
        uint32_t        weights_count;
        float*          weights;
        // StringBuffer                name;
    };

    struct MaterialOcclusionTextureInfo {
        int32_t index;
        int32_t texCoord;
        float   strength;
    };

    struct Material {
        float alpha_cutoff;
        // OPAQUE The alpha value is ignored, and the rendered output is fully opaque.
        // MASK The rendered output is either fully opaque or fully transparent depending on the alpha value and the specified `alphaCutoff` value; the exact appearance of the edges **MAY** be subject to implementation-specific techniques such as "`Alpha-to-Coverage`".
        // BLEND The alpha value is used to composite the source and destination areas. The rendered output is combined with the background using the normal painting operation (i.e. the Porter and Duff over operator).
        // StringBuffer                alpha_mode;
        bool         double_sided;
        uint32_t     emissive_factor_count;
        float*       emissive_factor;
        TextureInfo* emissive_texture;
        MaterialNormalTextureInfo*      normal_texture;
        MaterialOcclusionTextureInfo*   occlusion_texture;
        MaterialPBRMetallicRoughness*   pbr_metallic_roughness;
        // StringBuffer                name;
    };

    struct Buffer {
        int32_t byte_length;
        // StringBuffer uri;
        // StringBuffer name;
    };

    struct CameraPerspective {
        float aspect_ratio;
        float yfov;
        float zfar;
        float znear;
    };

    struct Animation {
        uint32_t            channels_count;
        AnimationChannel*   channels;
        uint32_t            samplers_count;
        AnimationSampler*   samplers;
    };

    struct AccessorSparseValues {
        int32_t bufferView;
        int32_t byteOffset;
    };

    struct Scene {
        uint32_t nodes_count;
        int32_t* nodes;
    };

    struct Sampler {
        enum Filter {
            NEAREST = 9728, LINEAR = 9729, NEAREST_MIPMAP_NEAREST = 9984, LINEAR_MIPMAP_NEAREST = 9985, NEAREST_MIPMAP_LINEAR = 9986, LINEAR_MIPMAP_LINEAR = 9987
        };

        enum Wrap {
            CLAMP_TO_EDGE = 33071, MIRRORED_REPEAT = 33648, REPEAT = 10497
        };

        int32_t mag_filter;
        int32_t min_filter;
        int32_t wrap_s;
        int32_t wrap_t;
    };

    struct glTF {
        uint32_t    accessors_count;
        Accessor*   accessors;
        uint32_t    animations_count;
        Animation*  animations;
        Asset       asset;
        uint32_t    buffer_views_count;
        BufferView* buffer_views;
        uint32_t    buffers_count;
        Buffer*     buffers;
        uint32_t    cameras_count;
        Camera*     cameras;
        uint32_t    extensions_required_count;
        // StringBuffer* extensions_required;
        uint32_t    extensions_used_count;
        // StringBuffer* extensions_used;
        uint32_t    images_count;
        Image*      images;
        uint32_t    materials_count;
        Material*   materials;
        uint32_t    meshes_count;
        Mesh*       meshes;
        uint32_t    nodes_count;
        Node*       nodes;
        uint32_t    samplers_count;
        Sampler*    samplers;
        int32_t     scene;
        uint32_t    scenes_count;
        Scene*      scenes;
        uint32_t    skins_count;
        Skin*       skins;
        uint32_t    textures_count;
        Texture*    textures;

        // LinearAllocator             allocator;
    };
}
