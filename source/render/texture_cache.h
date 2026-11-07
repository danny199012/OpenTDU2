#pragma once

#include "render/vulkan/render_device.h"

class Bank;
struct Texture2DB;

// Owns the GPU textures created from .2DB resources. Textures are looked up by
// Eden hashcode (GetIdentifier64bit of the resource name) and grouped by
// "owner" (usually the bank path) so a whole bank can be released at once.
class TextureCache {
public:
    TextureCache();
    ~TextureCache();

    void initialize( RenderDevice *pDevice );
    void destroy();

    GPUTexture *createFrom2DB( const Texture2DB &texture, const char *pName );
    GPUTexture *createFromRGBA( uint32_t width,
                                uint32_t height,
                                const uint8_t *pPixels,
                                const char *pName );

    // Loads every .2db of a bank. Returns the number of textures loaded.
    uint32_t loadBankTextures( const Bank &bank, const char *pOwner );
    void releaseOwner( const char *pOwner );

    // Global lookup (most recently loaded owner first).
    GPUTexture *find( uint64_t hashcode ) const;
    GPUTexture *find( const char *pName ) const;
    // Lookup restricted to the textures of an owner (bank).
    GPUTexture *find( const char *pOwner, const char *pName ) const;
    // The hash form of the owner lookup: the string form re-hashes
    // IdentifierToString(hashcode), and that round trip is NOT idempotent -
    // slot bytes that land in 'a'..'z' fold a second time and the lookup
    // misses textures registered under their true hash (the airport's
    // red-noise ground, library building materials losing slot 6).
    GPUTexture *find( const char *pOwner, uint64_t hashcode ) const;
    // Global lookup on behalf of a borrower owner (the material setup
    // fallback when a texture is not in the material's own bank). A texture
    // found in another owner records the borrow: releaseOwner of the lender
    // then keeps its textures alive (retired) until every borrower is
    // released too - the lender unload used to free textures other banks'
    // materials still drew with (VUID-vkCmdDrawIndexed-None-08114, then
    // VK_ERROR_DEVICE_LOST).
    GPUTexture *findForOwner( const char *pBorrower, uint64_t hashcode );
    GPUTexture *getWhiteTexture() const { return pWhiteTexture; }
    GPUTexture *getCheckerTexture() const { return pCheckerTexture; }

private:
    struct OwnerTextures {
        std::string Owner;
        // How many loads hold these textures. Several views load the same bank
        // path (the free ride world and a shop interior's own EdenWorld stream
        // the same commonworld / island / sector banks), and the owner is keyed
        // by that path, so they share ONE set of GPU textures. The first of
        // them to unload used to destroy it for all of them: leaving the Audi
        // showroom freed the island colour map the flat map draws and the
        // sector textures the free ride world still drew with (the owner's
        // "odd textures being stamped on the map", then the GPU hang).
        uint32_t References = 0; // loadBankTextures ++, releaseOwner --
        std::unordered_map<uint64_t, GPUTexture *> Textures;  // Owned
        std::unordered_map<uint64_t, GPUTexture *> Aliases;   // Not owned
    };

    RenderDevice *pDevice;
    std::vector<OwnerTextures> owners; // Load order
    // Released while borrowed: not findable, destroyed once unborrowed.
    std::vector<OwnerTextures> retired;
    // Borrower owner -> lender owner -> number of material slots.
    std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>> borrows;

    OwnerTextures &getOwner( const char *pOwner );
    bool isBorrowed( const std::string &lender ) const;
    void destroyOwnerTextures( OwnerTextures &owner );
    GPUTexture *pWhiteTexture;
    GPUTexture *pCheckerTexture;
};
