#include "shared.h"
#include "render/d3d12/d3d12_shader.h"

#if defined( OTDU_D3D12 )

#include "render/shaders/dxso_translator.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <spirv_hlsl.hpp>

namespace {
uint32_t AlignUp( uint32_t value, uint32_t alignment ) {
    return ( value + alignment - 1 ) & ~( alignment - 1 );
}

// One register binding per SPIR-V resource, applied to every stage, so the
// root signature is a constant of the backend rather than a per-program object
// (see the header). The index is the position in the reflection list, which is
// deterministic for a given blob (the resources come back in SPIR-V id order).
void RemapResources( spirv_cross::CompilerHLSL &compiler, uint32_t &outTextures, uint32_t &outSamplers,
                     uint32_t &outConstantBuffers ) {
    outTextures = 0;
    outSamplers = 0;
    outConstantBuffers = 0;

    const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    const spirv_cross::ExecutionModel model = compiler.get_execution_model();

    // Constant buffers: b0, b1, ... (the root signature exposes b0 per stage;
    // a D3D9 shader usually has one, the "shared PS constants" block makes two).
    uint32_t cbv = 0;
    for ( const spirv_cross::Resource &resource : resources.uniform_buffers ) {
        spirv_cross::HLSLResourceBinding binding = {};
        binding.stage = model;
        binding.desc_set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
        binding.binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
        binding.cbv.register_space = 0;
        binding.cbv.register_binding = cbv;
        compiler.add_hlsl_resource_binding( binding );
        cbv++;
    }
    outConstantBuffers = cbv;

    // Textures, then samplers. A combined image sampler (a D3D9 `sampler2D`)
    // takes one texture slot and the sampler slot of the same index, so the two
    // heaps line up.
    uint32_t slot = 0;
    for ( const spirv_cross::Resource &resource : resources.sampled_images ) {
        spirv_cross::HLSLResourceBinding binding = {};
        binding.stage = model;
        binding.desc_set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
        binding.binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
        binding.srv.register_space = 0;
        binding.srv.register_binding = slot;
        binding.sampler.register_space = 0;
        binding.sampler.register_binding = slot;
        compiler.add_hlsl_resource_binding( binding );
        slot++;
    }
    for ( const spirv_cross::Resource &resource : resources.separate_images ) {
        spirv_cross::HLSLResourceBinding binding = {};
        binding.stage = model;
        binding.desc_set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
        binding.binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
        binding.srv.register_space = 0;
        binding.srv.register_binding = slot;
        compiler.add_hlsl_resource_binding( binding );
        slot++;
    }
    outTextures = slot;
    slot = 0;
    for ( const spirv_cross::Resource &resource : resources.separate_samplers ) {
        spirv_cross::HLSLResourceBinding binding = {};
        binding.stage = model;
        binding.desc_set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
        binding.binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
        binding.sampler.register_space = 0;
        binding.sampler.register_binding = slot;
        compiler.add_hlsl_resource_binding( binding );
        slot++;
    }
    outSamplers = slot;
}


// The push constants become 32-bit root constants at b13: SPIRV-Cross needs the
// range and the register spelled out, otherwise it leaves the cbuffer with no
// `register()` at all and the D3D compiler picks a number that can collide with
// b0 (the constants block).
void SetPushConstantLayout( spirv_cross::CompilerHLSL &compiler, uint32_t &outSize ) {
    outSize = 0;
    const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    if ( resources.push_constant_buffers.empty() ) {
        return;
    }
    uint32_t size = 0;
    for ( const spirv_cross::Resource &resource : resources.push_constant_buffers ) {
        const spirv_cross::SPIRType &type = compiler.get_type( resource.type_id );
        size = Max<uint32_t>( size, static_cast<uint32_t>( compiler.get_declared_struct_size( type ) ) );
    }
    size = AlignUp( size, 4 );
    std::vector<spirv_cross::RootConstants> layout;
    spirv_cross::RootConstants range = {};
    range.start = 0;
    range.end = size;
    range.binding = D3D12ShaderCompiler::kPushConstantRegister;
    range.space = 0;
    layout.push_back( range );
    compiler.set_root_constant_layouts( layout );
    outSize = size;
}

bool CompileHLSL( const std::string &hlsl,
                  eShaderType type,
                  const char *pDebugName,
                  std::vector<uint8_t> &outBytecode,
                  std::string &outError ) {
    const char *pTarget = ( type == eShaderType::ST_Vertex ) ? "vs_5_0" : "ps_5_0";
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined( _DEBUG )
    flags |= D3DCOMPILE_DEBUG;
#endif
    ID3DBlob *pShader = nullptr;
    ID3DBlob *pErrors = nullptr;
    const HRESULT result = D3DCompile( hlsl.data(), hlsl.size(), pDebugName, nullptr, nullptr,
                                       D3D12ShaderCompiler::kEntryPoint, pTarget, flags, 0, &pShader,
                                       &pErrors );
    if ( FAILED( result ) && pErrors != nullptr ) {
        // The compiler's first line names the offending construct; the whole
        // listing is only interesting when the HLSL is dumped.
        const char *pMessage = static_cast<const char *>( pErrors->GetBufferPointer() );
        outError.assign( pMessage, strnlen( pMessage, 512 ) );
        const size_t newline = outError.find( '\n' );
        if ( newline != std::string::npos ) {
            outError.resize( newline );
        }
    }
    if ( SUCCEEDED( result ) && pShader != nullptr ) {
        const uint8_t *pBytes = static_cast<const uint8_t *>( pShader->GetBufferPointer() );
        outBytecode.assign( pBytes, pBytes + pShader->GetBufferSize() );
    }
    if ( pShader != nullptr ) {
        pShader->Release();
    }
    if ( pErrors != nullptr ) {
        pErrors->Release();
    }
    if ( FAILED( result ) && outError.empty() ) {
        outError = "D3DCompile failed (0x" + std::to_string( static_cast<uint32_t>( result ) ) + ")";
    }
    return !outBytecode.empty();
}
} // namespace


const char *D3D12ShaderCompiler::vertexSemantic( uint32_t ) { return "TEXCOORD"; }

bool D3D12ShaderCompiler::compile( const TranslatedShader &shader,
                                   D3D12ShaderStage &outStage,
                                   const char *pDebugName ) {
    outStage = D3D12ShaderStage();
    outStage.Type = shader.Stage;
    if ( shader.SPIRV.empty() ) {
        outStage.Error = "no SPIR-V";
        return false;
    }

    try {
        spirv_cross::CompilerHLSL compiler( shader.SPIRV.data(), shader.SPIRV.size() );
        spirv_cross::CompilerHLSL::Options options = compiler.get_hlsl_options();
        // SM 5.0: the highest shader model D3DCompile writes, and valid input
        // for D3D12. `point_size_compat` / `point_coord_compat` are what a
        // D3D9 point sprite needs - HLSL has neither builtin.
        options.shader_model = 50;
        options.point_size_compat = true;
        options.point_coord_compat = true;
        options.support_nonzero_base_vertex_base_instance = false;
        compiler.set_hlsl_options( options );

        uint32_t pushSize = 0;
        SetPushConstantLayout( compiler, pushSize );
        RemapResources( compiler, outStage.NumTextures, outStage.NumSamplers, outStage.NumConstantBuffers );
        outStage.PushConstantsSize = pushSize;
        outStage.RootConstants = pushSize / 4;

        outStage.HLSL = compiler.compile();
    } catch ( const std::exception &e ) {
        outStage.Error = std::string( "SPIRV-Cross: " ) + e.what();
        return false;
    } catch ( ... ) {
        outStage.Error = "SPIRV-Cross: unknown failure";
        return false;
    }

    if ( outStage.NumTextures > kMaxResources || outStage.NumSamplers > kMaxResources ) {
        outStage.Error = "too many textures (" + std::to_string( outStage.NumTextures ) + ")";
        return false;
    }
    if ( outStage.RootConstants > 64 ) {
        // D3D12 allows 64 32-bit values per root constant parameter; a shader
        // that needs more is one of the software vertex shader cases and needs
        // a CBV instead (docs/backends.md section 6).
        outStage.Error = "push constants too large (" + std::to_string( outStage.PushConstantsSize ) + " bytes)";
        return false;
    }

    if ( !CompileHLSL( outStage.HLSL, shader.Stage, pDebugName, outStage.Bytecode, outStage.Error ) ) {
        return false;
    }
    outStage.bReady = true;
    return true;
}

#endif // OTDU_D3D12
