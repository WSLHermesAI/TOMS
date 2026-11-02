#pragma once
// A fighter on the battle screen -- the player or a monster -- behind one interface, so how it is animated and drawn
// (2D clips from the anim editor, or a 3D glTF model) is the data's choice and the battle code stays the same
// (docs/gameplay/18_FIGHTER_VIEWS.md). Named by docs/project/12_CPP_NAMING.md.
//
// The battle asks a view for a clip by name ("idle", "charge", "attack", "attack_big", "miss", "hurt", "die", or a
// monster's own "attack_slime"), lets it run, asks whether it has finished, and draws it in the fighter's box (the feet
// at the box's bottom centre). Which file, which renderer and how the box is filled belong to the view.
//
// The data (FighterLook): items.json "battleAnim" for the player's weapon, looks.json for a monster's look:
//   "render": "2d" (default) | "3d"
//   2d: "character": ".anim" (+ "held": ".anim", drawn at the character's hand)
//   3d: "model": ".glb" (its animations by the same clip names)
// A 3D look is not drawn yet: the factory says so once and uses the look's 2D files.
#include <functional>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "anim_player.h"
#include "render_iface.h"

namespace toms {
namespace res { class AnimRes; }

struct FighterLook {
    std::string m_strRender = "2d";           // "2d" | "3d"
    std::string m_strCharacter, m_strHeld;    // 2d: the .anim files (paths under assets/media)
    std::string m_strModel;                   // 3d: the .glb
    bool f_Is3d() const { return m_strRender == "3d"; }
};

class FighterView {
public:
    virtual ~FighterView() = default;
    virtual bool f_Has(const std::string& l_strClip) const = 0;
    // Plays the clip (from the start when `l_bRestart` or when another clip is playing). False: no such clip (the view
    // keeps what it was playing).
    virtual bool f_Play(const std::string& l_strClip, bool l_bRestart) = 0;
    virtual void f_Stop() = 0;
    virtual const std::string& f_Clip() const = 0;   // "" = nothing playing
    virtual bool f_Finished() const = 0;             // a one-shot clip played out (a loop never is)
    virtual void f_Update(int l_iDtMs) = 0;
    // In the fighter's box (x, y, w, h: screen pixels), the feet at its bottom centre.
    virtual void f_Draw(IRenderer* l_pRen, const glm::vec4& l_v4Box) const = 0;
};

// 2D: a character clip (+ a held clip at its hand) from .anim files, the anim editor's (CharacterRig).
class AnimFighterView : public FighterView {
public:
    // l_fnFit: the clip made for any character, fitted to this one (Game::fitClipToCharacter: its sprite in the "body"
    // node); none = as drawn. l_fUnitsTall: the clip units drawn at the box's height (the player's clips: 32); the
    // monsters' instead fill the box with their sprite's longer side (l_bBoxSide).
    using Fit = std::function<anim::Clip(const anim::Clip&)>;
    AnimFighterView(std::shared_ptr<res::AnimRes> l_spCharacter, std::shared_ptr<res::AnimRes> l_spHeld, Fit l_fnFit,
                    float l_fUnitsTall, bool l_bBoxSide = false);
    bool f_Has(const std::string& l_strClip) const override;
    bool f_Play(const std::string& l_strClip, bool l_bRestart) override;
    void f_Stop() override;
    const std::string& f_Clip() const override { return m_strClip; }
    bool f_Finished() const override { return m_oRig.f_Finished(); }
    void f_Update(int l_iDtMs) override;
    void f_Draw(IRenderer* l_pRen, const glm::vec4& l_v4Box) const override;

private:
    std::shared_ptr<res::AnimRes> m_spCharacter, m_spHeld;
    Fit m_fnFit;
    float m_fUnits;
    bool m_bBoxSide;
    anim::Clip m_oFitted;
    anim::CharacterRig m_oRig;
    std::string m_strClip;
};

}  // namespace toms
