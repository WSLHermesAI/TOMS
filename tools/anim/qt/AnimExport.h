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
    Format format = Format::Gif;
    int fps = 30;                 // GIF: at most 50 (its delays are hundredths of a second)
    double scale = 0.5;           // of the clip's pixels
    QColor background = QColor(0, 0, 0, 0);   // alpha 0: transparent (GIF: 1-bit, edges hard)
    bool loop = true;             // play forever (else once)
    int padding = 16;             // clip pixels around what the clip draws
};

// The frames of a clip: one picture per 1/fps second over its length (one loop of a looping clip; a clip that plays
// once also gets its last frame). Its frame: what every frame draws, with the held clip at `hand` (a node bigger than
// 1500 px -- a screen-sized backdrop -- is left out of the framing, not of the pictures).
std::vector<QImage> renderFrames(AnimViewport* view, const AnimDocument& doc, const toms::anim::Clip& clip,
                                 const toms::anim::Clip* held, const Options& opt, QString* error,
                                 const std::function<bool(int done, int total)>& progress = {});

// Writers. false and `error` on failure.
bool writeGif(const QString& path, const std::vector<QImage>& frames, int fps, bool loop, QString* error);
bool writeApng(const QString& path, const std::vector<QImage>& frames, int fps, bool loop, QString* error);
bool writePngFrames(const QString& dir, const QString& base, const std::vector<QImage>& frames, QString* error);

// One clip into outDir: <file>_<clip>.gif / .png / <file>_<clip>_NNN.png. Returns the path written ("" on failure).
QString exportClip(AnimViewport* view, const AnimDocument& doc, const toms::anim::Clip& clip, const Options& opt,
                   const QString& outDir, QString* error, const std::function<bool(int, int)>& progress = {});

// File > Export Clips as Animation: which clips (checked), the format, frame rate, scale, background, loop, the folder.
class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    ExportDialog(AnimDocument* doc, AnimViewport* view, QWidget* parent = nullptr);

private:
    void run();
    AnimDocument* m_doc;
    AnimViewport* m_view;
    QListWidget* m_clips;
    QComboBox* m_format;
    QSpinBox* m_fps;
    QDoubleSpinBox* m_scale;
    QCheckBox *m_transparent, *m_loop, *m_held;
    QLineEdit* m_dir;
    QPushButton* m_bg;   // the background colour (its "colour" property)
};

}  // namespace animexport
