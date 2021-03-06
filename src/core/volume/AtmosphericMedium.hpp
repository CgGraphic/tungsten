#ifndef ATMOSPHERICMEDIUM_HPP_
#define ATMOSPHERICMEDIUM_HPP_

#include "Medium.hpp"

#include "materials/Texture.hpp"

#include <memory>

namespace Tungsten {

class AtmosphericMedium : public Medium
{
    const float Rg = 6360.0f*1e3f;
    const float Rt = 6420.0f*1e3f;
    const float Hr = 8.0f*1e3f;
    const float BetaR = 5.8f*1e-6f;
    const float BetaG = 13.5f*1e-6f;
    const float BetaB = 33.1f*1e-6f;
    const float FalloffScale = 14.0f;

    const Scene *_scene;

    std::string _primName;

    Vec3f _pos;
    float _scale;
    float _cloudMinR;
    float _cloudMaxR;
    float _cloudDensity;
    float _cloudAlbedo;
    float _cloudShift;
    Vec3f _backgroundSigmaS;
    std::shared_ptr<Texture> _cloudThickness;

    Vec3f _sigmaS;
    float _rG;
    float _rT;
    float _hR;

    inline bool sphereMinTMaxT(const Vec3f &o, const Vec3f &w, Vec2f &tSpan, float r) const;

    inline Vec2f densityAndDerivative(float r, float mu, float t, float d) const;

    bool useCloudDensity(const Vec3f &q, float d) const;
    bool insideClouds(const Vec3f &p) const;

    void sampleColorChannel(VolumeScatterEvent &event, MediumState &state) const;

    Vec4f spectralOpticalDepthAndT(const Vec3f &p, const Vec3f &w, float maxT, float targetDepth, int targetChannel, float rand) const;
    Vec2f opticalDepthAndT(const Vec3f &p, const Vec3f &w, float maxT, float targetDepth) const;

public:
    AtmosphericMedium();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual bool isHomogeneous() const override;

    virtual void prepareForRender() override;
    virtual void teardownAfterRender() override;

    virtual bool sampleDistance(VolumeScatterEvent &event, MediumState &state) const override;
    virtual bool absorb(VolumeScatterEvent &event, MediumState &state) const override;
    virtual bool scatter(VolumeScatterEvent &event) const override;
    virtual Vec3f transmittance(const VolumeScatterEvent &event) const override;
    virtual Vec3f emission(const VolumeScatterEvent &event) const override;

    virtual Vec3f phaseEval(const VolumeScatterEvent &event) const override;
};

}

#endif /* ATMOSPHERICMEDIUM_HPP_ */
