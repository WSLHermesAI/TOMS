#pragma once
// AnimExport -- clips as animated pictures (File > Export Clips as Animation; anim_editor --export): every frame drawn
// by the game renderer offscreen (GameCanvasView::renderOffscreen: skins, lattices, effect layers, HDR glow as the game
// draws them), framed once for the whole clip, written as an animated GIF, an animated PNG (APNG) or numbered PNGs.

#include "anim_clip.h"

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QString>

#include <functional>
#include <vector>

class AnimDocument;
class AnimViewport;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QListWidget;
class QSpinBox;

namespace animexport {

enum class Format { Gif, Apng, PngFrames };

struct Options {
    Format m_eFormat = Format::Gif;
    int m_iFps = 30;                 // GIF: at most 50 (its delays are hundredths of a second)
    double m_dScale = 0.5;           // of the clip's pixels
    QColor m_oBackground = QColor(0, 0, 0, 0);   // alpha 0: transparent (GIF: 1-bit, edges hard)
    bool m_bLoop = true;             // play forever (else once)
    int m_iPadding = 16;             // clip pixels around what the clip draws
};

// The frames of a clip: one picture per 1/fps second over its length (one loop of a looping clip; a clip that plays
// once also gets its last frame). Its frame: what every frame draws, with the held clip at `hand` (a node bigger than
// 1500 px -- a screen-sized backdrop -- is left out of the framing, not of the pictures).
std::vector<QImage> f_RenderFrames(AnimViewport* l_pView, const AnimDocument& l_oDoc, const toms::anim::Clip& l_oClip,
                                 const toms::anim::Clip* l_pHeld, const Options& l_oOpt, QString* l_pError,
                                 const std::function<bool(int l_iDone, int l_iTotal)>& l_fnProgress = {});

// Writers. false and `error` on failure.
bool f_WriteGif(const QString& l_oPath, const std::vector<QImage>& l_vFrames, int l_iFps, bool l_bLoop, QString* l_pError);
bool f_WriteApng(const QString& l_oPath, const std::vector<QImage>& l_vFrames, int l_iFps, bool l_bLoop, QString* l_pError);
bool f_WritePngFrames(const QString& l_oDir, const QString& l_oBase, const std::vector<QImage>& l_vFrames, QString* l_pError);

// One clip into outDir: <file>_<clip>.gif / .png / <file>_<clip>_NNN.png. Returns the path written ("" on failure).
QString f_ExportClip(AnimViewport* l_pView, const AnimDocument& l_oDoc, const toms::anim::Clip& l_oClip, const Options& l_oOpt,
                   const QString& l_oOutDir, QString* l_pError, const std::function<bool(int, int)>& l_fnProgress = {});

// File > Export Clips as Animation: which clips (checked), the format, frame rate, scale, background, loop, the folder.
class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    ExportDialog(AnimDocument* l_pDoc, AnimViewport* l_pView, QWidget* l_pParent = nullptr);

private:
    void f_Run();
    AnimDocument* m_pDoc;
    AnimViewport* m_pView;
    QListWidget* m_pClips;
    QComboBox* m_pFormat;
    QSpinBox* m_pFps;
    QDoubleSpinBox* m_pScale;
    QCheckBox *m_pTransparent, *m_pLoop, *m_pHeld;
    QLineEdit* m_pDir;
    QPushButton* m_pBg;   // the background colour (its "colour" property)
};

}  // namespace animexport
