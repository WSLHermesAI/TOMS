// particle_fx.cpp -- see particle_fx.h.
#include "particle_fx.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>

using nlohmann::json;

namespace toms::fx {

// ---- curves ------------------------------------------------------------------------------------

template <class T> T Curve<T>::f_At(float l_fT, const T& l_oNone) const {
    if (m_vKeys.empty()) return l_oNone;
    if (l_fT <= m_vKeys.front().m_fT) return m_vKeys.front().m_oV;
    if (l_fT >= m_vKeys.back().m_fT) return m_vKeys.back().m_oV;
    auto l_oIt = std::upper_bound(m_vKeys.begin(), m_vKeys.end(), l_fT, [](float l_fX, const CurveKey<T>& l_oK) { return l_fX < l_oK.m_fT; });
    const CurveKey<T>& l_oB = *l_oIt;
    const CurveKey<T>& l_oA = *(l_oIt - 1);
    const float l_fSpan = l_oB.m_fT - l_oA.m_fT;
    if (l_fSpan <= 0 || l_oA.m_oEase.f_IsStepped()) return l_oA.m_oV;
    return l_oA.m_oV + (l_oB.m_oV - l_oA.m_oV) * l_oA.m_oEase.f_Apply((l_fT - l_oA.m_fT) / l_fSpan);
}
template struct Curve<float>;
template struct Curve<glm::vec4>;

// ---- names -------------------------------------------------------------------------------------

namespace {

const char* const g_arrFactorNames[Blend::FactorCount] = {"zero",     "one",         "srcColor", "invSrcColor", "srcAlpha",
                                                       "invSrcAlpha", "dstColor", "invDstColor", "dstAlpha", "invDstAlpha"};
const char* const g_arrShapeNames[] = {"point", "line", "box", "circle", "ring"};

}  // namespace

const char* Blend::f_FactorName(Factor l_eF) { return l_eF < FactorCount ? g_arrFactorNames[l_eF] : "?"; }

bool Blend::f_FactorFromName(const std::string& l_strName, Factor& l_eOut) {
    for (int l_iI = 0; l_iI < FactorCount; l_iI++)
        if (l_strName == g_arrFactorNames[l_iI]) { l_eOut = Factor(l_iI); return true; }
    return false;
}

std::string Blend::f_PresetName() const {
    if (*this == f_Normal()) return "normal";
    if (*this == f_Add()) return "add";
    if (*this == f_Multiply()) return "multiply";
    if (*this == f_Screen()) return "screen";
    return std::string();
}

bool Blend::f_FromPreset(const std::string& l_strName, Blend& l_oOut) {
    if (l_strName == "normal") l_oOut = f_Normal();
    else if (l_strName == "add") l_oOut = f_Add();
    else if (l_strName == "multiply") l_oOut = f_Multiply();
    else if (l_strName == "screen") l_oOut = f_Screen();
    else return false;
    return true;
}

const char* f_ShapeName(Shape::Type l_eT) { return g_arrShapeNames[l_eT]; }

const char* f_SimulationName(Emitter::Simulation l_eS) {
    return l_eS == Emitter::Simulation::Gpu ? "gpu" : l_eS == Emitter::Simulation::Cpu ? "cpu" : "auto";
}

const Effect* ParticleFile::f_Find(const std::string& l_strName) const {
    for (const Effect& l_oE : m_vEffects)
        if (l_oE.m_strName == l_strName) return &l_oE;
    return nullptr;
}

// ---- JSON --------------------------------------------------------------------------------------

namespace {

[[noreturn]] void f_Fail(const std::string& l_strWhere, const std::string& l_strWhat) { throw std::runtime_error(l_strWhere + ": " + l_strWhat); }

// Unknown fields are errors: a typo ("colour") would otherwise silently do nothing.
void f_OnlyKnown(const json& l_jJ, std::initializer_list<const char*> l_oKnown, const std::string& l_strWhere) {
    if (!l_jJ.is_object()) f_Fail(l_strWhere, "must be an object");
    for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
        bool l_bOk = false;
        for (const char* l_pszK : l_oKnown) l_bOk |= l_oIt.key() == l_pszK;
        if (!l_bOk) f_Fail(l_strWhere, "unknown field '" + l_oIt.key() + "'");
    }
}

float f_Num(const json& l_jJ, const std::string& l_strWhere) {
    if (!l_jJ.is_number()) f_Fail(l_strWhere, "must be a number");
    return l_jJ.get<float>();
}

Range f_RangeFrom(const json& l_jJ, const std::string& l_strWhere) {
    if (l_jJ.is_number()) return Range(l_jJ.get<float>());
    if (l_jJ.is_array() && l_jJ.size() == 2 && l_jJ[0].is_number() && l_jJ[1].is_number()) return Range(l_jJ[0].get<float>(), l_jJ[1].get<float>());
    f_Fail(l_strWhere, "must be a number or [min, max]");
}

glm::vec2 f_Vec2From(const json& l_jJ, const std::string& l_strWhere) {
    if (!l_jJ.is_array() || l_jJ.size() != 2) f_Fail(l_strWhere, "must be [x, y]");
    return {f_Num(l_jJ[0], l_strWhere), f_Num(l_jJ[1], l_strWhere)};
}

glm::vec4 f_ColorFrom(const json& l_jJ, const std::string& l_strWhere) {
    if (!l_jJ.is_array() || l_jJ.size() != 4) f_Fail(l_strWhere, "must be [r, g, b, a] (0..1)");
    return {f_Num(l_jJ[0], l_strWhere), f_Num(l_jJ[1], l_strWhere), f_Num(l_jJ[2], l_strWhere), f_Num(l_jJ[3], l_strWhere)};
}

void f_EaseFrom(const json& l_jJ, Ease& l_oE, const std::string& l_strWhere) {
    if (l_jJ.is_string()) {
        if (!toms::anim::f_ParseEase(l_jJ.get<std::string>(), l_oE)) f_Fail(l_strWhere, "unknown ease '" + l_jJ.get<std::string>() + "'");
    } else if (l_jJ.is_array() && l_jJ.size() == 4) {
        l_oE.m_iKind = Ease::m_iBezier;
        for (int l_iI = 0; l_iI < 4; l_iI++) l_oE.m_arrBezier[l_iI] = f_Num(l_jJ[l_iI], l_strWhere);
    } else {
        f_Fail(l_strWhere, "ease must be a name or [x1, y1, x2, y2]");
    }
}

template <class T, class F> void f_CurveFrom(const json& l_jJ, Curve<T>& l_oC, F l_oValue, const std::string& l_strWhere) {
    if (!l_jJ.is_array()) f_Fail(l_strWhere, "must be a list of {\"t\", \"v\"} keys");
    for (const json& l_jK : l_jJ) {
        f_OnlyKnown(l_jK, {"t", "v", "ease"}, l_strWhere);
        if (!l_jK.contains("t") || !l_jK.contains("v")) f_Fail(l_strWhere, "a key needs \"t\" and \"v\"");
        CurveKey<T> l_oKey;
        l_oKey.m_fT = f_Num(l_jK["t"], l_strWhere);
        l_oKey.m_oV = l_oValue(l_jK["v"], l_strWhere);
        if (l_jK.contains("ease")) f_EaseFrom(l_jK["ease"], l_oKey.m_oEase, l_strWhere);
        l_oC.m_vKeys.push_back(l_oKey);
    }
    std::stable_sort(l_oC.m_vKeys.begin(), l_oC.m_vKeys.end(), [](const CurveKey<T>& l_oA, const CurveKey<T>& l_oB) { return l_oA.m_fT < l_oB.m_fT; });
}

void f_EmitterFrom(const json& l_jJ, Emitter& l_oE, const std::string& l_strEffectName) {
    f_OnlyKnown(l_jJ, {"name", "offset", "start", "stop", "space", "maxParticles", "emission", "shape", "life", "direction",
                  "spread", "speed", "size", "rotation", "spin", "color", "sprite", "overLife", "forces", "flipbook",
                  "render", "simulation"},
              l_strEffectName + "/?");
    l_oE.m_strName = l_jJ.value("name", std::string());
    const std::string l_strW = l_strEffectName + "/" + l_oE.m_strName;
    if (l_jJ.contains("offset")) l_oE.m_v2Offset = f_Vec2From(l_jJ["offset"], l_strW + ".offset");
    if (l_jJ.contains("start")) l_oE.m_fStart = f_Num(l_jJ["start"], l_strW + ".start");
    if (l_jJ.contains("stop")) l_oE.m_fStop = f_Num(l_jJ["stop"], l_strW + ".stop");
    if (l_jJ.contains("space")) {
        const std::string l_strS = l_jJ["space"].is_string() ? l_jJ["space"].get<std::string>() : std::string();
        if (l_strS != "world" && l_strS != "local") f_Fail(l_strW + ".space", "must be \"world\" or \"local\"");
        l_oE.m_bLocalSpace = l_strS == "local";
    }
    if (l_jJ.contains("maxParticles")) l_oE.m_iMaxParticles = (int)f_Num(l_jJ["maxParticles"], l_strW + ".maxParticles");
    if (l_jJ.contains("simulation")) {
        const std::string l_strS = l_jJ["simulation"].is_string() ? l_jJ["simulation"].get<std::string>() : std::string();
        if (l_strS != "auto" && l_strS != "cpu" && l_strS != "gpu") f_Fail(l_strW + ".simulation", "must be \"auto\", \"cpu\" or \"gpu\"");
        l_oE.m_eSimulation = l_strS == "gpu" ? Emitter::Simulation::Gpu : l_strS == "cpu" ? Emitter::Simulation::Cpu : Emitter::Simulation::Auto;
    }
    if (l_jJ.contains("emission")) {
        const json& l_jEm = l_jJ["emission"];
        f_OnlyKnown(l_jEm, {"rate", "bursts"}, l_strW + ".emission");
        if (l_jEm.contains("rate")) l_oE.m_fRate = f_Num(l_jEm["rate"], l_strW + ".emission.rate");
        for (const json& l_jB : l_jEm.value("bursts", json::array())) {
            f_OnlyKnown(l_jB, {"t", "count", "repeat", "interval"}, l_strW + ".emission.bursts");
            Burst l_oBurst;
            if (l_jB.contains("t")) l_oBurst.m_fT = f_Num(l_jB["t"], l_strW + ".bursts.t");
            if (l_jB.contains("count")) l_oBurst.m_oCount = f_RangeFrom(l_jB["count"], l_strW + ".bursts.count");
            if (l_jB.contains("repeat")) l_oBurst.m_iRepeat = (int)f_Num(l_jB["repeat"], l_strW + ".bursts.repeat");
            if (l_jB.contains("interval")) l_oBurst.m_fInterval = f_Num(l_jB["interval"], l_strW + ".bursts.interval");
            l_oE.m_vBursts.push_back(l_oBurst);
        }
    }
    if (l_jJ.contains("shape")) {
        const json& l_jS = l_jJ["shape"];
        const std::string l_strSw = l_strW + ".shape";
        f_OnlyKnown(l_jS, {"type", "length", "angle", "size", "radius", "thickness", "arc", "edge", "outward"}, l_strSw);
        const std::string l_strType = l_jS.value("type", std::string("point"));
        int l_iT = -1;
        for (int l_iI = 0; l_iI < 5; l_iI++)
            if (l_strType == g_arrShapeNames[l_iI]) l_iT = l_iI;
        if (l_iT < 0) f_Fail(l_strSw, "type must be point, line, box, circle or ring");
        l_oE.m_oShape.m_eType = Shape::Type(l_iT);
        if (l_jS.contains("length")) l_oE.m_oShape.m_fLength = f_Num(l_jS["length"], l_strSw);
        if (l_jS.contains("angle")) l_oE.m_oShape.m_fAngle = f_Num(l_jS["angle"], l_strSw);
        if (l_jS.contains("size")) { const glm::vec2 l_v2V = f_Vec2From(l_jS["size"], l_strSw + ".size"); l_oE.m_oShape.m_fWidth = l_v2V.x; l_oE.m_oShape.m_fHeight = l_v2V.y; }
        if (l_jS.contains("radius")) l_oE.m_oShape.m_fRadius = f_Num(l_jS["radius"], l_strSw);
        if (l_jS.contains("thickness")) l_oE.m_oShape.m_fThickness = f_Num(l_jS["thickness"], l_strSw);
        if (l_jS.contains("arc")) { const glm::vec2 l_v2V = f_Vec2From(l_jS["arc"], l_strSw + ".arc"); l_oE.m_oShape.m_fArcFrom = l_v2V.x; l_oE.m_oShape.m_fArcTo = l_v2V.y; }
        if (l_jS.contains("edge")) l_oE.m_oShape.m_bEdge = l_jS["edge"].get<bool>();
        if (l_jS.contains("outward")) l_oE.m_oShape.m_bOutward = l_jS["outward"].get<bool>();
    }
    if (l_jJ.contains("life")) l_oE.m_oLife = f_RangeFrom(l_jJ["life"], l_strW + ".life");
    if (l_jJ.contains("direction")) l_oE.m_fDirection = f_Num(l_jJ["direction"], l_strW + ".direction");
    if (l_jJ.contains("spread")) l_oE.m_fSpread = f_Num(l_jJ["spread"], l_strW + ".spread");
    if (l_jJ.contains("speed")) l_oE.m_oSpeed = f_RangeFrom(l_jJ["speed"], l_strW + ".speed");
    if (l_jJ.contains("size")) l_oE.m_oSize = f_RangeFrom(l_jJ["size"], l_strW + ".size");
    if (l_jJ.contains("rotation")) l_oE.m_oRotation = f_RangeFrom(l_jJ["rotation"], l_strW + ".rotation");
    if (l_jJ.contains("spin")) l_oE.m_oSpin = f_RangeFrom(l_jJ["spin"], l_strW + ".spin");
    if (l_jJ.contains("color")) {   // one colour, or two to pick between
        const json& l_jC = l_jJ["color"];
        if (l_jC.is_array() && l_jC.size() == 2 && l_jC[0].is_array()) {
            l_oE.m_v4Color = f_ColorFrom(l_jC[0], l_strW + ".color");
            l_oE.m_v4Color2 = f_ColorFrom(l_jC[1], l_strW + ".color");
        } else {
            l_oE.m_v4Color = l_oE.m_v4Color2 = f_ColorFrom(l_jC, l_strW + ".color");
        }
    }
    if (l_jJ.contains("sprite")) l_oE.m_strSprite = l_jJ["sprite"].get<std::string>();
    if (l_jJ.contains("overLife")) {
        const json& l_jO = l_jJ["overLife"];
        f_OnlyKnown(l_jO, {"color", "size", "speed", "spin"}, l_strW + ".overLife");
        auto l_fnF = [](const json& l_jV, const std::string& l_strWhere) { return f_Num(l_jV, l_strWhere); };
        if (l_jO.contains("color")) f_CurveFrom(l_jO["color"], l_oE.m_oColorOverLife, f_ColorFrom, l_strW + ".overLife.color");
        if (l_jO.contains("size")) f_CurveFrom(l_jO["size"], l_oE.m_oSizeOverLife, l_fnF, l_strW + ".overLife.size");
        if (l_jO.contains("speed")) f_CurveFrom(l_jO["speed"], l_oE.m_oSpeedOverLife, l_fnF, l_strW + ".overLife.speed");
        if (l_jO.contains("spin")) f_CurveFrom(l_jO["spin"], l_oE.m_oSpinOverLife, l_fnF, l_strW + ".overLife.spin");
    }
    if (l_jJ.contains("forces")) {
        const json& l_jF = l_jJ["forces"];
        f_OnlyKnown(l_jF, {"gravity", "drag", "radial", "tangential"}, l_strW + ".forces");
        if (l_jF.contains("gravity")) l_oE.m_v2Gravity = f_Vec2From(l_jF["gravity"], l_strW + ".forces.gravity");
        if (l_jF.contains("drag")) l_oE.m_fDrag = f_Num(l_jF["drag"], l_strW + ".forces.drag");
        if (l_jF.contains("radial")) l_oE.m_oRadial = f_RangeFrom(l_jF["radial"], l_strW + ".forces.radial");
        if (l_jF.contains("tangential")) l_oE.m_oTangential = f_RangeFrom(l_jF["tangential"], l_strW + ".forces.tangential");
    }
    if (l_jJ.contains("flipbook")) {
        const json& l_jF = l_jJ["flipbook"];
        f_OnlyKnown(l_jF, {"frames", "mode", "fps", "randomStart"}, l_strW + ".flipbook");
        for (const json& l_jFr : l_jF.value("frames", json::array())) l_oE.m_vFrames.push_back(l_jFr.get<std::string>());
        const std::string l_strMode = l_jF.value("mode", std::string("overLife"));
        if (l_strMode != "overLife" && l_strMode != "fps") f_Fail(l_strW + ".flipbook.mode", "must be \"overLife\" or \"fps\"");
        l_oE.m_bFramesByFps = l_strMode == "fps";
        if (l_jF.contains("fps")) l_oE.m_fFps = f_Num(l_jF["fps"], l_strW + ".flipbook.fps");
        l_oE.m_bRandomFrame = l_jF.value("randomStart", false);
    }
    if (l_jJ.contains("render")) {
        const json& l_jR = l_jJ["render"];
        f_OnlyKnown(l_jR, {"blend", "order", "alignToVelocity", "oldestOnTop"}, l_strW + ".render");
        if (l_jR.contains("blend")) {
            const json& l_jB = l_jR["blend"];
            if (l_jB.is_string()) {
                if (!Blend::f_FromPreset(l_jB.get<std::string>(), l_oE.m_oBlend))
                    f_Fail(l_strW + ".render.blend", "unknown preset '" + l_jB.get<std::string>() + "' (normal, add, multiply, screen)");
            } else {
                f_OnlyKnown(l_jB, {"src", "dst"}, l_strW + ".render.blend");
                if (!l_jB.contains("src") || !l_jB.contains("dst") ||
                    !Blend::f_FactorFromName(l_jB["src"].get<std::string>(), l_oE.m_oBlend.m_eSrc) ||
                    !Blend::f_FactorFromName(l_jB["dst"].get<std::string>(), l_oE.m_oBlend.m_eDst))
                    f_Fail(l_strW + ".render.blend", "needs \"src\" and \"dst\" factors (zero, one, srcColor, invSrcColor, srcAlpha, "
                                              "invSrcAlpha, dstColor, invDstColor, dstAlpha, invDstAlpha)");
            }
        }
        if (l_jR.contains("order")) l_oE.m_iOrder = (int)f_Num(l_jR["order"], l_strW + ".render.order");
        l_oE.m_bAlignToVelocity = l_jR.value("alignToVelocity", false);
        l_oE.m_bOldestOnTop = l_jR.value("oldestOnTop", false);
    }
}

json f_RangeJson(const Range& l_oR) { return l_oR.f_Fixed() ? json(l_oR.m_fMin) : json::array({l_oR.m_fMin, l_oR.m_fMax}); }
json f_VecJson(const glm::vec2& l_v2V) { return json::array({l_v2V.x, l_v2V.y}); }
json f_VecJson(const glm::vec4& l_v4V) { return json::array({l_v4V.x, l_v4V.y, l_v4V.z, l_v4V.w}); }
json f_VecOrNum(float l_fV) { return json(l_fV); }
json f_VecOrNum(const glm::vec4& l_v4V) { return f_VecJson(l_v4V); }

template <class T> json f_CurveJson(const Curve<T>& l_oC) {
    json l_jA = json::array();
    for (const CurveKey<T>& l_oK : l_oC.m_vKeys) {
        json l_jO{{"t", l_oK.m_fT}, {"v", f_VecOrNum(l_oK.m_oV)}};
        if (!l_oK.m_oEase.f_IsLinear()) {
            if (l_oK.m_oEase.m_iKind == Ease::m_iBezier)
                l_jO["ease"] = json::array({l_oK.m_oEase.m_arrBezier[0], l_oK.m_oEase.m_arrBezier[1], l_oK.m_oEase.m_arrBezier[2], l_oK.m_oEase.m_arrBezier[3]});
            else
                l_jO["ease"] = toms::anim::f_EaseName(l_oK.m_oEase);
        }
        l_jA.push_back(l_jO);
    }
    return l_jA;
}

json f_EmitterJson(const Emitter& l_oE) {
    const Emitter l_oD;   // defaults are left out
    json l_jJ;
    l_jJ["name"] = l_oE.m_strName;
    if (l_oE.m_v2Offset != l_oD.m_v2Offset) l_jJ["offset"] = f_VecJson(l_oE.m_v2Offset);
    if (l_oE.m_fStart != l_oD.m_fStart) l_jJ["start"] = l_oE.m_fStart;
    if (l_oE.m_fStop != l_oD.m_fStop) l_jJ["stop"] = l_oE.m_fStop;
    if (l_oE.m_bLocalSpace) l_jJ["space"] = "local";
    if (l_oE.m_iMaxParticles != l_oD.m_iMaxParticles) l_jJ["maxParticles"] = l_oE.m_iMaxParticles;
    if (l_oE.m_eSimulation != l_oD.m_eSimulation) l_jJ["simulation"] = f_SimulationName(l_oE.m_eSimulation);
    json l_jEm = json::object();
    if (l_oE.m_fRate != l_oD.m_fRate) l_jEm["rate"] = l_oE.m_fRate;
    if (!l_oE.m_vBursts.empty()) {
        l_jEm["bursts"] = json::array();
        for (const Burst& l_oB : l_oE.m_vBursts) {
            json l_jBj{{"t", l_oB.m_fT}, {"count", f_RangeJson(l_oB.m_oCount)}};
            if (l_oB.m_iRepeat != 0) { l_jBj["repeat"] = l_oB.m_iRepeat; l_jBj["interval"] = l_oB.m_fInterval; }
            l_jEm["bursts"].push_back(l_jBj);
        }
    }
    if (!l_jEm.empty()) l_jJ["emission"] = l_jEm;
    if (l_oE.m_oShape.m_eType != Shape::Point || l_oE.m_oShape.m_bOutward) {
        const Shape& l_oS = l_oE.m_oShape;
        json l_jSj{{"type", g_arrShapeNames[l_oS.m_eType]}};
        if (l_oS.m_eType == Shape::Line) { l_jSj["length"] = l_oS.m_fLength; if (l_oS.m_fAngle != 0) l_jSj["angle"] = l_oS.m_fAngle; }
        if (l_oS.m_eType == Shape::Box) l_jSj["size"] = json::array({l_oS.m_fWidth, l_oS.m_fHeight});
        if (l_oS.m_eType == Shape::Circle || l_oS.m_eType == Shape::Ring) {
            l_jSj["radius"] = l_oS.m_fRadius;
            if (l_oS.m_eType == Shape::Ring) l_jSj["thickness"] = l_oS.m_fThickness;
            if (l_oS.m_fArcFrom != 0 || l_oS.m_fArcTo != 360) l_jSj["arc"] = json::array({l_oS.m_fArcFrom, l_oS.m_fArcTo});
        }
        if (l_oS.m_bEdge) l_jSj["edge"] = true;
        if (l_oS.m_bOutward) l_jSj["outward"] = true;
        l_jJ["shape"] = l_jSj;
    }
    if (!(l_oE.m_oLife == l_oD.m_oLife)) l_jJ["life"] = f_RangeJson(l_oE.m_oLife);
    if (l_oE.m_fDirection != l_oD.m_fDirection) l_jJ["direction"] = l_oE.m_fDirection;
    if (l_oE.m_fSpread != l_oD.m_fSpread) l_jJ["spread"] = l_oE.m_fSpread;
    if (!(l_oE.m_oSpeed == l_oD.m_oSpeed)) l_jJ["speed"] = f_RangeJson(l_oE.m_oSpeed);
    if (!(l_oE.m_oSize == l_oD.m_oSize)) l_jJ["size"] = f_RangeJson(l_oE.m_oSize);
    if (!(l_oE.m_oRotation == l_oD.m_oRotation)) l_jJ["rotation"] = f_RangeJson(l_oE.m_oRotation);
    if (!(l_oE.m_oSpin == l_oD.m_oSpin)) l_jJ["spin"] = f_RangeJson(l_oE.m_oSpin);
    if (l_oE.m_v4Color != l_oE.m_v4Color2) l_jJ["color"] = json::array({f_VecJson(l_oE.m_v4Color), f_VecJson(l_oE.m_v4Color2)});
    else if (l_oE.m_v4Color != l_oD.m_v4Color) l_jJ["color"] = f_VecJson(l_oE.m_v4Color);
    if (!l_oE.m_strSprite.empty()) l_jJ["sprite"] = l_oE.m_strSprite;
    json l_jO = json::object();
    if (!l_oE.m_oColorOverLife.f_Empty()) l_jO["color"] = f_CurveJson(l_oE.m_oColorOverLife);
    if (!l_oE.m_oSizeOverLife.f_Empty()) l_jO["size"] = f_CurveJson(l_oE.m_oSizeOverLife);
    if (!l_oE.m_oSpeedOverLife.f_Empty()) l_jO["speed"] = f_CurveJson(l_oE.m_oSpeedOverLife);
    if (!l_oE.m_oSpinOverLife.f_Empty()) l_jO["spin"] = f_CurveJson(l_oE.m_oSpinOverLife);
    if (!l_jO.empty()) l_jJ["overLife"] = l_jO;
    json l_jF = json::object();
    if (l_oE.m_v2Gravity != l_oD.m_v2Gravity) l_jF["gravity"] = f_VecJson(l_oE.m_v2Gravity);
    if (l_oE.m_fDrag != l_oD.m_fDrag) l_jF["drag"] = l_oE.m_fDrag;
    if (!(l_oE.m_oRadial == l_oD.m_oRadial)) l_jF["radial"] = f_RangeJson(l_oE.m_oRadial);
    if (!(l_oE.m_oTangential == l_oD.m_oTangential)) l_jF["tangential"] = f_RangeJson(l_oE.m_oTangential);
    if (!l_jF.empty()) l_jJ["forces"] = l_jF;
    if (!l_oE.m_vFrames.empty()) {
        json l_jFb{{"frames", l_oE.m_vFrames}};
        if (l_oE.m_bFramesByFps) { l_jFb["mode"] = "fps"; l_jFb["fps"] = l_oE.m_fFps; }
        if (l_oE.m_bRandomFrame) l_jFb["randomStart"] = true;
        l_jJ["flipbook"] = l_jFb;
    }
    json l_jR = json::object();
    if (l_oE.m_oBlend != l_oD.m_oBlend) {
        const std::string l_strPreset = l_oE.m_oBlend.f_PresetName();
        l_jR["blend"] = l_strPreset.empty() ? json{{"src", Blend::f_FactorName(l_oE.m_oBlend.m_eSrc)}, {"dst", Blend::f_FactorName(l_oE.m_oBlend.m_eDst)}}
                                    : json(l_strPreset);
    }
    if (l_oE.m_iOrder != l_oD.m_iOrder) l_jR["order"] = l_oE.m_iOrder;
    if (l_oE.m_bAlignToVelocity) l_jR["alignToVelocity"] = true;
    if (l_oE.m_bOldestOnTop) l_jR["oldestOnTop"] = true;
    if (!l_jR.empty()) l_jJ["render"] = l_jR;
    return l_jJ;
}

}  // namespace

bool f_ParseParticles(const std::string& l_strText, ParticleFile& l_oOut, std::string* l_pErr) {
    try {
        const json l_jJ = json::parse(l_strText);
        f_OnlyKnown(l_jJ, {"version", "atlases", "effects"}, "file");
        ParticleFile l_oF;
        l_oF.m_iVersion = l_jJ.value("version", 1);
        for (const json& l_jA : l_jJ.value("atlases", json::array())) {
            toms::anim::AtlasRef l_oR;
            if (l_jA.is_string()) l_oR.m_strPath = l_jA.get<std::string>();
            else { l_oR.m_strPath = l_jA.at("path").get<std::string>(); l_oR.m_strId = l_jA.value("id", std::string()); }
            if (l_oR.m_strId.empty()) l_oR.m_strId = toms::anim::f_DefaultAtlasId(l_oR.m_strPath);
            for (const toms::anim::AtlasRef& l_oO : l_oF.m_vAtlases)
                if (l_oO.m_strId == l_oR.m_strId) f_Fail("atlases", "two atlases have the id '" + l_oR.m_strId + "': give one an \"id\"");
            l_oF.m_vAtlases.push_back(l_oR);
        }
        for (const json& l_jEj : l_jJ.value("effects", json::array())) {
            f_OnlyKnown(l_jEj, {"name", "duration", "loop", "prewarm", "seed", "emitters"}, "effect");
            Effect l_oE;
            l_oE.m_strName = l_jEj.value("name", std::string());
            if (l_jEj.contains("duration")) l_oE.m_fDuration = f_Num(l_jEj["duration"], l_oE.m_strName + ".duration");
            l_oE.m_bLoop = l_jEj.value("loop", false);
            if (l_jEj.contains("prewarm")) l_oE.m_fPrewarm = f_Num(l_jEj["prewarm"], l_oE.m_strName + ".prewarm");
            l_oE.m_uSeed = l_jEj.value("seed", 0u);
            for (const json& l_jEm : l_jEj.value("emitters", json::array())) {
                l_oE.m_vEmitters.emplace_back();
                f_EmitterFrom(l_jEm, l_oE.m_vEmitters.back(), l_oE.m_strName);
            }
            l_oF.m_vEffects.push_back(std::move(l_oE));
        }
        l_oOut = std::move(l_oF);
        return true;
    } catch (const std::exception& l_oE) {
        if (l_pErr) *l_pErr = l_oE.what();
        return false;
    }
}

std::string f_ParticlesToJson(const ParticleFile& l_oF) {
    json l_jJ;
    l_jJ["version"] = l_oF.m_iVersion;
    if (!l_oF.m_vAtlases.empty()) {
        json l_jA = json::array();
        for (const toms::anim::AtlasRef& l_oR : l_oF.m_vAtlases) {
            if (l_oR.m_strId == toms::anim::f_DefaultAtlasId(l_oR.m_strPath)) l_jA.push_back(l_oR.m_strPath);
            else l_jA.push_back({{"id", l_oR.m_strId}, {"path", l_oR.m_strPath}});
        }
        l_jJ["atlases"] = l_jA;
    }
    l_jJ["effects"] = json::array();
    for (const Effect& l_oE : l_oF.m_vEffects) {
        json l_jEj{{"name", l_oE.m_strName}};
        if (l_oE.m_fDuration != 0) l_jEj["duration"] = l_oE.m_fDuration;
        if (l_oE.m_bLoop) l_jEj["loop"] = true;
        if (l_oE.m_fPrewarm != 0) l_jEj["prewarm"] = l_oE.m_fPrewarm;
        if (l_oE.m_uSeed != 0) l_jEj["seed"] = l_oE.m_uSeed;
        l_jEj["emitters"] = json::array();
        for (const Emitter& l_oEm : l_oE.m_vEmitters) l_jEj["emitters"].push_back(f_EmitterJson(l_oEm));
        l_jJ["effects"].push_back(l_jEj);
    }
    return l_jJ.dump(2) + "\n";
}

// ---- checks ------------------------------------------------------------------------------------

std::vector<Problem> f_CheckParticles(const ParticleFile& l_oF, const toms::anim::AtlasSet* l_pAtlases) {
    std::vector<Problem> l_vOut;
    auto l_fnErr = [&](const std::string& l_strW, const std::string& l_strT) { l_vOut.push_back({l_strW, l_strT, false}); };
    auto l_fnWarn = [&](const std::string& l_strW, const std::string& l_strT) { l_vOut.push_back({l_strW, l_strT, true}); };
    std::set<std::string> l_setEffectNames;
    for (const Effect& l_oE : l_oF.m_vEffects) {
        if (l_oE.m_strName.empty()) l_fnErr("(effect)", "an effect needs a name");
        else if (!l_setEffectNames.insert(l_oE.m_strName).second) l_fnErr(l_oE.m_strName, "two effects have this name");
        if (l_oE.m_fDuration < 0) l_fnErr(l_oE.m_strName, "duration must not be negative");
        if (l_oE.m_bLoop && l_oE.m_fDuration <= 0) l_fnWarn(l_oE.m_strName, "loop has no effect without a duration");
        if (l_oE.m_vEmitters.empty()) l_fnWarn(l_oE.m_strName, "no emitters: draws nothing");
        std::set<std::string> l_setEmitterNames;
        for (const Emitter& l_oM : l_oE.m_vEmitters) {
            const std::string l_strW = l_oE.m_strName + "/" + l_oM.m_strName;
            if (!l_setEmitterNames.insert(l_oM.m_strName).second) l_fnWarn(l_strW, "two emitters have this name");
            if (l_oM.m_iMaxParticles <= 0) l_fnErr(l_strW, "maxParticles must be at least 1");
            if (l_oM.m_fStop > 0 && l_oM.m_fStop <= l_oM.m_fStart) l_fnErr(l_strW, "stop must be after start");
            if (l_oM.m_oLife.m_fMin <= 0) l_fnErr(l_strW, "life must be above 0");
            for (const auto& [l_pszName, l_oR] : {std::pair<const char*, Range>{"life", l_oM.m_oLife}, {"speed", l_oM.m_oSpeed}, {"size", l_oM.m_oSize},
                                          {"rotation", l_oM.m_oRotation}, {"spin", l_oM.m_oSpin}, {"radial", l_oM.m_oRadial},
                                          {"tangential", l_oM.m_oTangential}})
                if (l_oR.m_fMin > l_oR.m_fMax) l_fnErr(l_strW, std::string(l_pszName) + ": min is above max");
            if (l_oM.m_fRate < 0) l_fnErr(l_strW, "emission.rate must not be negative");
            if (l_oM.m_fRate * l_oM.m_oLife.m_fMax > l_oM.m_iMaxParticles * 1.05f)
                l_fnWarn(l_strW, "rate x life (" + std::to_string((int)std::ceil(l_oM.m_fRate * l_oM.m_oLife.m_fMax)) + ") is more than maxParticles (" +
                            std::to_string(l_oM.m_iMaxParticles) + "): some particles will not be born");
            for (const Burst& l_oB : l_oM.m_vBursts) {
                if (l_oB.m_oCount.m_fMax > l_oM.m_iMaxParticles) l_fnWarn(l_strW, "a burst is bigger than maxParticles");
                if (l_oB.m_iRepeat != 0 && l_oB.m_fInterval <= 0) l_fnErr(l_strW, "a repeating burst needs an interval above 0");
            }
            if (l_oM.m_fRate <= 0 && l_oM.m_vBursts.empty()) l_fnWarn(l_strW, "no rate and no bursts: emits nothing");
            auto l_fnKeysIn01 = [&](const char* l_pszName, const std::vector<float>& l_vTs) {
                for (size_t l_nI = 0; l_nI < l_vTs.size(); l_nI++) {
                    if (l_vTs[l_nI] < 0 || l_vTs[l_nI] > 1) l_fnErr(l_strW, std::string("overLife.") + l_pszName + ": key t must be 0..1");
                    if (l_nI && l_vTs[l_nI] == l_vTs[l_nI - 1]) l_fnErr(l_strW, std::string("overLife.") + l_pszName + ": two keys at the same t");
                }
            };
            auto l_fnTimes = [](const auto& l_oC) { std::vector<float> l_vT; for (const auto& l_oK : l_oC.m_vKeys) l_vT.push_back(l_oK.m_fT); return l_vT; };
            l_fnKeysIn01("color", l_fnTimes(l_oM.m_oColorOverLife));
            l_fnKeysIn01("size", l_fnTimes(l_oM.m_oSizeOverLife));
            l_fnKeysIn01("speed", l_fnTimes(l_oM.m_oSpeedOverLife));
            l_fnKeysIn01("spin", l_fnTimes(l_oM.m_oSpinOverLife));
            if (l_oM.m_strSprite.empty() && l_oM.m_vFrames.empty()) l_fnErr(l_strW, "no sprite and no flipbook frames: draws nothing");
            if (l_oM.m_bFramesByFps && l_oM.m_fFps <= 0) l_fnErr(l_strW, "flipbook fps must be above 0");
            for (Blend::Factor l_eFac : {l_oM.m_oBlend.m_eSrc, l_oM.m_oBlend.m_eDst})
                if (l_eFac == Blend::DstAlpha || l_eFac == Blend::InvDstAlpha)
                    l_fnWarn(l_strW, "blend: the screen has no alpha, dstAlpha factors act like one / zero");
            if (l_pAtlases) {
                std::vector<std::string> l_vRefs = l_oM.m_vFrames;
                if (!l_oM.m_strSprite.empty()) l_vRefs.push_back(l_oM.m_strSprite);
                for (const std::string& l_strR : l_vRefs)
                    if (!l_pAtlases->f_Find(l_strR)) l_fnErr(l_strW, "sprite '" + l_strR + "' is in no atlas");
            }
        }
    }
    return l_vOut;
}

// ---- EffectInstance ----------------------------------------------------------------------------

void EffectInstance::Pool::f_Reserve(int l_iN) {
    m_vPos.resize((size_t)l_iN); m_vVel.resize((size_t)l_iN);
    m_vAge.resize((size_t)l_iN); m_vLife.resize((size_t)l_iN); m_vSize.resize((size_t)l_iN); m_vRot.resize((size_t)l_iN); m_vSpin.resize((size_t)l_iN);
    m_vRadial.resize((size_t)l_iN); m_vTangential.resize((size_t)l_iN);
    m_vColor.resize((size_t)l_iN); m_vFrame.resize((size_t)l_iN);
}

uint32_t EffectInstance::f_Next() {   // splitmix32
    uint32_t l_uZ = (m_uRng += 0x9E3779B9u);
    l_uZ = (l_uZ ^ (l_uZ >> 16)) * 0x85EBCA6Bu;
    l_uZ = (l_uZ ^ (l_uZ >> 13)) * 0xC2B2AE35u;
    return l_uZ ^ (l_uZ >> 16);
}

float EffectInstance::f_Rand01() { return float(f_Next() >> 8) * (1.0f / 16777216.0f); }

void EffectInstance::f_Play(const Effect* l_pEffect, uint32_t l_uSeed) {
    m_pEffect = l_pEffect;
    m_vPools.clear();
    m_vDrawOrder.clear();
    m_vGpu.clear();   // the previous play's GPU buffers go now (also for play(nullptr))
    m_fTime = 0;
    m_fCarry = 0;
    m_bStopped = false;
    if (!l_pEffect) return;
    if (l_uSeed == 0) l_uSeed = l_pEffect->m_uSeed;
    if (l_uSeed == 0) {
        static uint32_t l_uCounter = 0;
        l_uSeed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ (++l_uCounter * 0x9E3779B9u);
        if (l_uSeed == 0) l_uSeed = 1;
    }
    m_uSeed = l_uSeed;
    m_uRng = l_uSeed;
    m_i64StepIndex = 0;
    m_vPools.resize(l_pEffect->m_vEmitters.size());
    m_vGpu.assign(l_pEffect->m_vEmitters.size(), nullptr);
    for (size_t l_nI = 0; l_nI < m_vPools.size(); l_nI++) {
        const Emitter& l_oEm = l_pEffect->m_vEmitters[l_nI];
        m_vPools[l_nI].m_vBurstsDone.assign(l_oEm.m_vBursts.size(), 0);
        m_vDrawOrder.push_back(l_nI);
        using Sim = Emitter::Simulation;
        const bool l_bWantGpu = l_oEm.m_eSimulation == Sim::Gpu ||
                             (l_oEm.m_eSimulation == Sim::Auto && m_iGpuThreshold > 0 && l_oEm.m_iMaxParticles > m_iGpuThreshold);
        if (l_bWantGpu && m_pGpuRen && m_pGpuRen->f_SupportsGpuParticles()) f_StartGpu(l_nI);   // else (no compute) the CPU
        if (!m_vGpu[l_nI]) m_vPools[l_nI].f_Reserve(std::max(1, l_oEm.m_iMaxParticles));   // a GPU emitter's particles live on the GPU
    }
    std::stable_sort(m_vDrawOrder.begin(), m_vDrawOrder.end(),
                     [&](size_t l_nA, size_t l_nB) { return l_pEffect->m_vEmitters[l_nA].m_iOrder < l_pEffect->m_vEmitters[l_nB].m_iOrder; });
    for (int l_iN = (int)std::lround(l_pEffect->m_fPrewarm / m_fStep); l_iN > 0; l_iN--) f_Step(m_fStep);
}

void EffectInstance::f_StartGpu(size_t l_nEi) {
    const Emitter& l_oEm = m_pEffect->m_vEmitters[l_nEi];
    auto l_spG = std::make_shared<Gpu>();
    l_spG->m_pRen = m_pGpuRen;
    l_spG->m_iCapacity = std::max(1, l_oEm.m_iMaxParticles);
    l_spG->m_uId = m_pGpuRen->f_CreateGpuParticles(uint32_t(l_spG->m_iCapacity));
    if (!l_spG->m_uId) {   // no buffers: this emitter stays on the CPU
        l_spG->m_pRen = nullptr;
        m_vGpu[l_nEi] = nullptr;
        return;
    }
    l_spG->m_vFreeSlots.resize(size_t(l_spG->m_iCapacity));
    for (int l_iS = 0; l_iS < l_spG->m_iCapacity; l_iS++) l_spG->m_vFreeSlots[size_t(l_iS)] = l_spG->m_iCapacity - 1 - l_iS;   // slot 0 first
    // The curves as tables over the life (the GPU interpolates between entries).
    constexpr uint32_t l_uN = GpuParticleFrame::m_uLut;
    l_spG->m_vColorLut.resize(l_uN * 4);
    l_spG->m_vScalarLut.resize(l_uN * 4);
    for (uint32_t l_uI = 0; l_uI < l_uN; l_uI++) {
        const float l_fU = float(l_uI) / float(l_uN - 1);
        const glm::vec4 l_v4Col = l_oEm.m_oColorOverLife.f_At(l_fU, glm::vec4(1.0f));
        for (int l_iK = 0; l_iK < 4; l_iK++) l_spG->m_vColorLut[l_uI * 4 + l_iK] = l_v4Col[l_iK];
        l_spG->m_vScalarLut[l_uI * 4 + 0] = l_oEm.m_oSizeOverLife.f_At(l_fU, 1.0f);
        l_spG->m_vScalarLut[l_uI * 4 + 1] = l_oEm.m_oSpeedOverLife.f_At(l_fU, 1.0f);
        l_spG->m_vScalarLut[l_uI * 4 + 2] = l_oEm.m_oSpinOverLife.f_At(l_fU, 1.0f);
        l_spG->m_vScalarLut[l_uI * 4 + 3] = 0;
    }
    m_vGpu[l_nEi] = l_spG;
}

int EffectInstance::f_GpuEmitters() const {
    int l_iN = 0;
    for (const auto& l_spG : m_vGpu) l_iN += l_spG != nullptr;
    return l_iN;
}

void EffectInstance::f_Stop(bool l_bClear) {
    m_bStopped = true;
    if (l_bClear) {
        for (Pool& l_oP : m_vPools) l_oP.m_iCount = 0;
        for (size_t l_nI = 0; l_nI < m_vGpu.size(); l_nI++)
            if (m_vGpu[l_nI]) f_StartGpu(l_nI);   // fresh, empty buffers
    }
}

void EffectInstance::f_Update(float l_fDt) {
    if (!m_pEffect || l_fDt <= 0) return;
    m_fCarry += l_fDt;
    int l_iSteps = int(m_fCarry / m_fStep + 1e-3f);   // 60 x (1/60) is 60 steps despite float error
    if (l_iSteps > m_iMaxSteps) {   // a long hitch: simulate a little, drop the rest
        l_iSteps = m_iMaxSteps;
        m_fCarry = 0;
    } else {
        m_fCarry -= l_iSteps * m_fStep;
    }
    while (l_iSteps-- > 0) f_Step(m_fStep);
}

void EffectInstance::f_Seek(float l_fT) {
    const Effect* l_pE = m_pEffect;
    const uint32_t l_uS = m_uSeed;
    const glm::mat3 l_mat3M = m_mat3Transform;
    m_mat3Transform = l_mat3M;
    f_Play(l_pE, l_uS);   // starts at the prewarm time
    for (int l_iN = (int)std::lround((l_fT - m_fTime) / m_fStep); l_iN > 0; l_iN--) f_Step(m_fStep);
}

bool EffectInstance::f_Emitting() const {
    if (!m_pEffect || m_bStopped) return false;
    if (m_pEffect->m_fDuration <= 0 || m_pEffect->m_bLoop) return true;
    return m_fTime < m_pEffect->m_fDuration;
}

bool EffectInstance::f_Finished() const { return !f_Emitting() && f_LiveCount() == 0; }

int EffectInstance::f_LiveCount() const {
    int l_iN = 0;
    for (size_t l_nI = 0; l_nI < m_vPools.size(); l_nI++) l_iN += f_LiveCount(l_nI);
    return l_iN;
}

int EffectInstance::f_LiveCount(size_t l_nEmitter) const {
    if (l_nEmitter >= m_vPools.size()) return 0;
    if (const Gpu* l_pG = m_vGpu[l_nEmitter].get()) return l_pG->m_iCapacity - int(l_pG->m_vFreeSlots.size());
    return m_vPools[l_nEmitter].m_iCount;
}

namespace {

glm::vec2 f_Rotate(const glm::vec2& l_v2V, float l_fDegrees) {
    const float l_fR = glm::radians(l_fDegrees), l_fC = std::cos(l_fR), l_fS = std::sin(l_fR);
    return {l_v2V.x * l_fC - l_v2V.y * l_fS, l_v2V.x * l_fS + l_v2V.y * l_fC};
}
glm::vec2 f_Apply(const glm::mat3& l_mat3M, const glm::vec2& l_v2P) { const glm::vec3 l_v3V = l_mat3M * glm::vec3(l_v2P, 1); return {l_v3V.x, l_v3V.y}; }
glm::vec2 f_ApplyLinear(const glm::mat3& l_mat3M, const glm::vec2& l_v2D) { const glm::vec3 l_v3V = l_mat3M * glm::vec3(l_v2D, 0); return {l_v3V.x, l_v3V.y}; }

}  // namespace

void EffectInstance::f_Spawn(size_t l_nEi, int l_iN) {
    const Emitter& l_oEm = m_pEffect->m_vEmitters[l_nEi];
    Pool& l_oP = m_vPools[l_nEi];
    Gpu* l_pGpu = m_vGpu[l_nEi].get();
    l_iN = std::min(l_iN, l_pGpu ? int(l_pGpu->m_vFreeSlots.size()) : std::max(1, l_oEm.m_iMaxParticles) - l_oP.m_iCount);
    const Shape& l_oSh = l_oEm.m_oShape;
    for (; l_iN > 0; l_iN--) {
        // Where on the shape, and the direction it leaves in.
        glm::vec2 l_v2At{0, 0};
        float l_fOutAngle = 0;
        bool l_bHasOut = false;
        switch (l_oSh.m_eType) {
        case Shape::Point:
            break;
        case Shape::Line:
            l_v2At = f_Rotate({(f_Rand01() - 0.5f) * l_oSh.m_fLength, 0}, l_oSh.m_fAngle);
            break;
        case Shape::Box:
            if (l_oSh.m_bEdge) {
                const float l_fPer = 2 * (l_oSh.m_fWidth + l_oSh.m_fHeight);
                float l_fD = f_Rand01() * l_fPer;
                if (l_fD < l_oSh.m_fWidth) l_v2At = {l_fD - l_oSh.m_fWidth / 2, -l_oSh.m_fHeight / 2};
                else if ((l_fD -= l_oSh.m_fWidth) < l_oSh.m_fHeight) l_v2At = {l_oSh.m_fWidth / 2, l_fD - l_oSh.m_fHeight / 2};
                else if ((l_fD -= l_oSh.m_fHeight) < l_oSh.m_fWidth) l_v2At = {l_oSh.m_fWidth / 2 - l_fD, l_oSh.m_fHeight / 2};
                else l_v2At = {-l_oSh.m_fWidth / 2, l_oSh.m_fHeight / 2 - (l_fD - l_oSh.m_fWidth)};
            } else {
                l_v2At = {(f_Rand01() - 0.5f) * l_oSh.m_fWidth, (f_Rand01() - 0.5f) * l_oSh.m_fHeight};
            }
            break;
        case Shape::Circle:
        case Shape::Ring: {
            const float l_fA = l_oSh.m_fArcFrom + (l_oSh.m_fArcTo - l_oSh.m_fArcFrom) * f_Rand01();
            const float l_fR = l_oSh.m_eType == Shape::Ring ? l_oSh.m_fRadius - l_oSh.m_fThickness * f_Rand01()
                          : l_oSh.m_bEdge             ? l_oSh.m_fRadius
                                                : l_oSh.m_fRadius * std::sqrt(f_Rand01());
            l_v2At = f_Rotate({l_fR, 0}, l_fA);
            l_fOutAngle = l_fA;
            l_bHasOut = true;
            break;
        }
        }
        float l_fAngle = l_oEm.m_fDirection;
        if (l_oSh.m_bOutward) {
            if (l_bHasOut) l_fAngle = l_fOutAngle;
            else if (l_v2At != glm::vec2(0)) l_fAngle = glm::degrees(std::atan2(l_v2At.y, l_v2At.x));
            else l_fAngle = f_Rand01() * 360.0f;   // a point: every direction
        }
        if (l_oEm.m_fSpread != 0) l_fAngle += l_oEm.m_fSpread * (f_Rand01() * 2 - 1);
        glm::vec2 l_v2Vel = f_Rotate({f_Pick(l_oEm.m_oSpeed), 0}, l_fAngle);
        glm::vec2 l_v2Pos = l_oEm.m_v2Offset + l_v2At;
        if (!l_oEm.m_bLocalSpace) {   // born into the world where the effect is now
            l_v2Pos = f_Apply(m_mat3Transform, l_v2Pos);
            l_v2Vel = f_ApplyLinear(m_mat3Transform, l_v2Vel);
        }
        // The same random values in the same order on both paths: a GPU emitter's particles are
        // the ones the CPU would have made.
        const float l_fLife = std::max(0.001f, f_Pick(l_oEm.m_oLife));
        const float l_fSize = f_Pick(l_oEm.m_oSize), l_fRot = f_Pick(l_oEm.m_oRotation), l_fSpin = f_Pick(l_oEm.m_oSpin);
        const float l_fRadial = f_Pick(l_oEm.m_oRadial), l_fTangential = f_Pick(l_oEm.m_oTangential);
        const glm::vec4 l_v4Color = l_oEm.m_v4Color == l_oEm.m_v4Color2 ? l_oEm.m_v4Color : l_oEm.m_v4Color + (l_oEm.m_v4Color2 - l_oEm.m_v4Color) * f_Rand01();
        const int l_iFrame = l_oEm.m_bRandomFrame && !l_oEm.m_vFrames.empty() ? int(f_Next() % (uint32_t)l_oEm.m_vFrames.size()) : 0;
        if (l_pGpu) {   // a record for the GPU, into a free slot
            const int l_iSlot = l_pGpu->m_vFreeSlots.back();
            l_pGpu->m_vFreeSlots.pop_back();
            // Born in this step, after the steps already waiting for the next draw: it skips those.
            const float l_fAge = -float(l_pGpu->m_uPendingSteps) * m_fStep;
            const float l_arrRec[20] = {l_v2Pos.x, l_v2Pos.y, l_v2Vel.x, l_v2Vel.y, l_fAge, l_fLife, l_fSize, l_fRot, l_fSpin, l_fRadial, l_fTangential, float(l_iFrame),
                                   l_v4Color.r, l_v4Color.g, l_v4Color.b, l_v4Color.a, float(l_iSlot), 0, 0, 0};
            l_pGpu->m_vSpawn.insert(l_pGpu->m_vSpawn.end(), l_arrRec, l_arrRec + 20);
            // It dies in the step where its age reaches its life: the k-th step of its life, k = ceil(life / dt).
            const long long l_i64K = std::max(1LL, (long long)std::ceil(l_fLife / m_fStep - 1e-4));
            l_pGpu->m_oFrees.push({m_i64StepIndex + l_i64K - 1, l_iSlot});
            continue;
        }
        const int l_iI = l_oP.m_iCount++;
        l_oP.m_vPos[(size_t)l_iI] = l_v2Pos;
        l_oP.m_vVel[(size_t)l_iI] = l_v2Vel;
        l_oP.m_vAge[(size_t)l_iI] = 0;
        l_oP.m_vLife[(size_t)l_iI] = l_fLife;
        l_oP.m_vSize[(size_t)l_iI] = l_fSize;
        l_oP.m_vRot[(size_t)l_iI] = l_fRot;
        l_oP.m_vSpin[(size_t)l_iI] = l_fSpin;
        l_oP.m_vRadial[(size_t)l_iI] = l_fRadial;
        l_oP.m_vTangential[(size_t)l_iI] = l_fTangential;
        l_oP.m_vColor[(size_t)l_iI] = l_v4Color;
        l_oP.m_vFrame[(size_t)l_iI] = l_iFrame;
    }
}

void EffectInstance::f_Step(float l_fDt) {
    const Effect& l_oFx = *m_pEffect;
    const float l_fT0 = m_fTime, l_fT1 = m_fTime + l_fDt;
    const float l_fEffectEnd = l_oFx.m_fDuration > 0 ? l_oFx.m_fDuration : 1e30f;
    for (size_t l_nEi = 0; l_nEi < l_oFx.m_vEmitters.size(); l_nEi++) {
        const Emitter& l_oEm = l_oFx.m_vEmitters[l_nEi];
        Pool& l_oP = m_vPools[l_nEi];
        Gpu* l_pGpu = m_vGpu[l_nEi].get();
        if (l_pGpu)   // the slots of particles that die in this step take newborns again
            while (!l_pGpu->m_oFrees.empty() && l_pGpu->m_oFrees.top().first <= m_i64StepIndex) {
                l_pGpu->m_vFreeSlots.push_back(l_pGpu->m_oFrees.top().second);
                l_pGpu->m_oFrees.pop();
            }
        // Emission within the emitter's active window [start, end).
        if (!m_bStopped) {
            const float l_fEnd = std::min(l_fEffectEnd, l_oEm.m_fStop > 0 ? l_oEm.m_fStop : 1e30f);
            const float l_fA = std::max(l_fT0, l_oEm.m_fStart), l_fB = std::min(l_fT1, l_fEnd);
            if (l_fB > l_fA && l_oEm.m_fRate > 0) {
                // The step's length, trimmed only at the window's edges (t1 - t0 drifts with the clock).
                double l_dInside = l_fDt;
                if (l_oEm.m_fStart > l_fT0) l_dInside -= l_oEm.m_fStart - l_fT0;
                if (l_fEnd < l_fT1) l_dInside -= l_fT1 - l_fEnd;
                l_oP.m_dSpawnCarry += double(l_oEm.m_fRate) * l_dInside;
                const int l_iN = int(l_oP.m_dSpawnCarry + 1e-6);   // 60 x (10/60) must give 10, not 9.9999
                l_oP.m_dSpawnCarry -= l_iN;
                f_Spawn(l_nEi, l_iN);
            }
            for (size_t l_nBi = 0; l_nBi < l_oEm.m_vBursts.size(); l_nBi++) {
                const Burst& l_oBu = l_oEm.m_vBursts[l_nBi];
                const bool l_bRepeats = l_oBu.m_iRepeat != 0 && l_oBu.m_fInterval > 0;
                for (;;) {
                    const int l_iK = l_oP.m_vBurstsDone[l_nBi];
                    if (l_iK > 0 && (!l_bRepeats || (l_oBu.m_iRepeat > 0 && l_iK > l_oBu.m_iRepeat))) break;
                    const float l_fAt = l_oEm.m_fStart + l_oBu.m_fT + (l_bRepeats ? l_iK * l_oBu.m_fInterval : 0.0f);
                    if (l_fAt >= l_fT1 || l_fAt >= l_fEnd) break;
                    l_oP.m_vBurstsDone[l_nBi]++;
                    if (l_fAt >= l_fT0 || l_iK == 0) f_Spawn(l_nEi, (int)std::lround(f_Pick(l_oBu.m_oCount)));
                }
            }
        }
        if (l_pGpu) {   // the GPU simulates it, at the next draw
            l_pGpu->m_uPendingSteps++;
            continue;
        }
        // Simulation; dead particles leave, the rest keep their birth order.
        const glm::vec2 l_v2Origin = l_oEm.m_bLocalSpace ? l_oEm.m_v2Offset : f_Apply(m_mat3Transform, l_oEm.m_v2Offset);
        const float l_fDragMul = l_oEm.m_fDrag > 0 ? std::exp(-l_oEm.m_fDrag * l_fDt) : 1.0f;
        int l_iW = 0;
        for (int l_iI = 0; l_iI < l_oP.m_iCount; l_iI++) {
            const size_t l_nS = (size_t)l_iI;
            const float l_fAge = l_oP.m_vAge[l_nS] + l_fDt;
            if (l_fAge >= l_oP.m_vLife[l_nS]) continue;
            const float l_fU = l_fAge / l_oP.m_vLife[l_nS];
            glm::vec2 l_v2V = l_oP.m_vVel[l_nS] + l_oEm.m_v2Gravity * l_fDt;
            if (l_oP.m_vRadial[l_nS] != 0 || l_oP.m_vTangential[l_nS] != 0) {
                const glm::vec2 l_v2D = l_oP.m_vPos[l_nS] - l_v2Origin;
                const float l_fLen = std::sqrt(l_v2D.x * l_v2D.x + l_v2D.y * l_v2D.y);
                if (l_fLen > 1e-4f) {
                    const glm::vec2 l_v2N = l_v2D / l_fLen;
                    l_v2V += (l_v2N * l_oP.m_vRadial[l_nS] + glm::vec2(-l_v2N.y, l_v2N.x) * l_oP.m_vTangential[l_nS]) * l_fDt;   // (-y, x): clockwise, y down
                }
            }
            l_v2V *= l_fDragMul;
            const size_t l_nO = (size_t)l_iW++;
            l_oP.m_vVel[l_nO] = l_v2V;
            l_oP.m_vPos[l_nO] = l_oP.m_vPos[l_nS] + l_v2V * l_oEm.m_oSpeedOverLife.f_At(l_fU, 1.0f) * l_fDt;
            l_oP.m_vRot[l_nO] = l_oP.m_vRot[l_nS] + l_oP.m_vSpin[l_nS] * l_oEm.m_oSpinOverLife.f_At(l_fU, 1.0f) * l_fDt;
            l_oP.m_vAge[l_nO] = l_fAge;
            if (l_nO != l_nS) {
                l_oP.m_vLife[l_nO] = l_oP.m_vLife[l_nS]; l_oP.m_vSize[l_nO] = l_oP.m_vSize[l_nS]; l_oP.m_vSpin[l_nO] = l_oP.m_vSpin[l_nS];
                l_oP.m_vRadial[l_nO] = l_oP.m_vRadial[l_nS]; l_oP.m_vTangential[l_nO] = l_oP.m_vTangential[l_nS]; l_oP.m_vColor[l_nO] = l_oP.m_vColor[l_nS]; l_oP.m_vFrame[l_nO] = l_oP.m_vFrame[l_nS];
            }
        }
        l_oP.m_iCount = l_iW;
    }
    m_fTime = l_fT1;
    m_i64StepIndex++;
    if (l_oFx.m_bLoop && l_oFx.m_fDuration > 0 && m_fTime >= l_oFx.m_fDuration) {   // start over: bursts fire again
        m_fTime -= l_oFx.m_fDuration;
        for (Pool& l_oP : m_vPools) std::fill(l_oP.m_vBurstsDone.begin(), l_oP.m_vBurstsDone.end(), 0);
    }
}

void EffectInstance::f_AppendQuads(const toms::anim::AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, const float l_arrTint[4],
                                 std::vector<Quad>& l_vOut, std::vector<std::string>* l_pMissing) const {
    if (!m_pEffect) return;
    const glm::vec4 l_v4TintV = l_arrTint ? glm::vec4(l_arrTint[0], l_arrTint[1], l_arrTint[2], l_arrTint[3]) : glm::vec4(1.0f);
    for (size_t l_nEi : m_vDrawOrder)
        if (!m_vGpu[l_nEi]) f_AppendEmitterQuads(l_nEi, l_oAtlases, l_mat3Placement, l_v4TintV, l_vOut, l_pMissing);
}

void EffectInstance::f_AppendEmitterQuads(size_t l_nEi, const toms::anim::AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement,
                                        const glm::vec4& l_v4TintV, std::vector<Quad>& l_vOut, std::vector<std::string>* l_pMissing) const {
    auto l_fnReport = [&](const std::string& l_strRef) {
        if (l_pMissing && std::find(l_pMissing->begin(), l_pMissing->end(), l_strRef) == l_pMissing->end()) l_pMissing->push_back(l_strRef);
    };
    struct Look { const toms::AtlasRegion* m_pR = nullptr; uint16_t m_u16Tex = g_u16SpriteAtlasTexture; };
    std::vector<Look> l_vLooks;
    {
        const Emitter& l_oEm = m_pEffect->m_vEmitters[l_nEi];
        const Pool& l_oP = m_vPools[l_nEi];
        if (l_oP.m_iCount == 0) return;
        // The sprites of this emitter, looked up once per draw.
        const std::vector<std::string> l_vSingle{l_oEm.m_strSprite};
        const std::vector<std::string>& l_vRefs = l_oEm.m_vFrames.empty() ? l_vSingle : l_oEm.m_vFrames;
        for (const std::string& l_strRef : l_vRefs) {
            Look l_oL;
            l_oL.m_pR = l_oAtlases.f_Find(l_strRef, &l_oL.m_u16Tex);
            if (!l_oL.m_pR) l_fnReport(l_strRef);
            l_vLooks.push_back(l_oL);
        }
        const glm::mat3 l_mat3Base = l_oEm.m_bLocalSpace ? l_mat3Placement * m_mat3Transform : l_mat3Placement;
        const bool l_bAdditive = l_oEm.m_oBlend == Blend::f_Add();
        for (int l_iK = 0; l_iK < l_oP.m_iCount; l_iK++) {
            const size_t l_nI = (size_t)(l_oEm.m_bOldestOnTop ? l_oP.m_iCount - 1 - l_iK : l_iK);
            const float l_fU = l_oP.m_vAge[l_nI] / l_oP.m_vLife[l_nI];
            size_t l_nF = 0;
            if (l_vLooks.size() > 1) {
                l_nF = l_oEm.m_bFramesByFps ? (size_t)(l_oP.m_vFrame[l_nI] + int(l_oP.m_vAge[l_nI] * l_oEm.m_fFps)) % l_vLooks.size()
                                   : (size_t)(l_oP.m_vFrame[l_nI] + std::min(int(l_fU * l_vLooks.size()), int(l_vLooks.size()) - 1)) % l_vLooks.size();
            }
            const Look& l_oLook = l_vLooks[l_nF];
            if (!l_oLook.m_pR || l_oLook.m_pR->m_iOrigW <= 0) continue;
            const glm::vec4 l_v4Col = l_oP.m_vColor[l_nI] * l_oEm.m_oColorOverLife.f_At(l_fU, glm::vec4(1.0f)) * l_v4TintV;
            if (l_v4Col.a <= 0.0f) continue;
            const float l_fSize = l_oP.m_vSize[l_nI] * l_oEm.m_oSizeOverLife.f_At(l_fU, 1.0f);
            if (l_fSize <= 0.0f) continue;
            const toms::AtlasRegion& l_oR = *l_oLook.m_pR;
            float l_fAngle = l_oP.m_vRot[l_nI];
            if (l_oEm.m_bAlignToVelocity && (l_oP.m_vVel[l_nI].x != 0 || l_oP.m_vVel[l_nI].y != 0)) l_fAngle += glm::degrees(std::atan2(l_oP.m_vVel[l_nI].y, l_oP.m_vVel[l_nI].x));
            // The packed pixels inside the original image, around its centre, scaled to `size` wide.
            const float l_f2 = l_fSize / float(l_oR.m_iOrigW);
            const float l_fX0 = (l_oR.m_iOffX - 0.5f * l_oR.m_iOrigW) * l_f2, l_fY0 = (l_oR.m_iOffY - 0.5f * l_oR.m_iOrigH) * l_f2;
            const float l_fX1 = l_fX0 + l_oR.m_iW * l_f2, l_fY1 = l_fY0 + l_oR.m_iH * l_f2;
            const float l_fRad = glm::radians(l_fAngle), l_fC = std::cos(l_fRad), l_fS = std::sin(l_fRad);
            glm::mat3 l_mat3Local(1.0f);
            l_mat3Local[0] = glm::vec3(l_fC, l_fS, 0);
            l_mat3Local[1] = glm::vec3(-l_fS, l_fC, 0);
            l_mat3Local[2] = glm::vec3(l_oP.m_vPos[l_nI], 1);
            const glm::mat3 l_mat3M = l_mat3Base * l_mat3Local;
            const glm::vec3 l_arrCs[4] = {l_mat3M * glm::vec3(l_fX0, l_fY0, 1), l_mat3M * glm::vec3(l_fX1, l_fY0, 1), l_mat3M * glm::vec3(l_fX1, l_fY1, 1),
                                     l_mat3M * glm::vec3(l_fX0, l_fY1, 1)};
            Quad l_oQ;
            l_oQ.m_bHasCorners = true;
            float l_fMinX = l_arrCs[0].x, l_fMinY = l_arrCs[0].y, l_fMaxX = l_arrCs[0].x, l_fMaxY = l_arrCs[0].y;
            for (int l_iV = 0; l_iV < 4; l_iV++) {
                l_oQ.m_arrCorners[l_iV * 2] = l_arrCs[l_iV].x;
                l_oQ.m_arrCorners[l_iV * 2 + 1] = l_arrCs[l_iV].y;
                l_fMinX = std::min(l_fMinX, l_arrCs[l_iV].x); l_fMaxX = std::max(l_fMaxX, l_arrCs[l_iV].x);
                l_fMinY = std::min(l_fMinY, l_arrCs[l_iV].y); l_fMaxY = std::max(l_fMaxY, l_arrCs[l_iV].y);
            }
            l_oQ.m_arrRect[0] = l_fMinX; l_oQ.m_arrRect[1] = l_fMinY; l_oQ.m_arrRect[2] = l_fMaxX - l_fMinX; l_oQ.m_arrRect[3] = l_fMaxY - l_fMinY;
            std::copy(l_oR.m_arrUv, l_oR.m_arrUv + 4, l_oQ.m_arrUv);
            l_oQ.m_arrTint[0] = l_v4Col.r; l_oQ.m_arrTint[1] = l_v4Col.g; l_oQ.m_arrTint[2] = l_v4Col.b; l_oQ.m_arrTint[3] = l_v4Col.a;
            l_oQ.m_bAdditive = l_bAdditive;   // other blend modes: phase 2b (docs/engine/07_PARTICLES.md 3a)
            l_oQ.m_u16Texture = l_oLook.m_u16Tex;
            l_vOut.push_back(l_oQ);
        }
    }
}

void EffectInstance::f_Draw(IRenderer* l_pRen, const toms::anim::AtlasSet& l_oAtlases, const glm::mat3& l_mat3Placement, const float l_arrTint[4],
                          std::vector<std::string>* l_pMissing) {
    if (!m_pEffect || !l_pRen) return;
    const glm::vec4 l_v4TintV = l_arrTint ? glm::vec4(l_arrTint[0], l_arrTint[1], l_arrTint[2], l_arrTint[3]) : glm::vec4(1.0f);
    std::vector<Quad> l_vQuads;
    for (size_t l_nEi : m_vDrawOrder) {
        Gpu* l_pG = m_vGpu[l_nEi].get();
        if (!l_pG) {
            l_vQuads.clear();
            f_AppendEmitterQuads(l_nEi, l_oAtlases, l_mat3Placement, l_v4TintV, l_vQuads, l_pMissing);
            for (const Quad& l_oQ : l_vQuads) l_pRen->f_DrawSprite(l_oQ);
            continue;
        }
        const Emitter& l_oEm = m_pEffect->m_vEmitters[l_nEi];
        // The sprite / flipbook frames: their uv rects and, for size 1, their rects around the centre.
        const std::vector<std::string> l_vSingle{l_oEm.m_strSprite};
        const std::vector<std::string>& l_vRefs = l_oEm.m_vFrames.empty() ? l_vSingle : l_oEm.m_vFrames;
        const uint32_t l_uFrames = std::min<uint32_t>((uint32_t)l_vRefs.size(), GpuParticleFrame::m_uMaxFrames);
        float l_arrUv[GpuParticleFrame::m_uMaxFrames * 4] = {}, l_arrRect[GpuParticleFrame::m_uMaxFrames * 4] = {};
        uint16_t l_u16Texture = g_u16SpriteAtlasTexture;
        for (uint32_t l_uF = 0; l_uF < l_uFrames; l_uF++) {
            uint16_t l_u16Tex = g_u16SpriteAtlasTexture;
            const toms::AtlasRegion* l_pR = l_oAtlases.f_Find(l_vRefs[l_uF], &l_u16Tex);
            if (!l_pR || l_pR->m_iOrigW <= 0) {   // nothing to draw for this frame (a zero rect)
                if (l_pMissing && std::find(l_pMissing->begin(), l_pMissing->end(), l_vRefs[l_uF]) == l_pMissing->end()) l_pMissing->push_back(l_vRefs[l_uF]);
                continue;
            }
            if (l_uF == 0) l_u16Texture = l_u16Tex;   // one texture per draw: frames on other pages draw from the first's
            std::copy(l_pR->m_arrUv, l_pR->m_arrUv + 4, l_arrUv + l_uF * 4);
            const float l_fK = 1.0f / float(l_pR->m_iOrigW);
            l_arrRect[l_uF * 4 + 0] = (l_pR->m_iOffX - 0.5f * l_pR->m_iOrigW) * l_fK;
            l_arrRect[l_uF * 4 + 1] = (l_pR->m_iOffY - 0.5f * l_pR->m_iOrigH) * l_fK;
            l_arrRect[l_uF * 4 + 2] = l_arrRect[l_uF * 4 + 0] + l_pR->m_iW * l_fK;
            l_arrRect[l_uF * 4 + 3] = l_arrRect[l_uF * 4 + 1] + l_pR->m_iH * l_fK;
        }
        GpuParticleFrame l_oFr;
        l_oFr.m_uEmitter = l_pG->m_uId;
        l_oFr.m_pSpawn = l_pG->m_vSpawn.data();
        l_oFr.m_uSpawnCount = uint32_t(l_pG->m_vSpawn.size() / 20);
        l_oFr.m_uSteps = l_pG->m_uPendingSteps;
        l_oFr.m_fDt = m_fStep;
        l_oFr.m_arrGravity[0] = l_oEm.m_v2Gravity.x;
        l_oFr.m_arrGravity[1] = l_oEm.m_v2Gravity.y;
        l_oFr.m_fDragPerStep = l_oEm.m_fDrag > 0 ? std::exp(-l_oEm.m_fDrag * m_fStep) : 1.0f;
        const glm::vec2 l_v2Origin = l_oEm.m_bLocalSpace ? l_oEm.m_v2Offset : f_Apply(m_mat3Transform, l_oEm.m_v2Offset);
        l_oFr.m_arrOrigin[0] = l_v2Origin.x;
        l_oFr.m_arrOrigin[1] = l_v2Origin.y;
        l_oFr.m_bAlignToVelocity = l_oEm.m_bAlignToVelocity;
        l_oFr.m_pColorLut = l_pG->m_vColorLut.data();
        l_oFr.m_pScalarLut = l_pG->m_vScalarLut.data();
        l_oFr.m_uFrameCount = std::max(1u, l_uFrames);
        l_oFr.m_fFps = l_oEm.m_bFramesByFps && l_uFrames > 1 ? l_oEm.m_fFps : 0.0f;
        l_oFr.m_pFrameUv = l_arrUv;
        l_oFr.m_pFrameRect = l_arrRect;
        const glm::mat3 l_mat3M = l_oEm.m_bLocalSpace ? l_mat3Placement * m_mat3Transform : l_mat3Placement;
        l_oFr.m_arrXform[0] = l_mat3M[0][0]; l_oFr.m_arrXform[1] = l_mat3M[1][0]; l_oFr.m_arrXform[2] = l_mat3M[2][0];
        l_oFr.m_arrXform[3] = l_mat3M[0][1]; l_oFr.m_arrXform[4] = l_mat3M[1][1]; l_oFr.m_arrXform[5] = l_mat3M[2][1];
        for (int l_iK = 0; l_iK < 4; l_iK++) l_oFr.m_arrTint[l_iK] = l_v4TintV[l_iK];
        l_oFr.m_u16Texture = l_u16Texture;
        l_oFr.m_bAdditive = l_oEm.m_oBlend == Blend::f_Add();
        l_pRen->f_DrawGpuParticles(l_oFr);
        l_pG->m_vSpawn.clear();
        l_pG->m_uPendingSteps = 0;
    }
}
}  // namespace toms::fx
