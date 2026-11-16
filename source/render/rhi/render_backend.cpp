#include "shared.h"
#include "render/rhi/render_backend.h"

#include "render/d3d12/d3d12_device.h"
#include "system/cmd_line.h"

namespace {
// "-renderer=d3d12" as well as "-renderer d3d12": CmdLine only knows the two
// token form, and the "=" form is what a shell alias tends to produce.
const char *FindRendererValue() {
    constexpr const char *pFlag = "-renderer";
    const char *pValue = CmdLine::GetArgValue( pFlag );
    if ( pValue != nullptr ) {
        return pValue;
    }
    constexpr size_t flagLength = 9; // strlen( "-renderer" )
    for ( int32_t i = 1; i < gCmdLineArgCount; i++ ) {
        const char *pArg = gpCmdLineArgs[i];
        if ( pArg == nullptr ) {
            continue;
        }
        if ( StringNCmpI( pArg, pFlag, flagLength ) == 0 && pArg[flagLength] == '=' ) {
            return pArg + flagLength + 1;
        }
    }
    return nullptr;
}

// Name of the CMake option that compiles a backend in (LogResolved).
const char *GetOptionName( eRenderBackend backend ) {
    switch ( backend ) {
    case eRenderBackend::D3D12:
        return "OTDU_ENABLE_D3D12";
    case eRenderBackend::D3D11:
        return "OTDU_ENABLE_D3D11";
    case eRenderBackend::D3D9:
        return "OTDU_ENABLE_D3D9";
    default:
        return "";
    }
}
} // namespace

bool RenderBackend::IsCompiledIn( eRenderBackend backend ) {
    switch ( backend ) {
    case eRenderBackend::Vulkan:
        return true;
    case eRenderBackend::D3D12:
#if defined( OTDU_D3D12 )
        return true;
#else
        return false;
#endif
    case eRenderBackend::D3D11:
#if defined( OTDU_D3D11 )
        return true;
#else
        return false;
#endif
    case eRenderBackend::D3D9:
#if defined( OTDU_D3D9 )
        return true;
#else
        return false;
#endif
    default:
        return false;
    }
}

eRenderBackend RenderBackend::FromName( const char *pName ) {
    if ( pName == nullptr ) {
        return eRenderBackend::Invalid;
    }
    const std::string name = ToLower( pName );
    if ( name == "vulkan" || name == "vk" ) {
        return eRenderBackend::Vulkan;
    }
    if ( name == "d3d12" || name == "dx12" || name == "direct3d12" ) {
        return eRenderBackend::D3D12;
    }
    if ( name == "d3d11" || name == "dx11" || name == "direct3d11" ) {
        return eRenderBackend::D3D11;
    }
    if ( name == "d3d9" || name == "dx9" || name == "direct3d9" ) {
        return eRenderBackend::D3D9;
    }
    return eRenderBackend::Invalid;
}

const char *RenderBackend::GetName( eRenderBackend backend ) {
    switch ( backend ) {
    case eRenderBackend::Vulkan:
        return "vulkan";
    case eRenderBackend::D3D12:
        return "d3d12";
    case eRenderBackend::D3D11:
        return "d3d11";
    case eRenderBackend::D3D9:
        return "d3d9";
    default:
        return "invalid";
    }
}

bool RenderBackend::IsDirect3D( eRenderBackend backend ) {
    return backend == eRenderBackend::D3D12 || backend == eRenderBackend::D3D11 ||
           backend == eRenderBackend::D3D9;
}

bool RenderBackend::CanRenderScene( eRenderBackend backend ) {
    // The Direct3D backends get the scene once their shader path exists:
    // DXSO's SPIR-V -> HLSL (SPIRV-Cross) -> DXIL (dxc), or the original D3D9
    // bytecode straight to D3D9. docs/backends.md has the order.
    return backend == eRenderBackend::Vulkan;
}

eRenderBackend RenderBackend::Resolve() {
    const char *pRequested = FindRendererValue();
    if ( pRequested == nullptr || pRequested[0] == '\0' ) {
        const char *pEnvironment = getenv( "OTDU_RENDERER" );
        if ( pEnvironment != nullptr && pEnvironment[0] != '\0' ) {
            pRequested = pEnvironment;
        }
    }
    if ( pRequested == nullptr || pRequested[0] == '\0' ) {
        return eRenderBackend::Vulkan;
    }

    const eRenderBackend backend = FromName( pRequested );
    if ( backend == eRenderBackend::Invalid ) {
        OTDU_LOG_WARN( "Unknown renderer '%s' (vulkan, d3d12, d3d11 or d3d9); using Vulkan\n",
                       pRequested );
        return eRenderBackend::Vulkan;
    }
    if ( !IsCompiledIn( backend ) ) {
        OTDU_LOG_WARN( "Renderer '%s' is not compiled in (%s=OFF); using Vulkan\n",
                       GetName( backend ), GetOptionName( backend ) );
        return eRenderBackend::Vulkan;
    }
    return backend;
}

void RenderBackend::LogResolved( eRenderBackend backend ) {
    OTDU_LOG_INFO( "Renderer: %s - compiled in: vulkan%s%s%s\n", GetName( backend ),
                   IsCompiledIn( eRenderBackend::D3D12 ) ? ", d3d12" : "",
                   IsCompiledIn( eRenderBackend::D3D11 ) ? ", d3d11" : "",
                   IsCompiledIn( eRenderBackend::D3D9 ) ? ", d3d9" : "" );
    if ( IsDirect3D( backend ) && !CanRenderScene( backend ) ) {
        OTDU_LOG_WARN( "Renderer %s presents the swapchain only: the scene, the 2D UI and "
                       "Flash still come from the Vulkan path until the port of "
                       "docs/backends.md lands\n",
                       GetName( backend ) );
    }
}

bool RenderBackend::RunSelfTest() {
    const eRenderBackend backend = Resolve();
    LogResolved( backend );

    if ( backend == eRenderBackend::Vulkan ) {
        OTDU_LOG_ERROR( "-renderer-selftest needs a Direct3D backend: "
                        "-renderer d3d12\n" );
        return false;
    }
#if defined( OTDU_D3D12 )
    if ( backend == eRenderBackend::D3D12 ) {
        return D3D12RunSelfTest();
    }
#endif
    OTDU_LOG_ERROR( "-renderer-selftest: %s is not implemented yet "
                    "(docs/backends.md)\n",
                    GetName( backend ) );
    return false;
}
