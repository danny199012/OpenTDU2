#include "shared.h"
#include "render/eden/eden_blade_grass.h"

#include "render/shaders/builtin/bladegrass_frag.h"
#include "render/shaders/builtin/bladegrass_vert.h"

namespace {
inline uint8_t Unorm8( float v ) { return static_cast<uint8_t>( Clamp( v, 0.0f, 1.0f ) * 255.0f + 0.5f ); }
inline uint16_t Unorm16( float v ) { return static_cast<uint16_t>( Clamp( v, 0.0f, 1.0f ) * 65535.0f + 0.5f ); }
} // namespace

void AppendBlade( std::vector<BladeVertex> &vertices, std::vector<uint32_t> &indices, const Vector3f &root, float height,
                  float yaw, float flex, float hash, const Vector3f &tint, float width ) {
    static const float kRows[3] = { 0.0f, 0.38f, 0.72f };
    const uint32_t base = static_cast<uint32_t>( vertices.size() );
    BladeVertex vertex = {};
    vertex.Root[0] = root.x;
    vertex.Root[1] = root.y;
    vertex.Root[2] = root.z;
    vertex.Hash = Unorm8( hash );
    vertex.Flex = Unorm8( flex );
    vertex.R = Unorm8( tint.x );
    vertex.G = Unorm8( tint.y );
    vertex.B = Unorm8( tint.z );
    vertex.Width = Unorm8( width );
    vertex.Height = Unorm16( height / 1.5f );
    float yawTurns = yaw / 6.2831853f;
    yawTurns -= floorf( yawTurns );
    vertex.Yaw = Unorm16( yawTurns );
    for ( int32_t row = 0; row < 3; row++ ) {
        vertex.T = Unorm8( kRows[row] );
        vertex.Side = 0;
        vertices.push_back( vertex );
        vertex.Side = 255;
        vertices.push_back( vertex );
    }
    vertex.T = 255;
    vertex.Side = 128;
    vertices.push_back( vertex );
    for ( uint32_t row = 0; row < 2; row++ ) {
        const uint32_t i = base + row * 2;
        const uint32_t tri[6] = { i, i + 1, i + 2, i + 1, i + 3, i + 2 };
        indices.insert( indices.end(), tri, tri + 6 );
    }
    const uint32_t top = base + 4;
    indices.push_back( top );
    indices.push_back( top + 1 );
    indices.push_back( top + 2 );
}

void EdenBladeGrass::draw( RenderDevice &device, const Matrix4x4f &viewProjection, const Frame &frame,
                           const std::vector<BladeVertex> &vertices, const std::vector<uint32_t> &indices ) {
    if ( vertices.empty() || indices.empty() || device.getCurrentRenderPass() == VK_NULL_HANDLE ) {
        return;
    }
    const VkRenderPass renderPass = device.getCurrentRenderPass();
    VkPipeline &pipeline = pipelines[renderPass];
    if ( pipeline == VK_NULL_HANDLE ) {
        PipelineDesc desc;
        desc.pVertexSPIRV = kSPIRV_bladegrass_vert;
        desc.VertexSPIRVSize = sizeof( kSPIRV_bladegrass_vert );
        desc.pFragmentSPIRV = kSPIRV_bladegrass_frag;
        desc.FragmentSPIRVSize = sizeof( kSPIRV_bladegrass_frag );
        desc.Attributes = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof( BladeVertex, Root ) },
                            { 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof( BladeVertex, T ) },
                            { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof( BladeVertex, R ) },
                            { 3, 0, VK_FORMAT_R16G16_UNORM, offsetof( BladeVertex, Height ) } };
        desc.BindingStrides = { sizeof( BladeVertex ) };
        desc.Blend = eBlendMode::BM_Opaque;
        desc.Cull = eCullMode::CM_None;
        desc.bDepthTest = true;
        desc.bDepthWrite = true;
        desc.RenderPass = renderPass;
        pipeline = device.createPipeline( desc );
        if ( pipeline == VK_NULL_HANDLE ) {
            OTDU_LOG_WARN( "EdenBladeGrass: pipeline creation failed\n" );
            return;
        }
    }
    struct Push {
        float ViewProjection[16];
        float P0[4], P1[4], P2[4], P3[4];
    } push = {};
    memcpy( push.ViewProjection, &viewProjection.m[0][0], sizeof( push.ViewProjection ) );
    push.P0[0] = frame.Time;
    push.P0[1] = frame.WindAmplitude;
    push.P0[2] = frame.CarHalfLength;
    push.P0[3] = frame.CarHalfWidth;
    push.P1[0] = frame.CarPosition.x;
    push.P1[1] = frame.CarPosition.y;
    push.P1[2] = frame.CarPosition.z;
    push.P1[3] = frame.FadeEnd;
    push.P2[0] = frame.CarForward.x;
    push.P2[1] = frame.CarForward.z;
    push.P2[2] = frame.Camera.x;
    push.P2[3] = frame.Camera.z;
    push.P3[0] = frame.Light.x;
    push.P3[1] = frame.Light.y;
    push.P3[2] = frame.Light.z;
    push.P3[3] = frame.bCar ? 1.0f : 0.0f;
    const TransientAllocation vertexAllocation = device.allocateTransient( vertices.size() * sizeof( BladeVertex ), 16 );
    const TransientAllocation indexAllocation = device.allocateTransient( indices.size() * sizeof( uint32_t ), 16 );
    if ( vertexAllocation.pData == nullptr || indexAllocation.pData == nullptr ) {
        return;
    }
    memcpy( vertexAllocation.pData, vertices.data(), vertices.size() * sizeof( BladeVertex ) );
    memcpy( indexAllocation.pData, indices.data(), indices.size() * sizeof( uint32_t ) );
    VkCommandBuffer commandBuffer = device.getCommandBuffer();
    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdPushConstants( commandBuffer, device.getDefaultPipelineLayout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
    vkCmdBindVertexBuffers( commandBuffer, 0, 1, &vertexAllocation.Buffer, &vertexAllocation.Offset );
    vkCmdBindIndexBuffer( commandBuffer, indexAllocation.Buffer, indexAllocation.Offset, VK_INDEX_TYPE_UINT32 );
    vkCmdDrawIndexed( commandBuffer, static_cast<uint32_t>( indices.size() ), 1, 0, 0, 0 );
}

void EdenBladeGrass::destroy( RenderDevice &device ) {
    for ( auto &entry : pipelines ) {
        if ( entry.second != VK_NULL_HANDLE ) {
            device.destroyPipeline( entry.second );
        }
    }
    pipelines.clear();
}

EdenBladeGrass &GetBladeGrass() {
    static EdenBladeGrass instance;
    return instance;
}
