#include "fighter_view.h"
#include <algorithm>
#include "resource_types.h"

namespace toms {

AnimFighterView::AnimFighterView(std::shared_ptr<res::AnimRes> l_spCharacter, std::shared_ptr<res::AnimRes> l_spHeld,
                                 Fit l_fnFit, float l_fUnitsTall, bool l_bBoxSide)
    : m_spCharacter(std::move(l_spCharacter)), m_spHeld(std::move(l_spHeld)), m_fnFit(std::move(l_fnFit)),
      m_fUnits(l_fUnitsTall > 0.0f ? l_fUnitsTall : 32.0f), m_bBoxSide(l_bBoxSide) {}

bool AnimFighterView::f_Has(const std::string& l_strClip) const {
    return (m_spCharacter && m_spCharacter->find(l_strClip)) || (m_spHeld && m_spHeld->find(l_strClip));
}

bool AnimFighterView::f_Play(const std::string& l_strClip, bool l_bRestart) {
    if (!f_Has(l_strClip)) return false;
    if (l_strClip == m_strClip && !l_bRestart) return true;
    const anim::Clip* l_pBody = m_spCharacter ? m_spCharacter->find(l_strClip) : nullptr;
    if (l_pBody) {
        m_oFitted = m_fnFit ? m_fnFit(*l_pBody) : *l_pBody;
        l_pBody = &m_oFitted;
    }
    m_oRig.play(l_pBody, m_spHeld ? m_spHeld->find(l_strClip) : nullptr);
    m_strClip = l_strClip;
    return true;
}

void AnimFighterView::f_Stop() {
    m_oRig.stop();
    m_strClip.clear();
}

void AnimFighterView::f_Update(int l_iDtMs) {
    m_oRig.update(l_iDtMs);
    m_oRig.takeEvents();   // (release / hit / miss: the battle's own timers place the numbers)
}

void AnimFighterView::f_Draw(IRenderer* l_pRen, const glm::vec4& l_v4Box) const {
    static const anim::AtlasSet l_oNone;
    if (m_strClip.empty()) return;
    const float l_fFill = m_bBoxSide ? std::min(l_v4Box.z, l_v4Box.w) : l_v4Box.w;
    m_oRig.draw(l_pRen, m_spCharacter ? m_spCharacter->atlases() : l_oNone, m_spHeld ? m_spHeld->atlases() : l_oNone,
                anim::placement(l_v4Box.x + l_v4Box.z / 2.0f, l_v4Box.y + l_v4Box.w, l_fFill / m_fUnits));
}

}  // namespace toms
