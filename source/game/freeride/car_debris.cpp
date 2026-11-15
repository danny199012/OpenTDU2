#include "shared.h"
#include "car_debris.h"

#include "core/logger.h"
#include "render/3dd.h"

#include <cmath>
#include <random>

namespace {
constexpr size_t kMaxPieces = 40;
constexpr float kGravity = 9.81f;
constexpr float kRemoveDistance = 450.0f;

std::mt19937 &Rng() {
    static std::mt19937 rng( 0x5eed1234u );
    return rng;
}

float Rand( float lo, float hi ) {
    return std::uniform_real_distribution<float>( lo, hi )( Rng() );
}

// Gram-Schmidt on the rotation columns (the integration drifts).
void Orthonormalize( Matrix4x4f &r ) {
    Vector3f x( r.m[0][0], r.m[0][1], r.m[0][2] );
    Vector3f y( r.m[1][0], r.m[1][1], r.m[1][2] );
    x = x.normalized();
    y = ( y - x * x.dot( y ) ).normalized();
    const Vector3f z = x.cross( y );
    r.m[0][0] = x.x; r.m[0][1] = x.y; r.m[0][2] = x.z;
    r.m[1][0] = y.x; r.m[1][1] = y.y; r.m[1][2] = y.z;
    r.m[2][0] = z.x; r.m[2][1] = z.y; r.m[2][2] = z.z;
}
} // namespace

void CarDebris::spawn( EdenModel &model, const CarPartRig::Detached &part, const Matrix4x4f &vehicleWorld,
                       const Vector3f &vehicleVelocity ) {
    if ( pieces.size() >= kMaxPieces ) {
        pieces.erase( pieces.begin() );
    }
    Piece piece;
    piece.Model = &model;
    piece.Nodes = part.Nodes;
    piece.Center = part.Center;
    piece.Radius = part.Radius;
    const std::vector<SceneNode> &nodes = model.getScene().getNodes();
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        bool bInPart = false;
        for ( const auto &node : part.Nodes ) {
            if ( node.first == static_cast<int32_t>( i ) ) {
                bInPart = true;
                break;
            }
        }
        if ( !bInPart && nodes[i].ObjectHashcode != 0 ) {
            piece.HideOthers.push_back( static_cast<int32_t>( i ) );
        }
    }
    piece.Rotation = vehicleWorld;
    piece.Rotation.m[3][0] = piece.Rotation.m[3][1] = piece.Rotation.m[3][2] = 0.0f;
    piece.Position = vehicleWorld.transformPoint( part.Center );
    // Away from the car's middle, upwards, with the car's own speed.
    Vector3f outward = piece.Position - vehicleWorld.getTranslation();
    outward.y = 0.0f;
    outward = outward.length() > 0.01f ? outward.normalized() : Vector3f( 0.0f, 0.0f, 1.0f );
    piece.Velocity = vehicleVelocity + outward * Rand( 1.5f, 4.0f ) + Vector3f( 0.0f, Rand( 1.5f, 3.5f ), 0.0f );
    piece.Spin = Vector3f( Rand( -7.0f, 7.0f ), Rand( -5.0f, 5.0f ), Rand( -7.0f, 7.0f ) );
    pieces.push_back( std::move( piece ) );
    OTDU_LOG_INFO( "CarDebris: a part came off (%zu piece(s) in the world), speed %.1f m/s\n", pieces.size(),
                   vehicleVelocity.length() );
}

void CarDebris::update( float deltaTime, const Vector3f &viewer ) {
    if ( pieces.empty() || deltaTime <= 0.0f ) {
        return;
    }
    const float dt = Min( deltaTime, 0.05f );
    for ( size_t i = 0; i < pieces.size(); ) {
        Piece &p = pieces[i];
        p.Age += dt;
        const Vector3f away = p.Position - viewer;
        if ( away.x * away.x + away.z * away.z > kRemoveDistance * kRemoveDistance ) {
            pieces.erase( pieces.begin() + static_cast<std::ptrdiff_t>( i ) );
            continue;
        }
        i++;
        if ( p.bSettled ) {
            continue;
        }
        p.Velocity.y -= kGravity * dt;
        // Air drag, a light one: a bumper is a wide thin shell.
        p.Velocity = p.Velocity * ( 1.0f - 0.15f * dt );
        p.Position = p.Position + p.Velocity * dt;
        const float spin = p.Spin.length();
        if ( spin > 1e-3f ) {
            p.Rotation = Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( p.Spin * ( 1.0f / spin ), spin * dt ) ) * p.Rotation;
            Orthonormalize( p.Rotation );
        }
        float groundY = 0.0f;
        if ( ground && ground( Vector3f( p.Position.x, p.Position.y + 1.5f, p.Position.z ), groundY ) &&
             p.Position.y - p.Radius * 0.5f < groundY ) {
            p.Position.y = groundY + p.Radius * 0.5f;
            if ( p.Velocity.y < 0.0f ) {
                p.Velocity.y = -p.Velocity.y * 0.28f;
            }
            // Scraping: the ground takes the sideways speed and the spin.
            const float grip = Max( 0.0f, 1.0f - 3.5f * dt );
            p.Velocity.x *= grip;
            p.Velocity.z *= grip;
            p.Spin = p.Spin * Max( 0.0f, 1.0f - 4.0f * dt );
            if ( p.Velocity.length() < 0.35f && p.Spin.length() < 0.6f ) {
                p.Velocity = Vector3f( 0.0f, 0.0f, 0.0f );
                p.Spin = Vector3f( 0.0f, 0.0f, 0.0f );
                p.bSettled = true;
            }
        }
        if ( p.Age > 20.0f ) {
            p.bSettled = true; // never simulate forever (no ground under it: stays where it is)
        }
    }
}

void CarDebris::draw( EdenRenderer &renderer ) {
    EdenDrawParams params;
    for ( Piece &piece : pieces ) {
        if ( piece.Model == nullptr || !piece.Model->isLoaded() ) {
            continue;
        }
        EdenModel &model = *piece.Model;
        std::vector<Matrix4x4f> saved;
        saved.reserve( piece.Nodes.size() );
        for ( const auto &node : piece.Nodes ) {
            saved.push_back( model.getNodeTransform( node.first ) );
            model.setNodeTransform( node.first, node.second );
        }
        params.ForceHiddenNodes = piece.HideOthers;
        Matrix4x4f world = piece.Rotation;
        world.m[3][0] = piece.Position.x;
        world.m[3][1] = piece.Position.y;
        world.m[3][2] = piece.Position.z;
        model.draw( renderer, world * Matrix4x4f::Translation( -piece.Center ), &params );
        for ( size_t n = 0; n < piece.Nodes.size(); n++ ) {
            model.setNodeTransform( piece.Nodes[n].first, saved[n] );
        }
    }
}
