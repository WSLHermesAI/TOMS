#include "power_bar.h"
#include <algorithm>
#include <cmath>

namespace toms {

float simulatePosition(const PowerBarParams& p, float heldSeconds, float stepSeconds) {
    if (heldSeconds <= 0.0f) return 0.0f;
    float span = 2.0f * p.redOuter;
    float position = 0.0f;
    float direction = 1.0f;
    float t = 0.0f;
    while (t < heldSeconds) {
        float dt = std::min(stepSeconds, heldSeconds - t);
        float progress = p.rampTime > 0.0f ? std::min(1.0f, t / p.rampTime) : 1.0f;
        float speed = p.v0 + (p.vmax - p.v0) * (progress * progress * progress);   // cubic ease-in
        position += direction * speed * dt;
        if (position > span) { position = 2.0f * span - position; direction = -1.0f; }
        if (position < 0.0f) { position = -position; direction = 1.0f; }
        t += dt;
    }
    return position;
}

float powerFromPosition(const PowerBarParams& p, float position) {
    float center = p.redOuter;
    float dist = std::fabs(position - center);
    if (dist <= p.greenHalf) {
        float t = p.greenHalf <= 0.0f ? 0.0f : dist / p.greenHalf;
        return 100.0f - 29.0f * t;                       // 100 (center) .. 71 (green edge)
    } else if (dist <= p.blueOuter) {
        float t = (dist - p.greenHalf) / (p.blueOuter - p.greenHalf);
        return 70.0f - 39.0f * t;                         // 70 .. 31
    } else {
        float t = std::min(1.0f, (dist - p.blueOuter) / (p.redOuter - p.blueOuter));
        return 30.0f - 30.0f * t;                         // 30 .. 0 (edge)
    }
}

PowerZone zoneFromPosition(const PowerBarParams& p, float position) {
    float dist = std::fabs(position - p.redOuter);
    if (dist <= p.greenHalf) return PowerZone::Green;
    if (dist <= p.blueOuter) return PowerZone::Blue;
    return PowerZone::Red;
}

int computeAttackDamage(int baseHit, float powerPercent, float maxMult) {
    float mult = 0.2f + (maxMult - 0.2f) * (powerPercent / 100.0f);
    int dmg = (int)std::ceil(baseHit * mult);
    if (powerPercent >= 97.0f) dmg = (int)std::ceil(dmg * 1.25f);
    return dmg;
}

int computeDefenseDamage(int incoming, float powerPercent, float mitigationFloor) {
    if (powerPercent >= 99.0f) return 0;   // Perfect Guard, regardless of any floor
    float mitigation = powerPercent / 100.0f;
    if (mitigation < mitigationFloor) mitigation = mitigationFloor;
    return (int)std::ceil(incoming * (1.0f - mitigation));
}

} // namespace toms
