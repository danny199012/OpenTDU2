#include "shared.h"
#include "render/rt/ray_tracing.h"

#include "render/shaders/builtin/rt_probe_comp.h"
#include "render/shaders/builtin/rt_ao_comp.h"

namespace {
// The shader writes exactly this (std430): hit, distance, instance, primitive.
struct ProbeResultRaw {
    uint32_t Hit;
    float Distance;
    uint32_t Instance;
    uint32_t Primitive;
};

struct ProbePushConstants {
    float Origin[4];
    float Direction[4];
    float Params[4]; // x: maximum distance
};

// Instance transform: VkTransformMatrixKHR is a row major 3x4 matrix whose
// last column is the translation (VUID-VkTransformMatrixKHR-matrix-03799: the
// first three columns are the invertible 3x3). The Eden matrices are column
// major (m[column][row]), so the stored form is the transpose:
//   vk.matrix[i][j] = eden.m[j][i]
void FillInstanceTransform( const Matrix4x4f &source, VkTransformMatrixKHR &out ) {
    for ( int32_t row = 0; row < 3; row++ ) {
        for ( int32_t column = 0; column < 4; column++ ) {
            out.matrix[row][column] = source.m[column][row];
        }
    }
}

constexpr uint32_t kProbeResultSize = sizeof( ProbeResultRaw );
} // namespace

RayTracing::RayTracing() {}

RayTracing::~RayTracing() {
    destroy();
}

bool RayTracing::initialize( RenderDevice &device ) {
    pDevice = &device;
    if ( !device.supportsRayTracing() ) {
        return false;
    }
    bReady = true;
    return true;
}

void RayTracing::destroy() {
    if ( pDevice != nullptr ) {
        // The structures first: their storage are device buffers.
        while ( !blases.empty() ) {
            destroyBlas( blases.back() );
        }
        while ( !tlases.empty() ) {
            destroyTlas( tlases.back() );
        }
        VkDevice device = pDevice->getDevice();
        if ( probePipeline != VK_NULL_HANDLE ) {
            vkDestroyPipeline( device, probePipeline, nullptr );
        }
        if ( probePipelineLayout != VK_NULL_HANDLE ) {
            vkDestroyPipelineLayout( device, probePipelineLayout, nullptr );
        }
        if ( probeSetLayout != VK_NULL_HANDLE ) {
            vkDestroyDescriptorSetLayout( device, probeSetLayout, nullptr );
        }
        if ( probePool != VK_NULL_HANDLE ) {
            vkDestroyDescriptorPool( device, probePool, nullptr );
        }
        if ( pProbeResult != nullptr ) {
            pDevice->destroyBuffer( pProbeResult );
        }
        if ( aoPipeline != VK_NULL_HANDLE ) {
            vkDestroyPipeline( device, aoPipeline, nullptr );
        }
        if ( aoPipelineLayout != VK_NULL_HANDLE ) {
            vkDestroyPipelineLayout( device, aoPipelineLayout, nullptr );
        }
        if ( aoSetLayout != VK_NULL_HANDLE ) {
            vkDestroyDescriptorSetLayout( device, aoSetLayout, nullptr );
        }
        if ( aoPool != VK_NULL_HANDLE ) {
            vkDestroyDescriptorPool( device, aoPool, nullptr );
        }
        if ( aoSampler != VK_NULL_HANDLE ) {
            // From the device's sampler heap: it destroys them.
            aoSampler = VK_NULL_HANDLE;
        }
        if ( pScratch != nullptr ) {
            pDevice->destroyBuffer( pScratch );
        }
    }
    probePipeline = VK_NULL_HANDLE;
    probePipelineLayout = VK_NULL_HANDLE;
    probeSetLayout = VK_NULL_HANDLE;
    probePool = VK_NULL_HANDLE;
    probeSet = VK_NULL_HANDLE;
    aoPipeline = VK_NULL_HANDLE;
    aoPipelineLayout = VK_NULL_HANDLE;
    aoSetLayout = VK_NULL_HANDLE;
    aoPool = VK_NULL_HANDLE;
    aoSets[0] = aoSets[1] = VK_NULL_HANDLE;
    pProbeResult = nullptr;
    pScratch = nullptr;
    scratchSize = 0;
    bReady = false;
}

VkBufferUsageFlags RayTracing::getBuildInputUsage() {
    return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
}

bool RayTracing::ensureScratch( VkDeviceSize size ) {
    // One extra alignment of slack so the scratch address can be aligned
    // whatever the buffer base address happens to be.
    const VkDeviceSize needed = size + pDevice->getAccelScratchAlignment();
    if ( pScratch != nullptr && scratchSize >= needed ) {
        return true;
    }
    if ( pScratch != nullptr ) {
        pDevice->destroyBuffer( pScratch );
        pScratch = nullptr;
        scratchSize = 0;
    }
    pScratch = pDevice->createDeviceBuffer( needed, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT );
    if ( pScratch == nullptr ) {
        return false;
    }
    scratchSize = needed;
    return true;
}

VkDeviceAddress RayTracing::getScratchAddress() const {
    const VkDeviceAddress base = pDevice->getBufferDeviceAddress( pScratch );
    return base == 0 ? 0 : Align( base, pDevice->getAccelScratchAlignment() );
}

RayTracing::Blas *RayTracing::createTriangleBlas( VkDeviceAddress vertexAddress, uint32_t vertexStride,
                                                  VkFormat vertexFormat, uint32_t vertexCount,
                                                  VkDeviceAddress indexAddress, uint32_t indexCount ) {
    if ( !bReady || vertexAddress == 0 || indexAddress == 0 || vertexCount == 0 || indexCount < 3 ) {
        return nullptr;
    }
    VkDevice device = pDevice->getDevice();

    VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = vertexFormat;
    triangles.vertexData.deviceAddress = vertexAddress;
    triangles.vertexStride = vertexStride;
    triangles.maxVertex = vertexCount - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = indexAddress;
    // No transform here: the placement of a mesh comes from its TLAS instance.

    VkAccelerationStructureGeometryKHR geometry = {};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = triangles;

    const uint32_t primitiveCount = indexCount / 3;
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR( device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo,
                                             &primitiveCount, &sizes );
    if ( sizes.accelerationStructureSize == 0 || !ensureScratch( sizes.buildScratchSize ) ) {
        OTDU_LOG_ERROR( "Ray tracing: BLAS sizes unavailable (%u triangles)\n", primitiveCount );
        return nullptr;
    }

    Blas *pBlas = new Blas();
    pBlas->PrimitiveCount = primitiveCount;
    pBlas->Size = sizes.accelerationStructureSize;
    pBlas->pStorage = pDevice->createDeviceBuffer(
        sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT );
    if ( pBlas->pStorage == nullptr ) {
        delete pBlas;
        return nullptr;
    }

    VkAccelerationStructureCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = pBlas->pStorage->Buffer;
    createInfo.offset = 0;
    createInfo.size = sizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if ( vkCreateAccelerationStructureKHR( device, &createInfo, nullptr, &pBlas->Handle ) != VK_SUCCESS ) {
        pDevice->destroyBuffer( pBlas->pStorage );
        delete pBlas;
        return nullptr;
    }

    buildInfo.dstAccelerationStructure = pBlas->Handle;
    buildInfo.scratchData.deviceAddress = getScratchAddress();
    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR *pRange = &range;
    pDevice->executeImmediate( [&]( VkCommandBuffer commandBuffer ) {
        vkCmdBuildAccelerationStructuresKHR( commandBuffer, 1, &buildInfo, &pRange );
    } );

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = pBlas->Handle;
    pBlas->Address = vkGetAccelerationStructureDeviceAddressKHR( device, &addressInfo );
    blases.push_back( pBlas );
    return pBlas;
}

void RayTracing::destroyBlas( Blas *pBlas ) {
    if ( pBlas == nullptr ) {
        return;
    }
    if ( pBlas->Handle != VK_NULL_HANDLE && pDevice != nullptr ) {
        vkDestroyAccelerationStructureKHR( pDevice->getDevice(), pBlas->Handle, nullptr );
    }
    if ( pBlas->pStorage != nullptr && pDevice != nullptr ) {
        pDevice->destroyBuffer( pBlas->pStorage );
    }
    blases.erase( std::remove( blases.begin(), blases.end(), pBlas ), blases.end() );
    delete pBlas;
}

RayTracing::Tlas *RayTracing::createTlas( uint32_t maxInstances ) {
    if ( !bReady || maxInstances == 0 ) {
        return nullptr;
    }
    VkDevice device = pDevice->getDevice();

    VkAccelerationStructureGeometryInstancesDataKHR instancesData = {};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;

    VkAccelerationStructureGeometryKHR geometry = {};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR( device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo,
                                             &maxInstances, &sizes );
    if ( sizes.accelerationStructureSize == 0 || !ensureScratch( sizes.buildScratchSize ) ) {
        OTDU_LOG_ERROR( "Ray tracing: TLAS sizes unavailable (%u instances)\n", maxInstances );
        return nullptr;
    }

    Tlas *pTlas = new Tlas();
    pTlas->Capacity = maxInstances;
    pTlas->Size = sizes.accelerationStructureSize;
    pTlas->pStorage = pDevice->createDeviceBuffer(
        sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT );
    pTlas->pInstances = pDevice->createBuffer( sizeof( VkAccelerationStructureInstanceKHR ) * maxInstances,
                                               getBuildInputUsage(), nullptr );
    if ( pTlas->pStorage == nullptr || pTlas->pInstances == nullptr ) {
        destroyTlas( pTlas );
        return nullptr;
    }

    VkAccelerationStructureCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = pTlas->pStorage->Buffer;
    createInfo.offset = 0;
    createInfo.size = sizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if ( vkCreateAccelerationStructureKHR( device, &createInfo, nullptr, &pTlas->Handle ) != VK_SUCCESS ) {
        destroyTlas( pTlas );
        return nullptr;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = pTlas->Handle;
    pTlas->Address = vkGetAccelerationStructureDeviceAddressKHR( device, &addressInfo );
    tlases.push_back( pTlas );
    return pTlas;
}

void RayTracing::destroyTlas( Tlas *pTlas ) {
    if ( pTlas == nullptr ) {
        return;
    }
    if ( pTlas->Handle != VK_NULL_HANDLE && pDevice != nullptr ) {
        vkDestroyAccelerationStructureKHR( pDevice->getDevice(), pTlas->Handle, nullptr );
    }
    if ( pTlas->pStorage != nullptr && pDevice != nullptr ) {
        pDevice->destroyBuffer( pTlas->pStorage );
    }
    if ( pTlas->pInstances != nullptr && pDevice != nullptr ) {
        pDevice->destroyBuffer( pTlas->pInstances );
    }
    tlases.erase( std::remove( tlases.begin(), tlases.end(), pTlas ), tlases.end() );
    delete pTlas;
}

bool RayTracing::buildTlas( Tlas *pTlas, const Instance *pInstances, uint32_t numInstances ) {
    if ( !bReady || pTlas == nullptr || pInstances == nullptr || numInstances == 0 ||
         numInstances > pTlas->Capacity ) {
        return false;
    }

    std::vector<VkAccelerationStructureInstanceKHR> instances( numInstances );
    for ( uint32_t i = 0; i < numInstances; i++ ) {
        VkAccelerationStructureInstanceKHR &instance = instances[i];
        instance = {};
        FillInstanceTransform( pInstances[i].Transform, instance.transform );
        instance.mask = static_cast<uint8_t>( pInstances[i].Mask );
        instance.accelerationStructureReference = pInstances[i].Blas;
        // The Eden meshes have no consistent winding: never cull a face.
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    }
    pDevice->writeBuffer( pTlas->pInstances, instances.data(),
                          sizeof( VkAccelerationStructureInstanceKHR ) * numInstances );

    VkAccelerationStructureGeometryInstancesDataKHR instancesData = {};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = pDevice->getBufferDeviceAddress( pTlas->pInstances );

    VkAccelerationStructureGeometryKHR geometry = {};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.dstAccelerationStructure = pTlas->Handle;
    buildInfo.scratchData.deviceAddress = getScratchAddress();

    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount = numInstances;
    const VkAccelerationStructureBuildRangeInfoKHR *pRange = &range;
    pDevice->executeImmediate( [&]( VkCommandBuffer commandBuffer ) {
        vkCmdBuildAccelerationStructuresKHR( commandBuffer, 1, &buildInfo, &pRange );
    } );
    return true;
}

bool RayTracing::ensureProbePipeline() {
    if ( probePipeline != VK_NULL_HANDLE ) {
        return true;
    }
    VkDevice device = pDevice->getDevice();

    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if ( vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &probeSetLayout ) != VK_SUCCESS ) {
        return false;
    }

    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof( ProbePushConstants );

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &probeSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if ( vkCreatePipelineLayout( device, &pipelineLayoutInfo, nullptr, &probePipelineLayout ) != VK_SUCCESS ) {
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = sizeof( kSPIRV_rt_probe_comp );
    moduleInfo.pCode = kSPIRV_rt_probe_comp;
    VkShaderModule module = VK_NULL_HANDLE;
    if ( vkCreateShaderModule( device, &moduleInfo, nullptr, &module ) != VK_SUCCESS ) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = probePipelineLayout;
    const VkResult result = vkCreateComputePipelines( device, pDevice->getPipelineCache(), 1, &pipelineInfo, nullptr,
                                                     &probePipeline );
    vkDestroyShaderModule( device, module, nullptr );
    if ( result != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Ray tracing: the ray query compute pipeline failed (%d)\n",
                        static_cast<int32_t>( result ) );
        return false;
    }

    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if ( vkCreateDescriptorPool( device, &poolInfo, nullptr, &probePool ) != VK_SUCCESS ) {
        return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = probePool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &probeSetLayout;
    if ( vkAllocateDescriptorSets( device, &allocateInfo, &probeSet ) != VK_SUCCESS ) {
        return false;
    }

    pProbeResult = pDevice->createReadbackBuffer( kProbeResultSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    return pProbeResult != nullptr;
}

bool RayTracing::traceProbe( Tlas *pTlas, const Vector3f &origin, const Vector3f &direction, float maxDistance,
                             ProbeResult &outResult ) {
    if ( !bReady || pTlas == nullptr || pTlas->Handle == VK_NULL_HANDLE || !ensureProbePipeline() ) {
        return false;
    }
    VkDevice device = pDevice->getDevice();

    VkWriteDescriptorSetAccelerationStructureKHR accelWrite = {};
    accelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelWrite.accelerationStructureCount = 1;
    accelWrite.pAccelerationStructures = &pTlas->Handle;

    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = pProbeResult->Buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext = &accelWrite;
    writes[0].dstSet = probeSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = probeSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets( device, 2, writes, 0, nullptr );

    ProbePushConstants constants = {};
    constants.Origin[0] = origin.x;
    constants.Origin[1] = origin.y;
    constants.Origin[2] = origin.z;
    constants.Direction[0] = direction.x;
    constants.Direction[1] = direction.y;
    constants.Direction[2] = direction.z;
    constants.Params[0] = maxDistance;

    pDevice->executeImmediate( [&]( VkCommandBuffer commandBuffer ) {
        vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, probePipeline );
        vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, probePipelineLayout, 0, 1, &probeSet,
                                 0, nullptr );
        vkCmdPushConstants( commandBuffer, probePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            static_cast<uint32_t>( sizeof( constants ) ), &constants );
        vkCmdDispatch( commandBuffer, 1, 1, 1 );
        // The host reads the result once the fence is waited.
        VkMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                              &barrier, 0, nullptr, 0, nullptr );
    } );

    ProbeResultRaw raw = {};
    pDevice->readBuffer( pProbeResult, &raw, sizeof( raw ) );
    outResult.Hit = raw.Hit;
    outResult.Distance = raw.Distance;
    outResult.Instance = raw.Instance;
    outResult.Primitive = raw.Primitive;
    return true;
}

// The -rtao compute shader: TLAS (binding 0), the scene depth sampled (1),
// the R8 mask as a storage image out (2); the push constants carry the
// camera law (rt_ao.comp).
bool RayTracing::ensureAoPipeline() {
    if ( aoPipeline != VK_NULL_HANDLE ) {
        return true;
    }
    VkDevice device = pDevice->getDevice();

    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if ( vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &aoSetLayout ) != VK_SUCCESS ) {
        return false;
    }

    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof( AoParams );
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &aoSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if ( vkCreatePipelineLayout( device, &pipelineLayoutInfo, nullptr, &aoPipelineLayout ) != VK_SUCCESS ) {
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = sizeof( kSPIRV_rt_ao_comp );
    moduleInfo.pCode = kSPIRV_rt_ao_comp;
    VkShaderModule module = VK_NULL_HANDLE;
    if ( vkCreateShaderModule( device, &moduleInfo, nullptr, &module ) != VK_SUCCESS ) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = aoPipelineLayout;
    const VkResult result =
        vkCreateComputePipelines( device, pDevice->getPipelineCache(), 1, &pipelineInfo, nullptr, &aoPipeline );
    vkDestroyShaderModule( device, module, nullptr );
    if ( result != VK_SUCCESS ) {
        OTDU_LOG_ERROR( "Ray tracing: the AO compute pipeline failed (%d)\n", static_cast<int32_t>( result ) );
        return false;
    }

    VkDescriptorPoolSize poolSizes[3] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[2].descriptorCount = 2;
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    if ( vkCreateDescriptorPool( device, &poolInfo, nullptr, &aoPool ) != VK_SUCCESS ) {
        return false;
    }
    return true;
}

bool RayTracing::dispatchAO( VkCommandBuffer commandBuffer, Tlas *pTlas, GPUTexture *pDepth, GPUTexture *pDestination,
                             const AoParams &params, uint32_t frameSlot ) {
    if ( !bReady || pTlas == nullptr || pTlas->Handle == VK_NULL_HANDLE || pDepth == nullptr ||
         pDestination == nullptr || !ensureAoPipeline() ) {
        return false;
    }
    VkDevice device = pDevice->getDevice();
    if ( aoSampler == VK_NULL_HANDLE ) {
        aoSampler = pDevice->getSampler( eSamplerFilter::SF_Linear, eSamplerAddress::SA_Clamp );
    }
    if ( aoSets[frameSlot & 1] == VK_NULL_HANDLE ) {
        const VkDescriptorSetLayout layouts[2] = { aoSetLayout, aoSetLayout };
        VkDescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = aoPool;
        allocateInfo.descriptorSetCount = 2;
        allocateInfo.pSetLayouts = layouts;
        if ( vkAllocateDescriptorSets( device, &allocateInfo, aoSets ) != VK_SUCCESS ) {
            return false;
        }
    }
    VkDescriptorSet set = aoSets[frameSlot & 1];

    VkWriteDescriptorSetAccelerationStructureKHR accelWrite = {};
    accelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelWrite.accelerationStructureCount = 1;
    accelWrite.pAccelerationStructures = &pTlas->Handle;
    VkDescriptorImageInfo depthInfo = {};
    depthInfo.sampler = aoSampler;
    depthInfo.imageView = pDepth->View;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo destinationInfo = {};
    destinationInfo.imageView = pDestination->View;
    destinationInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext = &accelWrite;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &depthInfo;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].pImageInfo = &destinationInfo;
    // Rewritten at record time of the slot: the slot's previous submission
    // has completed by then (its fence was waited in beginFrame).
    vkUpdateDescriptorSets( device, 3, writes, 0, nullptr );

    // The render passes left the depth in its read only final layout; their
    // finished depth writes must be visible to this dispatch. The mask target
    // starts the frame undefined. The barrier needs the stencil aspect too
    // when the depth has one (D32_S8 / D24_S8).
    const VkImageAspectFlags depthAspect =
        pDepth->Format == VK_FORMAT_D32_SFLOAT_S8_UINT || pDepth->Format == VK_FORMAT_D24_UNORM_S8_UINT
            ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
            : VK_IMAGE_ASPECT_DEPTH_BIT;
    VkImageMemoryBarrier barriers[2] = {};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barriers[0].image = pDepth->Image;
    barriers[0].subresourceRange = { depthAspect, 0, 1, 0, 1 };
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].image = pDestination->Image;
    barriers[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier( commandBuffer,
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers );

    vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, aoPipeline );
    vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, aoPipelineLayout, 0, 1, &set, 0, nullptr );
    vkCmdPushConstants( commandBuffer, aoPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                        static_cast<uint32_t>( sizeof( AoParams ) ), &params );
    const uint32_t groupsX = Max( ( static_cast<uint32_t>( params.Params2[0] ) + 7 ) / 8, 1u );
    const uint32_t groupsY = Max( ( static_cast<uint32_t>( params.Params2[1] ) + 7 ) / 8, 1u );
    vkCmdDispatch( commandBuffer, groupsX, groupsY, 1 );

    // The scene blit samples the mask in the main pass later in this buffer.
    VkImageMemoryBarrier destBarrier = {};
    destBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    destBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    destBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    destBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    destBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    destBarrier.image = pDestination->Image;
    destBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &destBarrier );
    return true;
}

bool RayTracing::runSelfTest() {
    if ( !bReady ) {
        OTDU_LOG_INFO( "Ray tracing self test: skipped, no ray tracing on this device\n" );
        return false;
    }
    RenderDevice &device = *pDevice;

    // One triangle in the XY plane at z = 5 (D3D space, z is forward) covering
    // x + y <= 1, placed with an identity instance transform so the expected
    // answer does not depend on the transform conversion.
    const float vertices[3][3] = { { 0.0f, 0.0f, 5.0f }, { 1.0f, 0.0f, 5.0f }, { 0.0f, 1.0f, 5.0f } };
    const uint32_t indices[3] = { 0, 1, 2 };

    GPUBuffer *pVertices = device.createBuffer( sizeof( vertices ), getBuildInputUsage(), vertices );
    GPUBuffer *pIndices = device.createBuffer( sizeof( indices ), getBuildInputUsage(), indices );
    if ( pVertices == nullptr || pIndices == nullptr ) {
        OTDU_LOG_ERROR( "Ray tracing self test: the test geometry could not be uploaded\n" );
        return false;
    }

    Blas *pBlas = createTriangleBlas( device.getBufferDeviceAddress( pVertices ), sizeof( float ) * 3,
                                      VK_FORMAT_R32G32B32_SFLOAT, 3, device.getBufferDeviceAddress( pIndices ), 3 );
    Tlas *pTlas = pBlas != nullptr ? createTlas( 1 ) : nullptr;
    bool bBuilt = false;
    if ( pTlas != nullptr ) {
        Instance instance;
        instance.Blas = pBlas->Address;
        instance.Transform = Matrix4x4f::Identity();
        bBuilt = buildTlas( pTlas, &instance, 1 );
    }
    if ( !bBuilt ) {
        OTDU_LOG_ERROR( "Ray tracing self test: the acceleration structures could not be built\n" );
        destroyTlas( pTlas );
        destroyBlas( pBlas );
        device.destroyBuffer( pVertices );
        device.destroyBuffer( pIndices );
        return false;
    }

    // Through the middle of the triangle: (0.25, 0.25, 5) satisfies x + y <= 1,
    // so the ray must reach the plane z = 5 at t = 5.
    ProbeResult hit;
    const bool bHitTraced =
        traceProbe( pTlas, Vector3f( 0.25f, 0.25f, 0.0f ), Vector3f( 0.0f, 0.0f, 1.0f ), 100.0f, hit );
    // The same ray shifted outside the triangle must miss.
    ProbeResult miss;
    const bool bMissTraced =
        traceProbe( pTlas, Vector3f( 2.0f, 2.0f, 0.0f ), Vector3f( 0.0f, 0.0f, 1.0f ), 100.0f, miss );

    const bool bHitOk = bHitTraced && hit.Hit == 1 && fabsf( hit.Distance - 5.0f ) < 0.01f;
    const bool bMissOk = bMissTraced && miss.Hit == 0;
    OTDU_LOG_INFO( "Ray tracing self test: hit ray -> hit %u, t %.3f, instance %u, primitive %u "
                   "(expected 1, 5.000, 0, 0)\n",
                   hit.Hit, hit.Distance, hit.Instance, hit.Primitive );
    OTDU_LOG_INFO( "Ray tracing self test: miss ray -> hit %u (expected 0)\n", miss.Hit );

    // The same triangle rotated 90 degrees about Z -- (x, y, z) -> (-y, x, z) --
    // so it covers x <= 0, y >= 0 instead. This is the test that proves the
    // instance transform conversion: the Eden matrices are column major and
    // VkTransformMatrixKHR is row major, and getting that transpose wrong turns
    // the rotation into its inverse, which misses this ray.
    Matrix4x4f rotation = Matrix4x4f::Identity();
    rotation.m[0][0] = 0.0f;
    rotation.m[1][0] = -1.0f;
    rotation.m[0][1] = 1.0f;
    rotation.m[1][1] = 0.0f;
    Instance rotated;
    rotated.Blas = pBlas->Address;
    rotated.Transform = rotation;
    ProbeResult rotatedHit;
    const bool bRotatedTraced =
        buildTlas( pTlas, &rotated, 1 ) &&
        traceProbe( pTlas, Vector3f( -0.25f, 0.25f, 0.0f ), Vector3f( 0.0f, 0.0f, 1.0f ), 100.0f, rotatedHit );
    const bool bRotatedOk = bRotatedTraced && rotatedHit.Hit == 1 && fabsf( rotatedHit.Distance - 5.0f ) < 0.01f;
    OTDU_LOG_INFO( "Ray tracing self test: rotated ray -> hit %u, t %.3f (expected 1, 5.000)\n", rotatedHit.Hit,
                   rotatedHit.Distance );

    const bool bPassed = bHitOk && bMissOk && bRotatedOk;
    OTDU_LOG_INFO( "Ray tracing self test: %s\n", bPassed ? "PASSED" : "FAILED" );

    destroyTlas( pTlas );
    destroyBlas( pBlas );
    device.destroyBuffer( pVertices );
    device.destroyBuffer( pIndices );
    return bPassed;
}
