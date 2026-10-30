// AnimExport.cpp -- see AnimExport.h.
#include "AnimExport.h"

#include "AnimDocument.h"
#include "AnimViewport.h"
#include "anim_player.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace animexport {

using toms::anim::Clip;
using toms::anim::NodePose;

namespace {

// ---- framing ----------------------------------------------------------------------------------------------------

// The frames' times: a looping clip one loop (its end is its start again), a clip that plays once up to its last frame.
std::vector<float> frameTimes(const Clip& clip, int fps) {
    const float d = clip.duration();
    std::vector<float> out;
    if (d <= 0 || fps <= 0) return {0.0f};
    const int n = std::max(1, int(std::lround(d * fps)));
    for (int i = 0; i < n; i++) out.push_back(float(i) / float(fps));
    if (clip.playCount >= 0) out.push_back(d);
    return out;
}

// What every frame draws, in clip pixels (quads' corners, meshes' vertices; the held clip at `hand`).
bool clipBounds(const Clip& clip, const Clip* held, const toms::anim::AtlasSet& atlases, const std::vector<float>& times,
                float& x0, float& y0, float& x1, float& y1) {
    bool any = false;
    auto grow = [&](float x, float y) {
        if (!any) { x0 = x1 = x; y0 = y1 = y; any = true; return; }
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
    };
    std::vector<NodePose> poses, heldPoses;
    toms::anim::DrawList d;
    for (float t : times) {
        d.clear();
        toms::anim::evaluate(clip, t, poses);
        toms::anim::appendDraws(clip, poses, atlases, glm::mat3(1.0f), nullptr, d);
        glm::mat3 hand;
        if (held && toms::anim::attachPlacement(poses, "hand", glm::mat3(1.0f), hand)) {
            toms::anim::evaluate(*held, std::min(t, held->duration()), heldPoses);
            toms::anim::appendDraws(*held, heldPoses, atlases, hand, nullptr, d);
        }
        for (const Quad& q : d.quads) {
            if (q.tint[3] <= 0.0f || q.rect[2] > 1500.0f || q.rect[3] > 1500.0f) continue;   // a screen-sized backdrop
            for (int k = 0; k < 4; k++) grow(q.corners[2 * k], q.corners[2 * k + 1]);
        }
        for (const toms::anim::DrawList::MeshItem& m : d.meshes) {
            if (m.tint[3] <= 0.0f) continue;
            for (uint32_t v = m.firstVertex; v < m.firstVertex + m.vertexCount; v++) grow(d.vertices[v].x, d.vertices[v].y);
        }
    }
    return any;
}

// ---- GIF ------------------------------------------------------------------------------------------------------------

// One palette for every frame (the colours do not flicker between frames): median cut over the opaque pixels' colours
// at 5 bits a channel, 255 colours (index 255: transparent).
std::vector<std::array<uint8_t, 3>> gifPalette(const std::vector<QImage>& frames, int colours) {
    std::vector<uint32_t> hist(32768, 0);
    for (const QImage& f : frames)
        for (int y = 0; y < f.height(); y++) {
            const uint8_t* p = f.constScanLine(y);
            for (int x = 0; x < f.width(); x++, p += 4)
                if (p[3] >= 128) hist[size_t((p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3))]++;
        }
    struct Box { std::vector<uint16_t> cells; };
    std::vector<Box> boxes(1);
    for (uint16_t c = 0; c < 32768; c++) if (hist[c]) boxes[0].cells.push_back(c);
    if (boxes[0].cells.empty()) return {{0, 0, 0}};
    auto channel = [](uint16_t c, int k) { return int(k == 0 ? c >> 10 : k == 1 ? (c >> 5) & 31 : c & 31); };
    while ((int)boxes.size() < colours) {
        // the box with the most pixels that can still be split, along its widest channel, at the pixels' median
        int best = -1;
        uint64_t bestCount = 0;
        for (size_t b = 0; b < boxes.size(); b++) {
            if (boxes[b].cells.size() < 2) continue;
            uint64_t n = 0;
            for (uint16_t c : boxes[b].cells) n += hist[c];
            if (n > bestCount) { bestCount = n; best = (int)b; }
        }
        if (best < 0) break;
        Box& box = boxes[(size_t)best];
        int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
        for (uint16_t c : box.cells)
            for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], channel(c, k)); hi[k] = std::max(hi[k], channel(c, k)); }
        int axis = 0;
        for (int k = 1; k < 3; k++) if (hi[k] - lo[k] > hi[axis] - lo[axis]) axis = k;
        std::sort(box.cells.begin(), box.cells.end(), [&](uint16_t a, uint16_t b) { return channel(a, axis) < channel(b, axis); });
        uint64_t acc = 0;
        size_t cut = 1;
        for (size_t i = 0; i < box.cells.size() - 1; i++) {
            acc += hist[box.cells[i]];
            cut = i + 1;
            if (acc * 2 >= bestCount) break;
        }
        Box other;
        other.cells.assign(box.cells.begin() + (long)cut, box.cells.end());
        box.cells.resize(cut);
        boxes.push_back(std::move(other));
    }
    std::vector<std::array<uint8_t, 3>> pal;
    for (const Box& b : boxes) {   // each box's colour: its pixels' average
        uint64_t n = 0, s[3] = {0, 0, 0};
        for (uint16_t c : b.cells)
            for (int k = 0; k < 3; k++) s[k] += uint64_t(channel(c, k) * 8 + 4) * hist[c];
        for (uint16_t c : b.cells) n += hist[c];
        pal.push_back({uint8_t(s[0] / n), uint8_t(s[1] / n), uint8_t(s[2] / n)});
    }
    return pal;
}

// GIF's LZW: variable-width codes (min code size 8), a clear code when the table is full.
void lzw(const std::vector<uint8_t>& idx, QByteArray& out) {
    const int minSize = 8, clear = 1 << minSize, eoi = clear + 1;
    std::vector<int16_t> next(4096 * 256, -1);   // (prefix code, byte) -> code
    int size = minSize + 1, nextCode = eoi + 1;
    uint32_t bits = 0;
    int nbits = 0;
    QByteArray block;
    auto put = [&](int code) {
        bits |= uint32_t(code) << nbits;
        nbits += size;
        while (nbits >= 8) {
            block.append(char(bits & 0xff));
            bits >>= 8;
            nbits -= 8;
            if (block.size() == 255) { out.append(char(255)); out.append(block); block.clear(); }
        }
    };
    auto reset = [&] {
        std::fill(next.begin(), next.end(), int16_t(-1));
        size = minSize + 1;
        nextCode = eoi + 1;
    };
    put(clear);
    int prefix = idx.empty() ? 0 : idx[0];
    for (size_t i = 1; i < idx.size(); i++) {
        const uint8_t c = idx[i];
        const int16_t n = next[size_t(prefix) * 256 + c];
        if (n >= 0) { prefix = n; continue; }
        put(prefix);
        if (nextCode < 4096) {
            next[size_t(prefix) * 256 + c] = int16_t(nextCode);
            if (nextCode == (1 << size) && size < 12) size++;
            nextCode++;
        } else {
            put(clear);
            reset();
        }
        prefix = c;
    }
    if (!idx.empty()) put(prefix);
    put(eoi);
    if (nbits > 0) block.append(char(bits & 0xff));
    if (!block.isEmpty()) { out.append(char(block.size())); out.append(block); }
    out.append(char(0));
}

void le16(QByteArray& b, int v) { b.append(char(v & 0xff)); b.append(char((v >> 8) & 0xff)); }

// ---- PNG / APNG ---------------------------------------------------------------------------------------------------

uint32_t crc32(const QByteArray& type, const QByteArray& data) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        made = true;
    }
    uint32_t c = 0xffffffffu;
    for (char ch : type) c = table[(c ^ uint8_t(ch)) & 0xff] ^ (c >> 8);
    for (char ch : data) c = table[(c ^ uint8_t(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void be32(QByteArray& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.append(char((v >> s) & 0xff));
}

void chunk(QByteArray& png, const char* type, const QByteArray& data) {
    be32(png, uint32_t(data.size()));
    const QByteArray t(type, 4);
    png.append(t);
    png.append(data);
    be32(png, crc32(t, data));
}

// A frame's pixels as PNG image data: each row with filter 1 (Sub: each byte minus the one to its left), deflated.
QByteArray pngData(const QImage& f) {
    QByteArray raw;
    raw.reserve(f.height() * (f.width() * 4 + 1));
    for (int y = 0; y < f.height(); y++) {
        const uint8_t* p = f.constScanLine(y);
        raw.append(char(1));
        for (int x = 0; x < f.width() * 4; x++) raw.append(char(uint8_t(p[x] - (x >= 4 ? p[x - 4] : 0))));
    }
    const QByteArray z = qCompress(raw, 9);
    return z.mid(4);   // qCompress puts the length first; the rest is a zlib stream
}

}  // namespace

// ---- frames ---------------------------------------------------------------------------------------------------------

std::vector<QImage> renderFrames(AnimViewport* view, const AnimDocument& doc, const Clip& clip, const Clip* held,
                                 const Options& opt, QString* error, const std::function<bool(int, int)>& progress) {
    (void)doc;
    std::vector<QImage> frames;
    if (!view || !view->usingGameRenderer()) {
        if (error) *error = QObject::tr("Exporting needs the game renderer (View > Preview with the Game Renderer).");
        return frames;
    }
    const int fps = opt.format == Format::Gif ? std::clamp(opt.fps, 1, 50) : std::clamp(opt.fps, 1, 120);
    const std::vector<float> times = frameTimes(clip, fps);
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    // the first frame also uploads the atlases, so the bounds below see the same atlas set
    view->renderClipFrame(clip, held, 0.0f, 4, 4, glm::mat3(1.0f), QColor(0, 0, 0, 0));
    if (!clipBounds(clip, held, view->atlasSetForExport(), times, x0, y0, x1, y1)) {
        if (error) *error = QObject::tr("The clip draws nothing.");
        return frames;
    }
    const float s = float(std::clamp(opt.scale, 0.05, 4.0));
    const float pad = float(opt.padding) + (toms::anim::hasEffects(clip) ? 40.0f : 0.0f);   // blur and glow reach past
    x0 -= pad; y0 -= pad; x1 += pad; y1 += pad;
    const int w = std::clamp(int(std::ceil((x1 - x0) * s)), 1, 2048), h = std::clamp(int(std::ceil((y1 - y0) * s)), 1, 2048);
    const glm::mat3 place = toms::anim::placement(-x0 * s, -y0 * s, s);
    for (size_t i = 0; i < times.size(); i++) {
        if (progress && !progress(int(i), int(times.size()))) { frames.clear(); return frames; }
        QImage f = view->renderClipFrame(clip, held, times[i], w, h, place, opt.background);
        if (f.isNull()) {
            if (error) *error = QObject::tr("The renderer gave no picture for frame %1.").arg(i + 1);
            frames.clear();
            return frames;
        }
        frames.push_back(std::move(f));
    }
    if (progress) progress(int(times.size()), int(times.size()));
    return frames;
}

// ---- writers --------------------------------------------------------------------------------------------------------

bool writeGif(const QString& path, const std::vector<QImage>& frames, int fps, bool loop, QString* error) {
    if (frames.empty()) return false;
    const int w = frames[0].width(), h = frames[0].height();
    const std::vector<std::array<uint8_t, 3>> pal = gifPalette(frames, 255);
    const int transparent = 255;
    // each 5-bit colour's nearest palette entry, found once
    std::vector<int16_t> nearest(32768, -1);
    auto lookup = [&](uint8_t r, uint8_t g, uint8_t b) {
        const size_t key = size_t((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
        if (nearest[key] >= 0) return uint8_t(nearest[key]);
        int best = 0, bestD = 1 << 30;
        for (size_t i = 0; i < pal.size(); i++) {
            const int dr = int(pal[i][0]) - r, dg = int(pal[i][1]) - g, db = int(pal[i][2]) - b;
            const int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
            if (d < bestD) { bestD = d; best = (int)i; }
        }
        nearest[key] = int16_t(best);
        return uint8_t(best);
    };
    QByteArray gif("GIF89a");
    le16(gif, w);
    le16(gif, h);
    gif.append(char(0xF7));   // a global colour table of 256, 8 bits per channel
    gif.append(char(transparent));
    gif.append(char(0));
    for (int i = 0; i < 256; i++)
        for (int k = 0; k < 3; k++) gif.append(char(i < (int)pal.size() ? pal[(size_t)i][(size_t)k] : 0));
    if (loop) {   // NETSCAPE2.0: play forever
        gif.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01", 16);
        le16(gif, 0);
        gif.append(char(0));
    }
    const int delay = std::max(2, int(std::lround(100.0 / std::max(1, fps))));
    std::vector<uint8_t> idx((size_t)w * h);
    for (const QImage& f : frames) {
        for (int y = 0; y < h; y++) {
            const uint8_t* p = f.constScanLine(y);
            for (int x = 0; x < w; x++, p += 4) idx[(size_t)y * w + x] = p[3] < 128 ? uint8_t(transparent) : lookup(p[0], p[1], p[2]);
        }
        // graphic control: dispose to background (each frame replaces the last), the delay, the transparent index
        gif.append("\x21\xF9\x04", 3);
        gif.append(char(0x09));
        le16(gif, delay);
        gif.append(char(transparent));
        gif.append(char(0));
        gif.append(char(0x2C));
        le16(gif, 0);
        le16(gif, 0);
        le16(gif, w);
        le16(gif, h);
        gif.append(char(0));
        gif.append(char(8));   // LZW minimum code size
        lzw(idx, gif);
    }
    gif.append(char(0x3B));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(gif) != gif.size()) {
        if (error) *error = QObject::tr("Could not write %1").arg(path);
        return false;
    }
    return true;
}

bool writeApng(const QString& path, const std::vector<QImage>& frames, int fps, bool loop, QString* error) {
    if (frames.empty()) return false;
    const int w = frames[0].width(), h = frames[0].height();
    QByteArray png("\x89PNG\r\n\x1a\n", 8);
    QByteArray ihdr;
    be32(ihdr, uint32_t(w));
    be32(ihdr, uint32_t(h));
    ihdr.append("\x08\x06\x00\x00\x00", 5);   // 8 bits, RGBA, deflate, filters, no interlace
    chunk(png, "IHDR", ihdr);
    QByteArray actl;
    be32(actl, uint32_t(frames.size()));
    be32(actl, loop ? 0u : 1u);   // plays: 0 = forever
    chunk(png, "acTL", actl);
    uint32_t seq = 0;
    for (size_t i = 0; i < frames.size(); i++) {
        QByteArray fctl;
        be32(fctl, seq++);
        be32(fctl, uint32_t(w));
        be32(fctl, uint32_t(h));
        be32(fctl, 0);
        be32(fctl, 0);
        fctl.append(char(0)); fctl.append(char(1));                                     // delay 1 /
        fctl.append(char((std::max(1, fps) >> 8) & 0xff)); fctl.append(char(std::max(1, fps) & 0xff));   // fps seconds
        fctl.append(char(1));   // dispose: clear to transparent
        fctl.append(char(0));   // blend: replace
        chunk(png, "fcTL", fctl);
        const QByteArray data = pngData(frames[i]);
        if (i == 0) {
            chunk(png, "IDAT", data);
        } else {
            QByteArray fdat;
            be32(fdat, seq++);
            fdat.append(data);
            chunk(png, "fdAT", fdat);
        }
    }
    chunk(png, "IEND", QByteArray());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size()) {
        if (error) *error = QObject::tr("Could not write %1").arg(path);
        return false;
    }
    return true;
}

bool writePngFrames(const QString& dir, const QString& base, const std::vector<QImage>& frames, QString* error) {
    for (size_t i = 0; i < frames.size(); i++) {
        const QString p = QDir(dir).filePath(QStringLiteral("%1_%2.png").arg(base).arg(int(i), 3, 10, QLatin1Char('0')));
        if (!frames[i].save(p)) {
            if (error) *error = QObject::tr("Could not write %1").arg(p);
            return false;
        }
    }
    return true;
}

QString exportClip(AnimViewport* view, const AnimDocument& doc, const Clip& clip, const Options& opt, const QString& outDir,
                   QString* error, const std::function<bool(int, int)>& progress) {
    const toms::anim::AnimFile& held = doc.heldFileData();
    const Clip* heldClip = doc.heldFile().isEmpty() ? nullptr : held.find(clip.name);
    const std::vector<QImage> frames = renderFrames(view, doc, clip, heldClip, opt, error, progress);
    if (frames.empty()) return QString();
    const QString stem = QFileInfo(doc.filePath()).completeBaseName();
    const QString base = (stem.isEmpty() ? QStringLiteral("clip") : stem) + QLatin1Char('_') + QString::fromStdString(clip.name);
    QDir().mkpath(outDir);
    const int fps = opt.format == Format::Gif ? std::clamp(opt.fps, 1, 50) : std::clamp(opt.fps, 1, 120);
    switch (opt.format) {
    case Format::Gif: {
        const QString p = QDir(outDir).filePath(base + QStringLiteral(".gif"));
        return writeGif(p, frames, fps, opt.loop, error) ? p : QString();
    }
    case Format::Apng: {
        const QString p = QDir(outDir).filePath(base + QStringLiteral(".png"));
        return writeApng(p, frames, fps, opt.loop, error) ? p : QString();
    }
    case Format::PngFrames:
        return writePngFrames(outDir, base, frames, error) ? QDir(outDir).filePath(base + QStringLiteral("_000.png")) : QString();
    }
    return QString();
}

// ---- the dialog -----------------------------------------------------------------------------------------------------

ExportDialog::ExportDialog(AnimDocument* doc, AnimViewport* view, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_view(view)
{
    setWindowTitle(tr("Export Clips as Animation"));
    QSettings st(QStringLiteral("TOMS"), QStringLiteral("AnimEditor"));
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("Clips to export (each one file):"), this));
    m_clips = new QListWidget(this);
    const toms::anim::Clip* current = doc->clip();
    for (const toms::anim::Clip& c : doc->file().clips) {
        auto* it = new QListWidgetItem(QString::fromStdString(c.name), m_clips);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(current && current->name == c.name ? Qt::Checked : Qt::Unchecked);
        it->setToolTip(tr("%1 s%2").arg(double(c.duration()), 0, 'f', 2).arg(c.playCount < 0 ? tr(", loops") : QString()));
    }
    v->addWidget(m_clips, 1);
    auto* all = new QHBoxLayout();
    auto* allOn = new QPushButton(tr("All"), this);
    auto* allOff = new QPushButton(tr("None"), this);
    connect(allOn, &QPushButton::clicked, this, [this] { for (int i = 0; i < m_clips->count(); i++) m_clips->item(i)->setCheckState(Qt::Checked); });
    connect(allOff, &QPushButton::clicked, this, [this] { for (int i = 0; i < m_clips->count(); i++) m_clips->item(i)->setCheckState(Qt::Unchecked); });
    all->addWidget(allOn);
    all->addWidget(allOff);
    all->addStretch(1);
    v->addLayout(all);

    auto* f = new QFormLayout();
    m_format = new QComboBox(this);
    m_format->addItems({tr("GIF (.gif)"), tr("Animated PNG (.png, APNG)"), tr("PNG frames (one file each)")});
    m_format->setToolTip(tr("GIF: 256 colours, transparency on or off per pixel (soft edges become hard).\n"
                            "Animated PNG: full colour and soft transparency; browsers and most viewers play it.\n"
                            "PNG frames: one picture per frame, for a video tool or a sprite sheet."));
    m_format->setCurrentIndex(st.value(QStringLiteral("export/format"), 0).toInt());
    f->addRow(tr("Format"), m_format);
    m_fps = new QSpinBox(this);
    m_fps->setRange(1, 120);
    m_fps->setValue(st.value(QStringLiteral("export/fps"), 30).toInt());
    m_fps->setToolTip(tr("Frames per second (a GIF plays at most 50)"));
    f->addRow(tr("Frame rate"), m_fps);
    m_scale = new QDoubleSpinBox(this);
    m_scale->setRange(0.05, 4);
    m_scale->setSingleStep(0.05);
    m_scale->setDecimals(2);
    m_scale->setValue(st.value(QStringLiteral("export/scale"), 0.5).toDouble());
    m_scale->setToolTip(tr("The picture's size: times the clip's pixels"));
    f->addRow(tr("Scale"), m_scale);
    m_transparent = new QCheckBox(tr("Transparent background"), this);
    m_transparent->setChecked(st.value(QStringLiteral("export/transparent"), true).toBool());
    f->addRow(QString(), m_transparent);
    auto* bg = m_bg = new QPushButton(this);
    QColor colour = st.value(QStringLiteral("export/background"), QColor(32, 32, 40)).value<QColor>();
    auto paintButton = [bg](const QColor& c) { bg->setText(c.name()); bg->setStyleSheet(QStringLiteral("background:%1").arg(c.name())); };
    paintButton(colour);
    bg->setProperty("colour", colour);
    connect(bg, &QPushButton::clicked, this, [this, bg, paintButton] {
        const QColor c = QColorDialog::getColor(bg->property("colour").value<QColor>(), this, tr("Background"));
        if (c.isValid()) { bg->setProperty("colour", c); paintButton(c); }
    });
    bg->setEnabled(!m_transparent->isChecked());
    connect(m_transparent, &QCheckBox::toggled, bg, [bg](bool on) { bg->setEnabled(!on); });
    f->addRow(tr("Background"), bg);
    m_loop = new QCheckBox(tr("Play forever"), this);
    m_loop->setChecked(st.value(QStringLiteral("export/loop"), true).toBool());
    f->addRow(QString(), m_loop);
    m_held = new QCheckBox(tr("With the held clip (View > Held Clip)"), this);
    m_held->setEnabled(!doc->heldFile().isEmpty());
    m_held->setChecked(!doc->heldFile().isEmpty());
    m_held->setToolTip(doc->heldFile().isEmpty() ? tr("No held clip chosen") : doc->heldFile());
    f->addRow(QString(), m_held);
    auto* dirRow = new QHBoxLayout();
    m_dir = new QLineEdit(this);
    const QString defDir = doc->filePath().isEmpty() ? QDir::homePath() : QFileInfo(doc->filePath()).absolutePath() + QStringLiteral("/export");
    m_dir->setText(st.value(QStringLiteral("export/dir"), defDir).toString());
    auto* browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Export to"), m_dir->text());
        if (!d.isEmpty()) m_dir->setText(d);
    });
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browse);
    f->addRow(tr("Folder"), dirRow);
    v->addLayout(f);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* go = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, [this, bg] {
        QSettings s(QStringLiteral("TOMS"), QStringLiteral("AnimEditor"));
        s.setValue(QStringLiteral("export/format"), m_format->currentIndex());
        s.setValue(QStringLiteral("export/fps"), m_fps->value());
        s.setValue(QStringLiteral("export/scale"), m_scale->value());
        s.setValue(QStringLiteral("export/transparent"), m_transparent->isChecked());
        s.setValue(QStringLiteral("export/background"), bg->property("colour").value<QColor>());
        s.setValue(QStringLiteral("export/loop"), m_loop->isChecked());
        s.setValue(QStringLiteral("export/dir"), m_dir->text());
        run();
    });
    v->addWidget(buttons);
    resize(420, 520);
}

void ExportDialog::run()
{
    Options opt;
    opt.format = Format(m_format->currentIndex());
    opt.fps = m_fps->value();
    opt.scale = m_scale->value();
    opt.loop = m_loop->isChecked();
    if (!m_transparent->isChecked()) opt.background = m_bg->property("colour").value<QColor>();
    std::vector<const toms::anim::Clip*> clips;
    for (int i = 0; i < m_clips->count(); i++)
        if (m_clips->item(i)->checkState() == Qt::Checked)
            if (const toms::anim::Clip* c = m_doc->file().find(m_clips->item(i)->text().toStdString())) clips.push_back(c);
    if (clips.empty()) {
        QMessageBox::information(this, windowTitle(), tr("Check the clips to export."));
        return;
    }
    const QString held = m_doc->heldFile();
    if (!m_held->isChecked() && !held.isEmpty()) m_doc->setHeldFile(QString());   // without it this time
    QProgressDialog progress(tr("Exporting..."), tr("Cancel"), 0, 1000, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QStringList written, failed;
    for (size_t k = 0; k < clips.size(); k++) {
        QString err;
        const QString name = QString::fromStdString(clips[k]->name);
        const QString p = exportClip(m_view, *m_doc, *clips[k], opt, m_dir->text(), &err, [&](int done, int total) {
            progress.setLabelText(tr("%1 (%2 of %3): frame %4 of %5").arg(name).arg(k + 1).arg(clips.size()).arg(done).arg(total));
            progress.setValue(int(1000.0 * (double(k) + double(done) / std::max(1, total)) / double(clips.size())));
            return !progress.wasCanceled();
        });
        if (progress.wasCanceled()) break;
        if (p.isEmpty()) failed << name + QStringLiteral(": ") + err;
        else written << QDir::toNativeSeparators(p);
    }
    progress.close();
    if (!m_held->isChecked() && !held.isEmpty()) m_doc->setHeldFile(held);
    QString text = written.isEmpty() ? tr("Nothing was written.") : tr("Written:\n%1").arg(written.join(QLatin1Char('\n')));
    if (!failed.isEmpty()) text += tr("\n\nFailed:\n%1").arg(failed.join(QLatin1Char('\n')));
    QMessageBox::information(this, windowTitle(), text);
}

}  // namespace animexport
