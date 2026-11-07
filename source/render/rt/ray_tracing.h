#pragma once

#include "render/vulkan/render_device.h"

// Ray tracing (VK_KHR_acceleration_structure + VK_KHR_ray_query). Optional:
// initialize() fails when the device has no ray tracing (or -nort was passed)
// and every other call is then a no-op. See docs/re/ray_tracing.md for what
// this can and cannot do here.
//
// Bottom level structures are built once per mesh: the Eden meshes are
// uploaded once and never change. Top level structures are rebuilt from the
// instance list. Both are built on the GPU through a one time submit, so a
// build must be asked for outside a frame.
class RayTracing {
public:
    // One triangle mesh: a sub range of a shared vertex / index buffer, which
    // is how EdenModel stores its primitives.
    struct Blas {
        VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
        GPUBuffer *pStorage = nullptr;
        VkDeviceSize Size = 0;
        uint32_t PrimitiveCount = 0;
        VkDeviceAddress Address = 0;
    };

    // The instances of one frame.
    struct Tlas {
        VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
        GPUBuffer *pStorage = nullptr;
        GPUBuffer *pInstances = nullptr; // VkAccelerationStructureInstanceKHR[Capacity]
        uint32_t Capacity = 0;
        VkDeviceSize Size = 0;
        VkDeviceAddress Address = 0;
    };

    struct Instance {
        VkDeviceAddress Blas = 0;
        Matrix4x4f Transform; // model -> world, the renderer's matrices
        uint32_t Mask = 0xff;
    };

    // The first committed hit of one traced ray.
    struct ProbeResult {
        uint32_t Hit = 0;
        float Distance = 0.0f;
        uint32_t Instance = 0;
        uint32_t Primitive = 0;
    };

public:
    RayTracing();
    ~RayTracing();

    // False when this device has no ray tracing: everything else is a no-op.
    bool initialize( RenderDevice &device );
    void destroy();
    inline bool isReady() const { return bReady; }

    // Usage flags a vertex / index buffer needs to be usable as an
    // acceleration structure build input (0 when ray tracing is unavailable).
    static VkBufferUsageFlags getBuildInputUsage();

    // vertexFormat: VK_FORMAT_R32G32B32_SFLOAT or VK_FORMAT_R16G16B16A16_SFLOAT
    // (the two the Eden meshes use). nullptr on failure.
    Blas *createTriangleBlas( VkDeviceAddress vertexAddress, uint32_t vertexStride, VkFormat vertexFormat,
                              uint32_t vertexCount, VkDeviceAddress indexAddress, uint32_t indexCount );
    void destroyBlas( Blas *pBlas );

    Tlas *createTlas( uint32_t maxInstances );
    void destroyTlas( Tlas *pTlas );
    // Uploads the instances and builds the top level structure.
    bool buildTlas( Tlas *pTlas, const Instance *pInstances, uint32_t numInstances );

    // Traces one ray against pTlas. The self test: it proves the whole chain,
    // the acceleration structures, the ray query shader and the descriptors.
    bool traceProbe( Tlas *pTlas, const Vector3f &origin, const Vector3f &direction, float maxDistance,
                     ProbeResult &outResult );

    // Screen space ray traced ambient occlusion (-rtao, docs/re/ray_tracing.md
    // step 6): records one compute dispatch into the frame's command buffer —
    // unlike traceProbe there is no own submit and no host readback. pDepth is
    // the scene depth as a sampled texture, pDestination an R8 target written
    // as a storage image (the scene blit multiplies it on). GPU cost scales
    // with AoParams::samples: the [perf] gpu "rtao" pass measures it.
    struct AoParams {
        float CameraPosition[4];
        float Right[4];   // camera right * tan(fov/2) * aspect
        float Up[4];      // camera up * tan(fov/2)
        float Forward[4];
        float Params[4];  // near, far, occlusion radius (m), frame number
        float Params2[4]; // width, height, samples
    };
    bool dispatchAO( VkCommandBuffer commandBuffer, Tlas *pTlas, GPUTexture *pDepth, GPUTexture *pDestination,
                     const AoParams &params, uint32_t frameSlot );

    // Builds one triangle whose answer is known in advance and traces a ray
    // that must hit it and one that must miss it. Logs both results.
    bool runSelfTest();

private:
    RenderDevice *pDevice = nullptr;
    bool bReady = false;
    GPUBuffer *pScratch = nullptr;
    VkDeviceSize scratchSize = 0;
    GPUBuffer *pProbeResult = nullptr;
    // Every structure created here, so destroy() can release the ones a caller
    // still holds (a model unloaded after the renderer has gone).
    std::vector<Blas *> blases;
    std::vector<Tlas *> tlases;
    VkDescriptorSetLayout probeSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout probePipelineLayout = VK_NULL_HANDLE;
    VkPipeline probePipeline = VK_NULL_HANDLE;
    VkDescriptorPool probePool = VK_NULL_HANDLE;
    VkDescriptorSet probeSet = VK_NULL_HANDLE;
    // The -rtao pass (see dispatchAO): one set per frame slot, rewritten at
    // record time - safe because the slot's previous submission is complete
    // by then (its fence was waited in beginFrame).
    bool ensureAoPipeline();
    VkDescriptorSetLayout aoSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout aoPipelineLayout = VK_NULL_HANDLE;
    VkPipeline aoPipeline = VK_NULL_HANDLE;
    VkDescriptorPool aoPool = VK_NULL_HANDLE;
    VkDescriptorSet aoSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkSampler aoSampler = VK_NULL_HANDLE;

    bool ensureScratch( VkDeviceSize size );
    bool ensureProbePipeline();
    // Device address of the persistent scratch buffer, aligned to
    // minAccelerationStructureScratchOffsetAlignment.
    VkDeviceAddress getScratchAddress() const;
};
