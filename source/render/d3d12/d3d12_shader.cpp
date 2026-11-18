#include "shared.h"
#include "render/d3d12/d3d12_shader.h"

#if defined( OTDU_D3D12 )

#include "render/shaders/dxso_translator.h"

#include <windows.h>
#include <d3d12.h>
#include <d3d12shader.h> // ID3D12ShaderReflection: the linkage is measured, not guessed
#include <d3dcompiler.h>

#include <spirv_hlsl.hpp>

#include <algorithm>

namespace {
uint32_t AlignUp( uint32_t value, uint32_t alignment ) {
    return ( value + alignment - 1 ) & ~( alignment - 1 );
}

// One register binding per SPIR-V resource, applied to every stage, so the
// root signature is a constant of the backend rather than a per-program object
// (see the header). The index is the position in the reflection list, which is
// deterministic for a given blob (the resources come back in SPIR-V id order).
void RemapResources( spirv_cross::CompilerHLSL &compiler, uint32_t &outTextures, uint32_t &outSamplers,
                     uint32_t &outConstantBuffers, std::vector<std::string> *pConstantBufferNames,
                     const char *pDebugName ) {
    outTextures = 0;
    outSamplers = 0;
    outConstantBuffers = 0;
    if ( pConstantBufferNames != nullptr ) {
        pConstantBufferNames->clear();
    }

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
        if ( Logger::IsVerbose() ) {
            // The original decoration matters: a binding that lands on 13 would
            // also be the key to_resource_register() looks up for the push
            // constant block, and would silently move the root constants.
            OTDU_LOG_DEBUG( "D3D12 shader %s: cbuffer '%s' set %u binding %u -> b%u\n",
                            pDebugName != nullptr ? pDebugName : "?",
                            compiler.get_name( resource.id ).c_str(), binding.desc_set,
                            binding.binding, cbv );
        }
        binding.cbv.register_space = 0;
        binding.cbv.register_binding = cbv;
        compiler.add_hlsl_resource_binding( binding );
        // The name is what tells a caller which block landed at b<N>: DXSO's
        // names are the D3D9 ones (`cbuffer_t` = the constant file,
        // `spec_state_t`, `D3D9SharedPS`, `clip_info_t`), and an Eden draw has a
        // different value for each (docs/backends.md section 3.8).
        if ( pConstantBufferNames != nullptr ) {
            pConstantBufferNames->push_back( compiler.get_name( resource.id ) );
        }
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
// b0 (the constants block). The space is not 0 for the same reason - see
// D3D12ShaderCompiler::kPushConstantSpace: space 0 + binding 13 is a key that a
// D3D9 sampler slot 13 also uses.
void SetPushConstantLayout( spirv_cross::CompilerHLSL &compiler, uint32_t &outSize,
                            const char *pDebugName ) {
    outSize = 0;
    const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    if ( Logger::IsVerbose() ) {
        OTDU_LOG_DEBUG( "D3D12 shader %s: %zu push constant block(s)\n",
                        pDebugName != nullptr ? pDebugName : "?",
                        resources.push_constant_buffers.size() );
    }
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
    range.space = D3D12ShaderCompiler::kPushConstantSpace;
    layout.push_back( range );
    compiler.set_root_constant_layouts( layout );
    outSize = size;
}

bool CompileHLSL( const std::string &hlsl,
                  eShaderType type,
                  const char *pDebugName,
                  std::vector<uint8_t> &outBytecode,
                  std::string &outError ) {
    // 5.1 targets: SPIRV-Cross writes `NonUniformResourceIndex` (see
    // D3D12ShaderCompiler::compile), which only exists at shader model 5.1.
    // d3dcompiler_47 has both, and D3D12 accepts 5.1 DXBC.
    const char *pTarget = ( type == eShaderType::ST_Vertex ) ? "vs_5_1" : "ps_5_1";
    // A D3D9 shader selects its sampler with a register operand, so DXSO hands
    // SPIRV-Cross a dynamically indexed sampler array; the SM 5.1 HLSL declares
    // those tables unbounded, and FXC refuses that by default (error X3596,
    // "unbounded size descriptor tables are disabled to support frame captures").
    // The switch is the documented answer; D3D12 itself allows an unbounded SRV
    // range (D3D12Scene's table is the last root parameter for exactly that).
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 |
                 D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
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

// ---------------------------------------------------------------------------
// Vertex/pixel linkage (docs/backends.md section 3.9)
//
// D3D12 compares the two stages by the *slot* a varying occupies - the k-th
// non-system-value output of the vertex stage against the k-th input of the
// pixel stage - not by the semantic index. HLSL hands out those slots densely,
// in the order the struct declares them, so a stage that skips a varying
// register (D3D9 allows oT0..oT4 plus oT7) shifts everything after the gap.
// The signatures below come from the compiled DXBC through D3DReflect, so the
// slots are what the device will see, not what the SPIR-V said.
// ---------------------------------------------------------------------------

// The reflected inputs (bOutputs false) or outputs (true) of a stage, system
// values dropped and ordered by generic slot.
bool ReflectVaryings( const std::vector<uint8_t> &bytecode, bool bOutputs,
                      std::vector<ShaderVarying> &outVaryings ) {
    outVaryings.clear();
    ID3D12ShaderReflection *pReflection = nullptr;
    if ( bytecode.empty() ||
         FAILED( D3DReflect( bytecode.data(), bytecode.size(), IID_PPV_ARGS( &pReflection ) ) ) ||
         pReflection == nullptr ) {
        return false;
    }
    D3D12_SHADER_DESC desc = {};
    if ( FAILED( pReflection->GetDesc( &desc ) ) ) {
        pReflection->Release();
        return false;
    }
    const UINT count = bOutputs ? desc.OutputParameters : desc.InputParameters;
    for ( UINT i = 0; i < count; i++ ) {
        D3D12_SIGNATURE_PARAMETER_DESC parameter = {};
        const HRESULT result = bOutputs ? pReflection->GetOutputParameterDesc( i, &parameter )
                                        : pReflection->GetInputParameterDesc( i, &parameter );
        if ( FAILED( result ) || parameter.SystemValueType != D3D_NAME_UNDEFINED ) {
            continue;
        }
        ShaderVarying varying;
        varying.Name = parameter.SemanticName != nullptr ? parameter.SemanticName : "";
        varying.Index = parameter.SemanticIndex;
        outVaryings.push_back( std::move( varying ) );
    }
    pReflection->Release();
    std::sort( outVaryings.begin(), outVaryings.end(),
               []( const ShaderVarying &a, const ShaderVarying &b ) { return a.Index < b.Index; } );
    for ( uint32_t i = 0; i < outVaryings.size(); i++ ) {
        outVaryings[i].Slot = i;
    }
    return true;
}

bool VaryingsLineUp( const std::vector<ShaderVarying> &vsVaryings,
                     const std::vector<ShaderVarying> &psVaryings,
                     uint32_t &outMismatchSlot ) {
    outMismatchSlot = 0;
    // Equal length, not "the pixel stage may stop early". D3D12 compares the
    // *hardware register* of every parameter, and a system value takes the
    // register after the generics (the measured FXC fact below). A pixel stage
    // that reads 9 of the 11 varyings a vertex stage writes therefore puts its
    // SV_Position in register 9 against the vertex stage's 11, and the pipeline
    // is refused with "Semantic 'SV_Position' is defined for mismatched
    // hardware registers" although every generic semantic matches. Found by
    // -d3d12-pipetest (docs/backends.md section 3.10): 32 of TD_Accessories2's
    // 64 pairs and all 16 of TD_Avatar's were refused for exactly this.
    if ( psVaryings.size() != vsVaryings.size() ) {
        outMismatchSlot = static_cast<uint32_t>( psVaryings.size() < vsVaryings.size() ? psVaryings.size()
                                                                                       : vsVaryings.size() );
        return false;
    }
    for ( uint32_t i = 0; i < psVaryings.size(); i++ ) {
        if ( psVaryings[i].Name != vsVaryings[i].Name || psVaryings[i].Index != vsVaryings[i].Index ) {
            outMismatchSlot = i;
            return false;
        }
    }
    return true;
}

std::string DescribeVaryings( const std::vector<ShaderVarying> &varyings ) {
    std::string text = "[";
    for ( const ShaderVarying &varying : varyings ) {
        if ( text.size() > 1 ) {
            text += ' ';
        }
        text += varying.Name + std::to_string( varying.Index ) + "@" + std::to_string( varying.Slot );
    }
    return text + "]";
}
struct InterfaceField {
    std::string Line;
    std::string Semantic;
    uint32_t Index = 0;
    bool bVarying = false;
};

// One `float4 name : TEXCOORD0;` line of SPIRV-Cross's input struct.
bool ParseFieldLine( const std::string &line, InterfaceField &outField ) {
    const size_t colon = line.find( ':' );
    const size_t semicolon = line.find( ';', colon == std::string::npos ? 0 : colon );
    if ( colon == std::string::npos || semicolon == std::string::npos || semicolon <= colon + 1 ) {
        return false;
    }
    size_t begin = colon + 1;
    while ( begin < semicolon && ( line[begin] == ' ' || line[begin] == '\t' ) ) {
        begin++;
    }
    size_t end = begin;
    while ( end < semicolon &&
            ( std::isalnum( static_cast<unsigned char>( line[end] ) ) || line[end] == '_' ) ) {
        end++;
    }
    if ( end == begin ) {
        return false;
    }
    size_t digits = begin;
    while ( digits < end && !std::isdigit( static_cast<unsigned char>( line[digits] ) ) ) {
        digits++;
    }
    outField.Line = line;
    outField.Semantic = line.substr( begin, digits - begin );
    outField.Index =
        digits < end ? static_cast<uint32_t>( std::strtoul( line.c_str() + digits, nullptr, 10 ) ) : 0;
    // Only TEXCOORD is a generic varying in this backend: SPIRV-Cross writes
    // `TEXCOORD<location>` for every stage output and for every pixel input
    // (docs/backends.md section 4), everything else is a system value.
    outField.bVarying = outField.Semantic == "TEXCOORD";
    return true;
}
// Rebuilds the pixel stage's input struct so its k-th varying is the vertex
// stage's k-th output. A slot the pixel stage does not read becomes an unused
// `float4` pad with the vertex stage's semantic: FXC keeps it (measured - the
// entry stays in the input signature with an empty `Used` mask), and float4 is
// the only safe width because FXC packs two narrower unused pads into one
// register (measured: `float` TEXCOORD3 and `float` TEXCOORD4 both landed on
// register 3, x and y). Varyings are emitted before the system values for the
// same reason: a system value declared first occupies the register a varying
// would have taken (measured: SV_Position then TEXCOORD0 -> registers 0 and 1).
bool PadPixelInputs( const std::vector<ShaderVarying> &vsVaryings,
                     const std::vector<ShaderVarying> &psVaryings,
                     const std::string &hlsl,
                     std::string &outHlsl,
                     uint32_t &outPads,
                     uint32_t &outUnplaced ) {
    outPads = 0;
    outUnplaced = 0;
    const size_t structStart = hlsl.find( "struct SPIRV_Cross_Input" );
    if ( structStart == std::string::npos ) {
        return false;
    }
    const size_t braceOpen = hlsl.find( '{', structStart );
    const size_t braceClose = hlsl.find( "};", braceOpen );
    if ( braceOpen == std::string::npos || braceClose == std::string::npos ) {
        return false;
    }

    const std::string body = hlsl.substr( braceOpen + 1, braceClose - braceOpen - 1 );
    const std::string newline = body.find( "\r\n" ) != std::string::npos ? "\r\n" : "\n";
    std::vector<InterfaceField> varyings;
    std::vector<std::string> systemValues;
    size_t cursor = 0;
    while ( cursor <= body.size() ) {
        const size_t lineEnd = body.find( '\n', cursor );
        std::string line =
            body.substr( cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor );
        if ( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        if ( line.find_first_not_of( " \t" ) != std::string::npos ) {
            InterfaceField field;
            if ( !ParseFieldLine( line, field ) ) {
                return false;
            }
            if ( field.bVarying ) {
                varyings.push_back( std::move( field ) );
            } else {
                systemValues.push_back( line );
            }
        }
        if ( lineEnd == std::string::npos ) {
            break;
        }
        cursor = lineEnd + 1;
    }
    if ( varyings.size() != psVaryings.size() ) {
        // The struct and the signature disagree - do not guess.
        return false;
    }

    std::string rebuilt = newline;
    uint32_t next = 0;
    for ( const ShaderVarying &vsVarying : vsVaryings ) {
        if ( next < psVaryings.size() && psVaryings[next].Name == vsVarying.Name &&
             psVaryings[next].Index == vsVarying.Index ) {
            rebuilt += varyings[next].Line + newline;
            next++;
            continue;
        }
        rebuilt += "    float4 pad" + std::to_string( vsVarying.Index ) + " : " + vsVarying.Name +
                   std::to_string( vsVarying.Index ) + ";" + newline;
        outPads++;
    }
    // Anything the vertex stage does not output cannot be placed; keep it so
    // the compile still names it (the pipeline will be refused, with the slot).
    for ( ; next < varyings.size(); next++ ) {
        rebuilt += varyings[next].Line + newline;
        outUnplaced++;
    }
    for ( const std::string &line : systemValues ) {
        rebuilt += line + newline;
    }

    outHlsl = hlsl.substr( 0, braceOpen + 1 ) + rebuilt + hlsl.substr( braceClose );
    return true;
}

// Makes the two stages line up; returns false (with outError) when that cannot
// be done, so the caller can name the reason instead of the device's
// E_INVALIDARG.
bool LinkVaryings( D3D12ShaderStage &vs, D3D12ShaderStage &ps, const char *pDebugName,
                   std::string &outError ) {
    uint32_t mismatch = 0;
    if ( VaryingsLineUp( vs.Varyings, ps.Varyings, mismatch ) ) {
        return true;
    }
    if ( vs.Varyings.empty() ) {
        // A vertex stage that writes only system values has no register for the
        // pixel stage to read: unlinkable unless the pixel stage reads nothing
        // either (that case lines up above, without padding).
        outError = "the vertex stage reflects no varying, pixel inputs " + DescribeVaryings( ps.Varyings );
        return false;
    }
    // A pixel stage that reads *no* varying is normal - TD_Bones' 48 pairs are
    // exactly that, a flat colour shader - and it still needs the pads: its
    // SV_Position has to land on the register the vertex stage's SV_Position
    // occupies, which is the one after the vertex stage's generics.

    std::string padded;
    uint32_t pads = 0;
    uint32_t unplaced = 0;
    if ( ps.HLSL.find( "struct SPIRV_Cross_Input" ) == std::string::npos ) {
        // The pixel stage takes no parameter at all - not even SV_Position - so
        // there is no register of its own to align: extra vertex outputs are
        // harmless and the pair links as it is.
        return true;
    }
    if ( !PadPixelInputs( vs.Varyings, ps.Varyings, ps.HLSL, padded, pads, unplaced ) ) {
        outError = "pixel inputs " + DescribeVaryings( ps.Varyings ) + " do not line up with vertex outputs " +
                   DescribeVaryings( vs.Varyings ) + " (the input struct could not be rewritten)";
        return false;
    }

    std::vector<uint8_t> bytecode;
    std::string error;
    if ( !CompileHLSL( padded, eShaderType::ST_Pixel, pDebugName, bytecode, error ) ) {
        outError = "the padded pixel stage does not compile: " + error;
        return false;
    }
    ps.HLSL = padded;
    ps.Bytecode = bytecode;
    ps.Error.clear();
    ps.Varyings.clear();
    ReflectVaryings( ps.Bytecode, false, ps.Varyings );

    if ( unplaced > 0 || !VaryingsLineUp( vs.Varyings, ps.Varyings, mismatch ) ) {
        outError = "pixel inputs " + DescribeVaryings( ps.Varyings ) + " do not line up with vertex outputs " +
                   DescribeVaryings( vs.Varyings ) + " (" + std::to_string( unplaced ) +
                   " input(s) the vertex stage does not output)";
        return false;
    }

    OTDU_LOG_INFO( "D3D12 shader %s: pixel stage relinked to %u slot(s) (%u pad): vs %s, ps %s\n",
                   pDebugName != nullptr ? pDebugName : "?", static_cast<uint32_t>( vs.Varyings.size() ),
                   pads, DescribeVaryings( vs.Varyings ).c_str(), DescribeVaryings( ps.Varyings ).c_str() );
    return true;
}
} // namespace


const char *D3D12ShaderCompiler::vertexSemantic( uint32_t ) { return "TEXCOORD"; }

std::string D3D12ShaderCompiler::describeVaryings( const D3D12ShaderStage &vs,
                                                   const D3D12ShaderStage &ps ) {
    return "vs outputs " + DescribeVaryings( vs.Varyings ) + ", ps inputs " +
           DescribeVaryings( ps.Varyings );
}

bool D3D12ShaderCompiler::compileHlsl( const char *pHlsl,
                                       eShaderType type,
                                       D3D12ShaderStage &outStage,
                                       const char *pDebugName ) {
    outStage = D3D12ShaderStage();
    outStage.Type = type;
    if ( pHlsl == nullptr || pHlsl[0] == '\0' ) {
        outStage.Error = "no HLSL";
        return false;
    }
    outStage.HLSL = pHlsl;
    if ( !CompileHLSL( outStage.HLSL, type, pDebugName, outStage.Bytecode, outStage.Error ) ) {
        return false;
    }
    outStage.bReady = true;
    ReflectVaryings( outStage.Bytecode, type == eShaderType::ST_Vertex, outStage.Varyings );
    return true;
}

bool D3D12ShaderCompiler::compilePair( const TranslatedShader &vertexShader,
                                       const TranslatedShader &pixelShader,
                                       D3D12ShaderStage &outVS,
                                       D3D12ShaderStage &outPS,
                                       const char *pDebugName ) {
    if ( !compile( vertexShader, outVS, pDebugName ) ) {
        return false;
    }
    if ( !compile( pixelShader, outPS, pDebugName ) ) {
        return false;
    }
    std::string error;
    if ( !LinkVaryings( outVS, outPS, pDebugName, error ) ) {
        // The pair cannot be drawn; the caller logs this instead of the
        // device's E_INVALIDARG (which it now also prints - section 6).
        outPS.Error = error;
        return false;
    }
    return true;
}

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
        // SM 5.1, not 5.0: DXSO emits `NonUniformResourceIndex` for a D3D9
        // shader that indexes its sampler array dynamically (`texld r0, t0, r1`
        // where r1 is a sampler register), and SPIRV-Cross refuses that below 5.1
        // ("Shader model 5.1 or higher is required ..."). 5.1 DXBC is valid
        // D3D12 input as well - D3D12's floor is feature level 11_0 / SM 5.1 - so
        // this costs nothing but widens what compiles. `point_size_compat` /
        // `point_coord_compat` are what a D3D9 point sprite needs - HLSL has
        // neither builtin.
        options.shader_model = 51;
        options.point_size_compat = true;
        options.point_coord_compat = true;
        options.support_nonzero_base_vertex_base_instance = false;
        compiler.set_hlsl_options( options );

        uint32_t pushSize = 0;
        SetPushConstantLayout( compiler, pushSize, pDebugName );
        RemapResources( compiler, outStage.NumTextures, outStage.NumSamplers,
                        outStage.NumConstantBuffers, &outStage.ConstantBufferNames, pDebugName );
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
    // The interface as the DXBC declares it; compilePair compares the two
    // stages with it (section 3.9).
    ReflectVaryings( outStage.Bytecode, shader.Stage == eShaderType::ST_Vertex, outStage.Varyings );
    if ( Logger::IsVerbose() ) {
        // -verbose: one line per stage, so a shader whose constants land in the
        // wrong place (or that asks for more than the root signature has) is
        // visible without dumping the HLSL.
        OTDU_LOG_DEBUG( "D3D12 shader %s: %u B push constants at b%u (%u root constant(s)), %u "
                        "cbuffer(s), %u texture(s), %u sampler(s), %zu B DXBC\n",
                        pDebugName != nullptr ? pDebugName : "?", outStage.PushConstantsSize,
                        kPushConstantRegister, outStage.RootConstants, outStage.NumConstantBuffers,
                        outStage.NumTextures, outStage.NumSamplers, outStage.Bytecode.size() );
    }
    return true;
}

#endif // OTDU_D3D12
