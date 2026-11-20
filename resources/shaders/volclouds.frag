#version 450
// OpenTDU2 port-native volumetric clouds, march pass [INF: the retail game
// draws cloud billboards and a SKYCLO dome, no volumetric clouds; this is an
// addition].
//
// Half-res raymarch of the cloud slab (EdenClouds::marchAndAccumulate). The
// density is a CPU generated, tileable Perlin-Worley 3D texture (uBase) carved
// by a coverage threshold and a height profile, eroded by a tileable Worley
// detail texture (uDetail). Lit by a 6-tap sun march with a two-octave
// multi-scatter fit (Schneider / Hillaire), a dual-lobe Henyey-Greenstein
// phase and a vertical sky gradient.
//
// TDUSC port (docs/re/tdusc_weather_physics.md): a second, higher cloud layer
// above the main deck (the TimeCycleElementSky template keeps
// Coverage##CloudCoverageSecondLayer and its densities at defaults, so the
// second slab is derived from the pushed main deck: +800 m above its top,
// sparser threshold, flatter noise) and the forward Henyey-Greenstein lobe at
// TDUSC's Mie Anisotropy 0.8. The template's Rainy Cloud coupling is the
// darkness scalar pulling the second layer into the storm deck. The two
// opt-outs ride in through uLayerParams (the push-constant block is at the
// 128-byte cap): a 1x1 RGBA32F texture filled once by EdenClouds::initialize.
//
// Output: premultiplied colour; the alpha channel carries
// packHalf2x16(alpha, hitDistance / kMaxDistance) for the accumulate pass
// (volclouds_accum.frag, temporal reprojection that removes the ray jitter)
// and the composite pass (volclouds_composite.frag, which turns the hit
// distance into a fragment depth so terrain and clouds occlude each other).
layout( set = 0, binding = 0 ) uniform sampler3D uBase;
layout( set = 0, binding = 1 ) uniform sampler3D uDetail;
layout( set = 0, binding = 2 ) uniform sampler2D uLayerParams;
layout( push_constant ) uniform PushConstants {
    vec4 CamPos;   // xyz camera (world), w time (s)
    vec4 Right;    // xyz camera right * tan(fov / 2) * aspect, w coverage 0..1
    vec4 Up;       // xyz camera up * tan(fov / 2), w cloud base (m)
    vec4 Fwd;      // xyz camera forward, w cloud top (m)
    vec4 ToSun;    // xyz unit vector towards the sun, w darkness 0..1
    vec4 SunColor; // rgb sun light, w wind x (m/s)
    vec4 Ambient;  // rgb sky ambient, w wind z (m/s)
    vec4 Fog;      // rgb fog colour, w coverage threshold
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

const float kMaxDistance = 60000.0; // shared with the accumulate / composite passes
const float kShapeScale = 2100.0;   // metres per base-noise tile (cumulus scale)
const float kDetailScale = 210.0;   // metres per detail-noise tile
const float kMarchLength = 26000.0; // metres marched inside the slab
// The second layer (TDUSC-derived, see the header comment): a slab above the
// main deck, sampled at twice the tile scale (flatter, wider cells) and with a
// threshold offset so it stays sparser than the deck below.
const float kLayer2Above = 800.0;    // metres above the main deck's top
const float kLayer2Thick = 1300.0;   // slab thickness
const float kLayer2March = 16000.0;  // march cap inside the second slab
const float kLayer2Thresh = 0.10;    // threshold offset over the main deck's
const float kLayer2Shape = 4200.0;   // metres per base-noise tile

float heightFraction( float y ) {
    return clamp( ( y - pc.Up.w ) / max( pc.Fwd.w - pc.Up.w, 1.0 ), 0.0, 1.0 );
}

// Ray / slab intersection from any camera height (above the deck too).
vec2 slabRange( float base, float top, vec3 dir ) {
    if ( abs( dir.y ) > 1e-4 ) {
        const float t0 = ( base - pc.CamPos.y ) / dir.y;
        const float t1 = ( top - pc.CamPos.y ) / dir.y;
        return vec2( max( min( t0, t1 ), 0.0 ), max( t0, t1 ) );
    }
    if ( pc.CamPos.y >= base && pc.CamPos.y <= top ) {
        return vec2( 0.0, kMaxDistance );
    }
    return vec2( 0.0, -1.0 );
}

float density( vec3 p, bool bDetail ) {
    const float h = heightFraction( p.y );
    const vec3 q = ( p + vec3( pc.SunColor.w, 0.0, pc.Ambient.w ) ) / kShapeScale;
    // Low-frequency maps (the same noise volume, a large-scale slice): where
    // the clouds cluster (cmap) and how tall each one grows (tmap). A flat
    // base with a per-cloud top is what makes a fair-weather cumulus field
    // read as separate puffy clouds with blue sky between, instead of one lid.
    const float cmap = texture( uBase, vec3( q.xz * 0.23, 0.37 ) ).r;
    const float tmap = texture( uBase, vec3( q.zx * 0.41 + 0.5, 0.81 ) ).r;
    const float topH = mix( 0.30, 1.0, smoothstep( 0.15, 0.85, tmap ) );
    // Rounded bases; each cloud's own top rounds off below the slab top.
    const float profile = smoothstep( 0.0, 0.08, h ) * ( 1.0 - smoothstep( topH * 0.55, topH, h ) );
    float d = texture( uBase, q ).r;
    // Clusters: the cluster map is sharpened so the deck splits into distinct
    // cloudy regions with clean gaps between, and it is applied
    // asymmetrically: below its mid it carves hard (stray single puffs in the
    // gaps die - v7's uniform confetti), above it only lifts a little, so the
    // clouds inside a region stay individuals with blue between instead of
    // merging into v7's gathered sheets. Storms (high coverage) blend the
    // lift back up so an overcast (kind R) still forms one deck.
    const float cs = smoothstep( 0.20, 0.80, cmap );
    const float cluster = ( cs - 0.5 ) * mix( 0.80, 0.35, pc.Right.w );
    d += cluster < 0.0 ? cluster * 1.30 : cluster * mix( 0.40, 1.0, pc.Right.w );
    // Coverage: the threshold is a measured quantile of the base-noise field
    // (EdenClouds::thresholdForCoverage, pushed in pc.Fog.w).
    const float threshold = pc.Fog.w;
    d = clamp( ( d - threshold ) / mix( 0.16, 0.22, pc.Right.w ), 0.0, 1.0 ) * profile;
    // Convection-cell gate: a 2D slice of the detail texture's freq-2
    // inverted Worley at a 4200 m tile -> ~2.1 km cells, dense at a cell
    // centre, zero towards the borders. This is what keeps the deck in
    // separate cumulus instead of gathered sheets: the base Perlin-Worley
    // field is shaped like ridges between its Worley cells (the
    // GenerateBaseVolume remap dips at the cell centres), so left alone its
    // highs always connect. One cloud per cell now, positions and interior
    // shape still from the base noise and the cluster map. Fades out for
    // storms - an overcast deck is not cellular.
    const float cell = texture( uDetail, vec3( q.xz * ( kShapeScale / kDetailScale ) * 0.05, 0.37 ) ).r;
    const float gate = smoothstep( 0.30, 0.62, cell );
    const float storm = smoothstep( 0.55, 0.85, pc.Right.w );
    d *= mix( gate, 1.0, storm );
    if ( bDetail && d > 0.0 ) {
        const vec4 w4 = texture( uDetail, q * ( kShapeScale / kDetailScale ) );
        const float w = dot( w4, vec4( 0.50, 0.28, 0.14, 0.08 ) );
        // Erode the edges hard: cauliflower billows, wispy at the base.
        d = clamp( d - ( 1.0 - w ) * 0.42 * mix( 1.0, 0.55, h ), 0.0, 1.0 );
    }
    return d * mix( 1.0, 1.6, pc.ToSun.w );
}

// The second layer: a thin broken deck floating above the main one. Same
// noise pair, twice the tile scale, no convection cells and only light
// erosion (the high deck reads as flattened altocumulus, not cumulus). The
// darkness scalar (the template's Rainy Cloud coupling) grows it into the
// storm overcast.
float densityLayer2( vec3 p, bool bDetail ) {
    const float base2 = pc.Fwd.w + kLayer2Above;
    const float top2 = base2 + kLayer2Thick;
    const float h = clamp( ( p.y - base2 ) / ( top2 - base2 ), 0.0, 1.0 );
    const vec3 q = ( p + vec3( pc.SunColor.w, 0.0, pc.Ambient.w ) ) / kLayer2Shape;
    const float cmap = texture( uBase, vec3( q.xz * 0.23, 0.37 ) ).r;
    // Broken deck: soft rounded band, denser in the middle of the slab.
    const float profile = smoothstep( 0.0, 0.30, h ) * ( 1.0 - smoothstep( 0.55, 1.0, h ) );
    float d = texture( uBase, q ).r;
    const float cluster = ( smoothstep( 0.25, 0.85, cmap ) - 0.5 ) * 0.5;
    d += cluster;
    const float threshold2 = min( pc.Fog.w + kLayer2Thresh, 0.95 );
    d = clamp( ( d - threshold2 ) / 0.18, 0.0, 1.0 ) * profile;
    if ( bDetail && d > 0.0 ) {
        const vec4 w4 = texture( uDetail, q * ( kLayer2Shape / kDetailScale ) );
        const float w = dot( w4, vec4( 0.50, 0.28, 0.14, 0.08 ) );
        d = clamp( d - ( 1.0 - w ) * 0.24, 0.0, 1.0 );
    }
    return d * texture( uLayerParams, vec2( 0.5 ) ).r * mix( 1.0, 1.4, pc.ToSun.w );
}

// Optical depth towards the sun: six taps with a growing step, ~1.4 km reach.
// The detail erosion only runs on the first taps (cost). A tap above the
// second slab samples the second layer's density.
float lightMarch( vec3 p ) {
    float sum = 0.0;
    float step = 28.0;
    for ( int s = 0; s < 6; s++ ) {
        const vec3 q = p + pc.ToSun.xyz * step;
        const float d = q.y >= pc.Fwd.w + kLayer2Above ? densityLayer2( q, s < 2 ) : density( q, s < 2 );
        sum += d * step;
        step *= 1.9;
    }
    return sum;
}

float henyeyGreenstein( float cosAngle, float g ) {
    const float g2 = g * g;
    return ( 1.0 - g2 ) / ( 12.566 * pow( 1.0 + g2 - 2.0 * g * cosAngle, 1.5 ) );
}

float interleavedGradient( vec2 p, float seed ) {
    return fract( 52.9829189 * fract( dot( p + seed * vec2( 5.41, 17.113 ), vec2( 0.06711056, 0.00583715 ) ) ) );
}

// One slab march, shared by the two layers. The per-sample integration is the
// v8 body: two multi-scatter octaves (Schneider's fit), powder darkening,
// height-shaped ambient. transmittance / light / hitT carry across the two
// segments so the layers compose as one medium.
void marchSegment( vec3 dir, float tNear, float tFar, int steps, float seed, bool bLayer2,
                   inout float transmittance, inout vec3 light, inout float hitT ) {
    const float stepLength = ( tFar - tNear ) / float( steps );
    const float jitter = interleavedGradient( gl_FragCoord.xy, seed );
    float t = tNear + stepLength * jitter;
    const float cosSun = dot( dir, pc.ToSun.xyz );
    // Dual-lobe HG with the forward lobe at TDUSC's Mie Anisotropy 0.8.
    const float phase = mix( henyeyGreenstein( cosSun, 0.80 ), henyeyGreenstein( cosSun, -0.20 ), 0.35 ) * 12.566;
    const float sigma = mix( 0.014, 0.036, pc.ToSun.w ); // extinction per metre of density
    for ( int i = 0; i < steps; i++ ) {
        const vec3 p = pc.CamPos.xyz + dir * t;
        const float d = bLayer2 ? densityLayer2( p, true ) : density( p, true );
        if ( d > 0.004 ) {
            // Two multi-scatter octaves: the first is the single scatter with
            // a powder darkening, the second fakes the light that bounces
            // into the cloud interior (Schneider's fit).
            const float lightDepth = lightMarch( p );
            const float powder = 1.0 - exp( -d * 6.0 );
            // Three multi-scatter octaves (Schneider / Hillaire): each one is
            // less extinct than the last. The floor keeps a thick deck from
            // going black - the single-scatter exp() alone is ~0 for any real
            // cloud depth and the whole layer read as flat grey.
            vec3 sunLight = vec3( 0.0 );
            float scatter = 1.0;
            for ( int o = 0; o < 3; o++ ) {
                const float octave = exp( -lightDepth * sigma / float( o + 1 ) );
                sunLight += pc.SunColor.rgb * max( octave, 0.10 ) * scatter;
                scatter *= 0.55;
            }
            sunLight *= 0.42 /* HDR sun: unscaled the lit tops clip to flat white */ * min( phase, 1.3 ) * mix( 1.0, powder, 0.35 );
            const float h = bLayer2 ? clamp( ( p.y - ( pc.Fwd.w + kLayer2Above ) ) / kLayer2Thick, 0.0, 1.0 )
                                    : heightFraction( p.y );
            const vec3 ambient = pc.Ambient.rgb * mix( 0.80, 1.35, h ) * mix( 1.0, 0.45, pc.ToSun.w );
            const float stepTransmittance = exp( -d * stepLength * sigma );
            // Energy of the step, integrated over its length (Hillaire's analytic form).
            const vec3 scattered = ( sunLight + ambient ) * ( 1.0 - stepTransmittance );
            light += transmittance * scattered;
            transmittance *= stepTransmittance;
            if ( transmittance >= 0.55 ) {
                hitT = t + stepLength * 0.5; // the front surface: the last sample still mostly transparent
            }
            if ( transmittance < 0.02 ) {
                break;
            }
        }
        t += stepLength;
    }
}

void main() {
    // The texcoords are D3D style (0,0 top left): the vertical NDC of the ray
    // must flip, or the sky renders mirrored below the horizon (attempt 1's
    // "cloud band" was this bug seen edge-on).
    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
    const vec3 dir = normalize( pc.Fwd.xyz + pc.Right.xyz * ndc.x + pc.Up.xyz * ndc.y );
    const float amp2 = texture( uLayerParams, vec2( 0.5 ) ).r;
    // Both slabs, then march them front to back (the second layer sits above
    // the deck, but a camera above it sees it first).
    vec2 range1 = slabRange( pc.Up.w, pc.Fwd.w, dir );
    const float base2 = pc.Fwd.w + kLayer2Above;
    vec2 range2 = amp2 > 0.0 ? slabRange( base2, base2 + kLayer2Thick, dir ) : vec2( 0.0, -1.0 );
    bool bSeg1 = range1.y > range1.x && range1.x < kMaxDistance;
    bool bSeg2 = range2.y > range2.x && range2.x < kMaxDistance;
    if ( !bSeg1 && !bSeg2 ) {
        outColor = vec4( 0.0 );
        return;
    }
    if ( bSeg1 ) {
        range1.y = min( range1.y, range1.x + kMarchLength );
    }
    if ( bSeg2 ) {
        range2.y = min( range2.y, range2.x + kLayer2March );
    }
    float transmittance = 1.0;
    vec3 light = vec3( 0.0 );
    float hitT = min( bSeg1 ? range1.x : 1e9, bSeg2 ? range2.x : 1e9 );
    // Second layer first when the camera (or the ray) reaches it sooner.
    const float seed = fract( pc.CamPos.w * 0.618034 );
    if ( bSeg2 && ( !bSeg1 || range2.x < range1.x ) ) {
        marchSegment( dir, range2.x, range2.y, 16, seed + 0.31, true, transmittance, light, hitT );
        if ( bSeg1 ) {
            marchSegment( dir, range1.x, range1.y, 56, seed, false, transmittance, light, hitT );
        }
    } else {
        marchSegment( dir, range1.x, range1.y, 56, seed, false, transmittance, light, hitT );
        if ( bSeg2 ) {
            marchSegment( dir, range2.x, range2.y, 16, seed + 0.31, true, transmittance, light, hitT );
        }
    }
    float alpha = 1.0 - transmittance;
    // Towards the horizon the layer dissolves into the fog colour, so it
    // never ends as a hard line (the composite's depth test keeps it in
    // front of / behind terrain correctly). The nearest slab entry drives it.
    const float tNear = min( bSeg1 ? range1.x : 1e9, bSeg2 ? range2.x : 1e9 );
    const float fade = clamp( ( tNear - 9000.0 ) / 26000.0, 0.0, 1.0 );
    // Rays grazing the slab (the deck seen edge-on) sample the noise field at
    // sub-pixel scale: fade only the truly edge-on ones (the old 0.10 window
    // erased the whole horizon band, which is where a 1-2 km deck appears).
    alpha *= smoothstep( 0.0, 0.03, abs( dir.y ) );
    light = mix( light, pc.Fog.rgb * alpha, fade );
    alpha *= 1.0 - fade * fade * 0.9;
    light *= 1.0 - fade * fade * 0.9;
    const float hitN = clamp( hitT / kMaxDistance, 0.0, 1.0 );
    outColor = vec4( light, uintBitsToFloat( packHalf2x16( vec2( alpha, hitN ) ) ) );
}
