#pragma once

#include <string>
#include <vector>

// Discovery objects of an island [DATA]: the wrecks (islands/<island>/scenarics/
// wrecks/wrecks.bnk -> entities.xmb, Entity__Wreck.orient_pos_scale.pos; the
// detector limits come from scenarics/wrecks/wrecks_config.cpr: range 250 m,
// bips every 1.35 s (far) to 0.20 s (near), activation distance 10 m) and the
// panoramas (islands/<island>/scenarics/photos.bnk, the nodes of its .3DD).
struct DiscoveryPoint {
    int32_t Id = 0;
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    std::string Name;
    int32_t Area = -1; // cruising zone (0-based), -1 outside
    bool bFound = false;
};

class DiscoveryPoints {
public:
    void load( const std::string &island );
    inline const std::vector<DiscoveryPoint> &getWrecks() const { return wrecks; }
    inline const std::vector<DiscoveryPoint> &getPanoramas() const { return panoramas; }
    inline std::vector<DiscoveryPoint> &getWrecks() { return wrecks; }
    inline std::vector<DiscoveryPoint> &getPanoramas() { return panoramas; }
    inline float getDetectorRange() const { return detectorRange; }
    inline float getActivationDistance() const { return activationDistance; }

private:
    std::vector<DiscoveryPoint> wrecks, panoramas;
    float detectorRange = 250.0f, activationDistance = 10.0f;
};
