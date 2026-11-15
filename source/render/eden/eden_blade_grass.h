#pragma once

#include "core/math.h"
#include "render/vulkan/render_device.h"

#include <unordered_map>
#include <vector>

// Port-native grass blades (resources/shaders/bladegrass.*): real tapered blade geometry near the camera,
// bent by the wind and pushed aside / flattened by the player's car in the vertex shader. The blades are
// emitted by EdenWorld::buildGrassBatch next to the retail-style cards (a handful of blades in the cell
// of every card within the blade distance) and drawn here in the scene's effects pass.
struct BladeVertex {
    float Root[3];
    uint8_t T, Side, Hash, Flex; // t along the blade, side (0 / 255), hash, flexibility
    uint8_t R, G, B, Width;
    uint16_t Height, Yaw; // height / 1.5 m and yaw / 2pi, UNORM16
};
static_assert( sizeof( BladeVertex ) == 24, "BladeVertex layout" );

// Appends one blade (7 vertices, 15 indices).
void AppendBlade( std::vector<BladeVertex> &vertices, std::vector<uint32_t> &indices, const Vector3f &root, float height,
                  float yaw, float flex, float hash, const Vector3f &tint, float width );

class EdenBladeGrass {
public:
    struct Frame {
        float Time = 0.0f;
        float WindAmplitude = 0.12f;
        bool bCar = false;
        Vector3f CarPosition;
        Vector3f CarForward = Vector3f( 0.0f, 0.0f, 1.0f );
        float CarHalfLength = 2.4f;
        float CarHalfWidth = 1.1f;
        Vector3f Camera;
        Vector3f Light = Vector3f( 1.0f, 1.0f, 1.0f );
        float FadeEnd = 30.0f;
    };
    void draw( RenderDevice &device, const Matrix4x4f &viewProjection, const Frame &frame,
               const std::vector<BladeVertex> &vertices, const std::vector<uint32_t> &indices );
    void destroy( RenderDevice &device );

private:
    std::unordered_map<VkRenderPass, VkPipeline> pipelines;
};

EdenBladeGrass &GetBladeGrass();
