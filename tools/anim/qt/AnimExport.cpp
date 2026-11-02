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
std::vector<float> f_FrameTimes(const Clip& l_oClip, int l_iFps) {
    const float l_fD = l_oClip.f_Duration();
    std::vector<float> l_vOut;
    if (l_fD <= 0 || l_iFps <= 0) return {0.0f};
    const int l_iN = std::max(1, int(std::lround(l_fD * l_iFps)));
    for (int l_iI = 0; l_iI < l_iN; l_iI++) l_vOut.push_back(float(l_iI) / float(l_iFps));
    if (l_oClip.m_iPlayCount >= 0) l_vOut.push_back(l_fD);
    return l_vOut;
}

// What every frame draws, in clip pixels (quads' corners, meshes' vertices; the held clip at `hand`).
bool f_ClipBounds(const Clip& l_oClip, const Clip* l_pHeld, const toms::anim::AtlasSet& l_oAtlases, const std::vector<float>& l_vTimes,
                float& l_fX0, float& l_fY0, float& l_fX1, float& l_fY1) {
    bool l_bAny = false;
    auto l_fnGrow = [&](float l_fX, float l_fY) {
        if (!l_bAny) { l_fX0 = l_fX1 = l_fX; l_fY0 = l_fY1 = l_fY; l_bAny = true; return; }
        l_fX0 = std::min(l_fX0, l_fX); l_fX1 = std::max(l_fX1, l_fX); l_fY0 = std::min(l_fY0, l_fY); l_fY1 = std::max(l_fY1, l_fY);
    };
    std::vector<NodePose> l_vPoses, l_vHeldPoses;
    toms::anim::DrawList l_oD;
    for (float l_fT : l_vTimes) {
        l_oD.f_Clear();
        toms::anim::f_Evaluate(l_oClip, l_fT, l_vPoses);
        toms::anim::f_AppendDraws(l_oClip, l_vPoses, l_oAtlases, glm::mat3(1.0f), nullptr, l_oD);
        glm::mat3 l_mat3Hand;
        if (l_pHeld && toms::anim::f_AttachPlacement(l_vPoses, "hand", glm::mat3(1.0f), l_mat3Hand)) {
            toms::anim::f_Evaluate(*l_pHeld, std::min(l_fT, l_pHeld->f_Duration()), l_vHeldPoses);
            toms::anim::f_AppendDraws(*l_pHeld, l_vHeldPoses, l_oAtlases, l_mat3Hand, nullptr, l_oD);
        }
        for (const Quad& l_oQ : l_oD.m_vQuads) {
            if (l_oQ.m_arrTint[3] <= 0.0f || l_oQ.m_arrRect[2] > 1500.0f || l_oQ.m_arrRect[3] > 1500.0f) continue;   // a screen-sized backdrop
            for (int l_iK = 0; l_iK < 4; l_iK++) l_fnGrow(l_oQ.m_arrCorners[2 * l_iK], l_oQ.m_arrCorners[2 * l_iK + 1]);
        }
        for (const toms::anim::DrawList::MeshItem& l_oM : l_oD.m_vMeshes) {
            if (l_oM.m_arrTint[3] <= 0.0f) continue;
            for (uint32_t l_uV = l_oM.m_uFirstVertex; l_uV < l_oM.m_uFirstVertex + l_oM.m_uVertexCount; l_uV++) l_fnGrow(l_oD.m_vVertices[l_uV].m_fX, l_oD.m_vVertices[l_uV].m_fY);
        }
    }
    return l_bAny;
}

// ---- GIF ------------------------------------------------------------------------------------------------------------

// One palette for every frame (the colours do not flicker between frames): median cut over the opaque pixels' colours
// at 5 bits a channel, 255 colours (index 255: transparent).
std::vector<std::array<uint8_t, 3>> f_GifPalette(const std::vector<QImage>& l_vFrames, int l_iColours) {
    std::vector<uint32_t> l_vHist(32768, 0);
    for (const QImage& l_oF : l_vFrames)
        for (int l_iY = 0; l_iY < l_oF.height(); l_iY++) {
            const uint8_t* l_pP = l_oF.constScanLine(l_iY);
            for (int l_iX = 0; l_iX < l_oF.width(); l_iX++, l_pP += 4)
                if (l_pP[3] >= 128) l_vHist[size_t((l_pP[0] >> 3) << 10 | (l_pP[1] >> 3) << 5 | (l_pP[2] >> 3))]++;
        }
    struct Box { std::vector<uint16_t> m_vCells; };
    std::vector<Box> l_vBoxes(1);
    for (uint16_t l_u16C = 0; l_u16C < 32768; l_u16C++) if (l_vHist[l_u16C]) l_vBoxes[0].m_vCells.push_back(l_u16C);
    if (l_vBoxes[0].m_vCells.empty()) return {{0, 0, 0}};
    auto l_fnChannel = [](uint16_t l_u16C, int l_iK) { return int(l_iK == 0 ? l_u16C >> 10 : l_iK == 1 ? (l_u16C >> 5) & 31 : l_u16C & 31); };
    while ((int)l_vBoxes.size() < l_iColours) {
        // the box with the most pixels that can still be split, along its widest channel, at the pixels' median
        int l_iBest = -1;
        uint64_t l_u64BestCount = 0;
        for (size_t l_nB = 0; l_nB < l_vBoxes.size(); l_nB++) {
            if (l_vBoxes[l_nB].m_vCells.size() < 2) continue;
            uint64_t l_u64N = 0;
            for (uint16_t l_u16C : l_vBoxes[l_nB].m_vCells) l_u64N += l_vHist[l_u16C];
            if (l_u64N > l_u64BestCount) { l_u64BestCount = l_u64N; l_iBest = (int)l_nB; }
        }
        if (l_iBest < 0) break;
        Box& l_oBox = l_vBoxes[(size_t)l_iBest];
        int l_arrLo[3] = {31, 31, 31}, l_arrHi[3] = {0, 0, 0};
        for (uint16_t l_u16C : l_oBox.m_vCells)
            for (int l_iK = 0; l_iK < 3; l_iK++) { l_arrLo[l_iK] = std::min(l_arrLo[l_iK], l_fnChannel(l_u16C, l_iK)); l_arrHi[l_iK] = std::max(l_arrHi[l_iK], l_fnChannel(l_u16C, l_iK)); }
        int l_iAxis = 0;
        for (int l_iK = 1; l_iK < 3; l_iK++) if (l_arrHi[l_iK] - l_arrLo[l_iK] > l_arrHi[l_iAxis] - l_arrLo[l_iAxis]) l_iAxis = l_iK;
        std::sort(l_oBox.m_vCells.begin(), l_oBox.m_vCells.end(), [&](uint16_t l_u16A, uint16_t l_u16B) { return l_fnChannel(l_u16A, l_iAxis) < l_fnChannel(l_u16B, l_iAxis); });
        uint64_t l_u64Acc = 0;
        size_t l_nCut = 1;
        for (size_t l_nI = 0; l_nI < l_oBox.m_vCells.size() - 1; l_nI++) {
            l_u64Acc += l_vHist[l_oBox.m_vCells[l_nI]];
            l_nCut = l_nI + 1;
            if (l_u64Acc * 2 >= l_u64BestCount) break;
        }
        Box l_oOther;
        l_oOther.m_vCells.assign(l_oBox.m_vCells.begin() + (long)l_nCut, l_oBox.m_vCells.end());
        l_oBox.m_vCells.resize(l_nCut);
        l_vBoxes.push_back(std::move(l_oOther));
    }
    std::vector<std::array<uint8_t, 3>> l_vPal;
    for (const Box& l_oB : l_vBoxes) {   // each box's colour: its pixels' average
        uint64_t l_u64N = 0, l_arrS[3] = {0, 0, 0};
        for (uint16_t l_u16C : l_oB.m_vCells)
            for (int l_iK = 0; l_iK < 3; l_iK++) l_arrS[l_iK] += uint64_t(l_fnChannel(l_u16C, l_iK) * 8 + 4) * l_vHist[l_u16C];
        for (uint16_t l_u16C : l_oB.m_vCells) l_u64N += l_vHist[l_u16C];
        l_vPal.push_back({uint8_t(l_arrS[0] / l_u64N), uint8_t(l_arrS[1] / l_u64N), uint8_t(l_arrS[2] / l_u64N)});
    }
    return l_vPal;
}

// GIF's LZW: variable-width codes (min code size 8), a clear code when the table is full.
void f_Lzw(const std::vector<uint8_t>& l_vIdx, QByteArray& l_oOut) {
    const int l_iMinSize = 8, l_iClear = 1 << l_iMinSize, l_iEoi = l_iClear + 1;
    std::vector<int16_t> l_vNext(4096 * 256, -1);   // (prefix code, byte) -> code
    int l_iSize = l_iMinSize + 1, l_iNextCode = l_iEoi + 1;
    uint32_t l_uBits = 0;
    int l_iNbits = 0;
    QByteArray l_oBlock;
    auto l_fnPut = [&](int l_iCode) {
        l_uBits |= uint32_t(l_iCode) << l_iNbits;
        l_iNbits += l_iSize;
        while (l_iNbits >= 8) {
            l_oBlock.append(char(l_uBits & 0xff));
            l_uBits >>= 8;
            l_iNbits -= 8;
            if (l_oBlock.size() == 255) { l_oOut.append(char(255)); l_oOut.append(l_oBlock); l_oBlock.clear(); }
        }
    };
    auto l_fnReset = [&] {
        std::fill(l_vNext.begin(), l_vNext.end(), int16_t(-1));
        l_iSize = l_iMinSize + 1;
        l_iNextCode = l_iEoi + 1;
    };
    l_fnPut(l_iClear);
    int l_iPrefix = l_vIdx.empty() ? 0 : l_vIdx[0];
    for (size_t l_nI = 1; l_nI < l_vIdx.size(); l_nI++) {
        const uint8_t l_u8C = l_vIdx[l_nI];
        const int16_t l_i16N = l_vNext[size_t(l_iPrefix) * 256 + l_u8C];
        if (l_i16N >= 0) { l_iPrefix = l_i16N; continue; }
        l_fnPut(l_iPrefix);
        if (l_iNextCode < 4096) {
            l_vNext[size_t(l_iPrefix) * 256 + l_u8C] = int16_t(l_iNextCode);
            if (l_iNextCode == (1 << l_iSize) && l_iSize < 12) l_iSize++;
            l_iNextCode++;
        } else {
            l_fnPut(l_iClear);
            l_fnReset();
        }
        l_iPrefix = l_u8C;
    }
    if (!l_vIdx.empty()) l_fnPut(l_iPrefix);
    l_fnPut(l_iEoi);
    if (l_iNbits > 0) l_oBlock.append(char(l_uBits & 0xff));
    if (!l_oBlock.isEmpty()) { l_oOut.append(char(l_oBlock.size())); l_oOut.append(l_oBlock); }
    l_oOut.append(char(0));
}

void f_Le16(QByteArray& l_oB, int l_iV) { l_oB.append(char(l_iV & 0xff)); l_oB.append(char((l_iV >> 8) & 0xff)); }

// ---- PNG / APNG ---------------------------------------------------------------------------------------------------

uint32_t f_Crc32(const QByteArray& l_oType, const QByteArray& l_oData) {
    static uint32_t l_arrTable[256];
    static bool l_bMade = false;
    if (!l_bMade) {
        for (uint32_t l_uN = 0; l_uN < 256; l_uN++) {
            uint32_t l_uC = l_uN;
            for (int l_iK = 0; l_iK < 8; l_iK++) l_uC = l_uC & 1 ? 0xedb88320u ^ (l_uC >> 1) : l_uC >> 1;
            l_arrTable[l_uN] = l_uC;
        }
        l_bMade = true;
    }
    uint32_t l_uC = 0xffffffffu;
    for (char l_cCh : l_oType) l_uC = l_arrTable[(l_uC ^ uint8_t(l_cCh)) & 0xff] ^ (l_uC >> 8);
    for (char l_cCh : l_oData) l_uC = l_arrTable[(l_uC ^ uint8_t(l_cCh)) & 0xff] ^ (l_uC >> 8);
    return l_uC ^ 0xffffffffu;
}

void f_Be32(QByteArray& l_oB, uint32_t l_uV) {
    for (int l_iS = 24; l_iS >= 0; l_iS -= 8) l_oB.append(char((l_uV >> l_iS) & 0xff));
}

void f_Chunk(QByteArray& l_oPng, const char* l_pszType, const QByteArray& l_oData) {
    f_Be32(l_oPng, uint32_t(l_oData.size()));
    const QByteArray l_oT(l_pszType, 4);
    l_oPng.append(l_oT);
    l_oPng.append(l_oData);
    f_Be32(l_oPng, f_Crc32(l_oT, l_oData));
}

// A frame's pixels as PNG image data: each row with filter 1 (Sub: each byte minus the one to its left), deflated.
QByteArray f_PngData(const QImage& l_oF) {
    QByteArray l_oRaw;
    l_oRaw.reserve(l_oF.height() * (l_oF.width() * 4 + 1));
    for (int l_iY = 0; l_iY < l_oF.height(); l_iY++) {
        const uint8_t* l_pP = l_oF.constScanLine(l_iY);
        l_oRaw.append(char(1));
        for (int l_iX = 0; l_iX < l_oF.width() * 4; l_iX++) l_oRaw.append(char(uint8_t(l_pP[l_iX] - (l_iX >= 4 ? l_pP[l_iX - 4] : 0))));
    }
    const QByteArray l_oZ = qCompress(l_oRaw, 9);
    return l_oZ.mid(4);   // qCompress puts the length first; the rest is a zlib stream
}

}  // namespace

// ---- frames ---------------------------------------------------------------------------------------------------------

std::vector<QImage> f_RenderFrames(AnimViewport* l_pView, const AnimDocument& l_oDoc, const Clip& l_oClip, const Clip* l_pHeld,
                                 const Options& l_oOpt, QString* l_pError, const std::function<bool(int, int)>& l_fnProgress) {
    (void)l_oDoc;
    std::vector<QImage> l_vFrames;
    if (!l_pView || !l_pView->f_UsingGameRenderer()) {
        if (l_pError) *l_pError = QObject::tr("Exporting needs the game renderer (View > Preview with the Game Renderer).");
        return l_vFrames;
    }
    const int l_iFps = l_oOpt.m_eFormat == Format::Gif ? std::clamp(l_oOpt.m_iFps, 1, 50) : std::clamp(l_oOpt.m_iFps, 1, 120);
    const std::vector<float> l_vTimes = f_FrameTimes(l_oClip, l_iFps);
    float l_fX0 = 0, l_fY0 = 0, l_fX1 = 0, l_fY1 = 0;
    // the first frame also uploads the atlases, so the bounds below see the same atlas set
    l_pView->f_RenderClipFrame(l_oClip, l_pHeld, 0.0f, 4, 4, glm::mat3(1.0f), QColor(0, 0, 0, 0));
    if (!f_ClipBounds(l_oClip, l_pHeld, l_pView->f_AtlasSetForExport(), l_vTimes, l_fX0, l_fY0, l_fX1, l_fY1)) {
        if (l_pError) *l_pError = QObject::tr("The clip draws nothing.");
        return l_vFrames;
    }
    const float l_fS = float(std::clamp(l_oOpt.m_dScale, 0.05, 4.0));
    const float l_fPad = float(l_oOpt.m_iPadding) + (toms::anim::f_HasEffects(l_oClip) ? 40.0f : 0.0f);   // blur and glow reach past
    l_fX0 -= l_fPad; l_fY0 -= l_fPad; l_fX1 += l_fPad; l_fY1 += l_fPad;
    const int l_iW = std::clamp(int(std::ceil((l_fX1 - l_fX0) * l_fS)), 1, 2048), l_iH = std::clamp(int(std::ceil((l_fY1 - l_fY0) * l_fS)), 1, 2048);
    const glm::mat3 l_mat3Place = toms::anim::f_Placement(-l_fX0 * l_fS, -l_fY0 * l_fS, l_fS);
    for (size_t l_nI = 0; l_nI < l_vTimes.size(); l_nI++) {
        if (l_fnProgress && !l_fnProgress(int(l_nI), int(l_vTimes.size()))) { l_vFrames.clear(); return l_vFrames; }
        QImage l_oF = l_pView->f_RenderClipFrame(l_oClip, l_pHeld, l_vTimes[l_nI], l_iW, l_iH, l_mat3Place, l_oOpt.m_oBackground);
        if (l_oF.isNull()) {
            if (l_pError) *l_pError = QObject::tr("The renderer gave no picture for frame %1.").arg(l_nI + 1);
            l_vFrames.clear();
            return l_vFrames;
        }
        l_vFrames.push_back(std::move(l_oF));
    }
    if (l_fnProgress) l_fnProgress(int(l_vTimes.size()), int(l_vTimes.size()));
    return l_vFrames;
}

// ---- writers --------------------------------------------------------------------------------------------------------

bool f_WriteGif(const QString& l_oPath, const std::vector<QImage>& l_vFrames, int l_iFps, bool l_bLoop, QString* l_pError) {
    if (l_vFrames.empty()) return false;
    const int l_iW = l_vFrames[0].width(), l_iH = l_vFrames[0].height();
    const std::vector<std::array<uint8_t, 3>> l_vPal = f_GifPalette(l_vFrames, 255);
    const int l_iTransparent = 255;
    // each 5-bit colour's nearest palette entry, found once
    std::vector<int16_t> l_vNearest(32768, -1);
    auto l_fnLookup = [&](uint8_t l_u8R, uint8_t l_u8G, uint8_t l_u8B) {
        const size_t l_nKey = size_t((l_u8R >> 3) << 10 | (l_u8G >> 3) << 5 | (l_u8B >> 3));
        if (l_vNearest[l_nKey] >= 0) return uint8_t(l_vNearest[l_nKey]);
        int l_iBest = 0, l_iBestD = 1 << 30;
        for (size_t l_nI = 0; l_nI < l_vPal.size(); l_nI++) {
            const int l_iDr = int(l_vPal[l_nI][0]) - l_u8R, l_iDg = int(l_vPal[l_nI][1]) - l_u8G, l_iDb = int(l_vPal[l_nI][2]) - l_u8B;
            const int l_iD = 3 * l_iDr * l_iDr + 4 * l_iDg * l_iDg + 2 * l_iDb * l_iDb;
            if (l_iD < l_iBestD) { l_iBestD = l_iD; l_iBest = (int)l_nI; }
        }
        l_vNearest[l_nKey] = int16_t(l_iBest);
        return uint8_t(l_iBest);
    };
    QByteArray l_oGif("GIF89a");
    f_Le16(l_oGif, l_iW);
    f_Le16(l_oGif, l_iH);
    l_oGif.append(char(0xF7));   // a global colour table of 256, 8 bits per channel
    l_oGif.append(char(l_iTransparent));
    l_oGif.append(char(0));
    for (int l_iI = 0; l_iI < 256; l_iI++)
        for (int l_iK = 0; l_iK < 3; l_iK++) l_oGif.append(char(l_iI < (int)l_vPal.size() ? l_vPal[(size_t)l_iI][(size_t)l_iK] : 0));
    if (l_bLoop) {   // NETSCAPE2.0: play forever
        l_oGif.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01", 16);
        f_Le16(l_oGif, 0);
        l_oGif.append(char(0));
    }
    const int l_iDelay = std::max(2, int(std::lround(100.0 / std::max(1, l_iFps))));
    std::vector<uint8_t> l_vIdx((size_t)l_iW * l_iH);
    for (const QImage& l_oF : l_vFrames) {
        for (int l_iY = 0; l_iY < l_iH; l_iY++) {
            const uint8_t* l_pP = l_oF.constScanLine(l_iY);
            for (int l_iX = 0; l_iX < l_iW; l_iX++, l_pP += 4) l_vIdx[(size_t)l_iY * l_iW + l_iX] = l_pP[3] < 128 ? uint8_t(l_iTransparent) : l_fnLookup(l_pP[0], l_pP[1], l_pP[2]);
        }
        // graphic control: dispose to background (each frame replaces the last), the delay, the transparent index
        l_oGif.append("\x21\xF9\x04", 3);
        l_oGif.append(char(0x09));
        f_Le16(l_oGif, l_iDelay);
        l_oGif.append(char(l_iTransparent));
        l_oGif.append(char(0));
        l_oGif.append(char(0x2C));
        f_Le16(l_oGif, 0);
        f_Le16(l_oGif, 0);
        f_Le16(l_oGif, l_iW);
        f_Le16(l_oGif, l_iH);
        l_oGif.append(char(0));
        l_oGif.append(char(8));   // LZW minimum code size
        f_Lzw(l_vIdx, l_oGif);
    }
    l_oGif.append(char(0x3B));
    QFile l_oFile(l_oPath);
    if (!l_oFile.open(QIODevice::WriteOnly) || l_oFile.write(l_oGif) != l_oGif.size()) {
        if (l_pError) *l_pError = QObject::tr("Could not write %1").arg(l_oPath);
        return false;
    }
    return true;
}

bool f_WriteApng(const QString& l_oPath, const std::vector<QImage>& l_vFrames, int l_iFps, bool l_bLoop, QString* l_pError) {
    if (l_vFrames.empty()) return false;
    const int l_iW = l_vFrames[0].width(), l_iH = l_vFrames[0].height();
    QByteArray l_oPng("\x89PNG\r\n\x1a\n", 8);
    QByteArray l_oIhdr;
    f_Be32(l_oIhdr, uint32_t(l_iW));
    f_Be32(l_oIhdr, uint32_t(l_iH));
    l_oIhdr.append("\x08\x06\x00\x00\x00", 5);   // 8 bits, RGBA, deflate, filters, no interlace
    f_Chunk(l_oPng, "IHDR", l_oIhdr);
    QByteArray l_oActl;
    f_Be32(l_oActl, uint32_t(l_vFrames.size()));
    f_Be32(l_oActl, l_bLoop ? 0u : 1u);   // plays: 0 = forever
    f_Chunk(l_oPng, "acTL", l_oActl);
    uint32_t l_uSeq = 0;
    for (size_t l_nI = 0; l_nI < l_vFrames.size(); l_nI++) {
        QByteArray l_oFctl;
        f_Be32(l_oFctl, l_uSeq++);
        f_Be32(l_oFctl, uint32_t(l_iW));
        f_Be32(l_oFctl, uint32_t(l_iH));
        f_Be32(l_oFctl, 0);
        f_Be32(l_oFctl, 0);
        l_oFctl.append(char(0)); l_oFctl.append(char(1));                                     // delay 1 /
        l_oFctl.append(char((std::max(1, l_iFps) >> 8) & 0xff)); l_oFctl.append(char(std::max(1, l_iFps) & 0xff));   // fps seconds
        l_oFctl.append(char(1));   // dispose: clear to transparent
        l_oFctl.append(char(0));   // blend: replace
        f_Chunk(l_oPng, "fcTL", l_oFctl);
        const QByteArray l_oData = f_PngData(l_vFrames[l_nI]);
        if (l_nI == 0) {
            f_Chunk(l_oPng, "IDAT", l_oData);
        } else {
            QByteArray l_oFdat;
            f_Be32(l_oFdat, l_uSeq++);
            l_oFdat.append(l_oData);
            f_Chunk(l_oPng, "fdAT", l_oFdat);
        }
    }
    f_Chunk(l_oPng, "IEND", QByteArray());
    QFile l_oFile(l_oPath);
    if (!l_oFile.open(QIODevice::WriteOnly) || l_oFile.write(l_oPng) != l_oPng.size()) {
        if (l_pError) *l_pError = QObject::tr("Could not write %1").arg(l_oPath);
        return false;
    }
    return true;
}

bool f_WritePngFrames(const QString& l_oDir, const QString& l_oBase, const std::vector<QImage>& l_vFrames, QString* l_pError) {
    for (size_t l_nI = 0; l_nI < l_vFrames.size(); l_nI++) {
        const QString l_oP = QDir(l_oDir).filePath(QStringLiteral("%1_%2.png").arg(l_oBase).arg(int(l_nI), 3, 10, QLatin1Char('0')));
        if (!l_vFrames[l_nI].save(l_oP)) {
            if (l_pError) *l_pError = QObject::tr("Could not write %1").arg(l_oP);
            return false;
        }
    }
    return true;
}

QString f_ExportClip(AnimViewport* l_pView, const AnimDocument& l_oDoc, const Clip& l_oClip, const Options& l_oOpt, const QString& l_oOutDir,
                   QString* l_pError, const std::function<bool(int, int)>& l_fnProgress) {
    const toms::anim::AnimFile& l_oHeld = l_oDoc.f_HeldFileData();
    const Clip* l_pHeldClip = l_oDoc.f_HeldFile().isEmpty() ? nullptr : l_oHeld.f_Find(l_oClip.m_strName);
    const std::vector<QImage> l_vFrames = f_RenderFrames(l_pView, l_oDoc, l_oClip, l_pHeldClip, l_oOpt, l_pError, l_fnProgress);
    if (l_vFrames.empty()) return QString();
    const QString l_oStem = QFileInfo(l_oDoc.f_FilePath()).completeBaseName();
    const QString l_oBase = (l_oStem.isEmpty() ? QStringLiteral("clip") : l_oStem) + QLatin1Char('_') + QString::fromStdString(l_oClip.m_strName);
    QDir().mkpath(l_oOutDir);
    const int l_iFps = l_oOpt.m_eFormat == Format::Gif ? std::clamp(l_oOpt.m_iFps, 1, 50) : std::clamp(l_oOpt.m_iFps, 1, 120);
    switch (l_oOpt.m_eFormat) {
    case Format::Gif: {
        const QString l_oP = QDir(l_oOutDir).filePath(l_oBase + QStringLiteral(".gif"));
        return f_WriteGif(l_oP, l_vFrames, l_iFps, l_oOpt.m_bLoop, l_pError) ? l_oP : QString();
    }
    case Format::Apng: {
        const QString l_oP = QDir(l_oOutDir).filePath(l_oBase + QStringLiteral(".png"));
        return f_WriteApng(l_oP, l_vFrames, l_iFps, l_oOpt.m_bLoop, l_pError) ? l_oP : QString();
    }
    case Format::PngFrames:
        return f_WritePngFrames(l_oOutDir, l_oBase, l_vFrames, l_pError) ? QDir(l_oOutDir).filePath(l_oBase + QStringLiteral("_000.png")) : QString();
    }
    return QString();
}

// ---- the dialog -----------------------------------------------------------------------------------------------------

ExportDialog::ExportDialog(AnimDocument* l_pDoc, AnimViewport* l_pView, QWidget* l_pParent)
    : QDialog(l_pParent), m_pDoc(l_pDoc), m_pView(l_pView)
{
    setWindowTitle(tr("Export Clips as Animation"));
    QSettings l_oSt(QStringLiteral("TOMS"), QStringLiteral("AnimEditor"));
    auto* l_pV = new QVBoxLayout(this);
    l_pV->addWidget(new QLabel(tr("Clips to export (each one file):"), this));
    m_pClips = new QListWidget(this);
    const toms::anim::Clip* l_pCurrent = l_pDoc->f_Clip();
    for (const toms::anim::Clip& l_oC : l_pDoc->f_File().m_vClips) {
        auto* l_pIt = new QListWidgetItem(QString::fromStdString(l_oC.m_strName), m_pClips);
        l_pIt->setFlags(l_pIt->flags() | Qt::ItemIsUserCheckable);
        l_pIt->setCheckState(l_pCurrent && l_pCurrent->m_strName == l_oC.m_strName ? Qt::Checked : Qt::Unchecked);
        l_pIt->setToolTip(tr("%1 s%2").arg(double(l_oC.f_Duration()), 0, 'f', 2).arg(l_oC.m_iPlayCount < 0 ? tr(", loops") : QString()));
    }
    l_pV->addWidget(m_pClips, 1);
    auto* l_pAll = new QHBoxLayout();
    auto* l_pAllOn = new QPushButton(tr("All"), this);
    auto* l_pAllOff = new QPushButton(tr("None"), this);
    connect(l_pAllOn, &QPushButton::clicked, this, [this] { for (int l_iI = 0; l_iI < m_pClips->count(); l_iI++) m_pClips->item(l_iI)->setCheckState(Qt::Checked); });
    connect(l_pAllOff, &QPushButton::clicked, this, [this] { for (int l_iI = 0; l_iI < m_pClips->count(); l_iI++) m_pClips->item(l_iI)->setCheckState(Qt::Unchecked); });
    l_pAll->addWidget(l_pAllOn);
    l_pAll->addWidget(l_pAllOff);
    l_pAll->addStretch(1);
    l_pV->addLayout(l_pAll);

    auto* l_pF = new QFormLayout();
    m_pFormat = new QComboBox(this);
    m_pFormat->addItems({tr("GIF (.gif)"), tr("Animated PNG (.png, APNG)"), tr("PNG frames (one file each)")});
    m_pFormat->setToolTip(tr("GIF: 256 colours, transparency on or off per pixel (soft edges become hard).\n"
                            "Animated PNG: full colour and soft transparency; browsers and most viewers play it.\n"
                            "PNG frames: one picture per frame, for a video tool or a sprite sheet."));
    m_pFormat->setCurrentIndex(l_oSt.value(QStringLiteral("export/format"), 0).toInt());
    l_pF->addRow(tr("Format"), m_pFormat);
    m_pFps = new QSpinBox(this);
    m_pFps->setRange(1, 120);
    m_pFps->setValue(l_oSt.value(QStringLiteral("export/fps"), 30).toInt());
    m_pFps->setToolTip(tr("Frames per second (a GIF plays at most 50)"));
    l_pF->addRow(tr("Frame rate"), m_pFps);
    m_pScale = new QDoubleSpinBox(this);
    m_pScale->setRange(0.05, 4);
    m_pScale->setSingleStep(0.05);
    m_pScale->setDecimals(2);
    m_pScale->setValue(l_oSt.value(QStringLiteral("export/scale"), 0.5).toDouble());
    m_pScale->setToolTip(tr("The picture's size: times the clip's pixels"));
    l_pF->addRow(tr("Scale"), m_pScale);
    m_pTransparent = new QCheckBox(tr("Transparent background"), this);
    m_pTransparent->setChecked(l_oSt.value(QStringLiteral("export/transparent"), true).toBool());
    l_pF->addRow(QString(), m_pTransparent);
    auto* l_pBg = m_pBg = new QPushButton(this);
    QColor l_oColour = l_oSt.value(QStringLiteral("export/background"), QColor(32, 32, 40)).value<QColor>();
    auto l_fnPaintButton = [l_pBg](const QColor& l_oC) { l_pBg->setText(l_oC.name()); l_pBg->setStyleSheet(QStringLiteral("background:%1").arg(l_oC.name())); };
    l_fnPaintButton(l_oColour);
    l_pBg->setProperty("colour", l_oColour);
    connect(l_pBg, &QPushButton::clicked, this, [this, l_pBg, l_fnPaintButton] {
        const QColor l_oC = QColorDialog::getColor(l_pBg->property("colour").value<QColor>(), this, tr("Background"));
        if (l_oC.isValid()) { l_pBg->setProperty("colour", l_oC); l_fnPaintButton(l_oC); }
    });
    l_pBg->setEnabled(!m_pTransparent->isChecked());
    connect(m_pTransparent, &QCheckBox::toggled, l_pBg, [l_pBg](bool l_bOn) { l_pBg->setEnabled(!l_bOn); });
    l_pF->addRow(tr("Background"), l_pBg);
    m_pLoop = new QCheckBox(tr("Play forever"), this);
    m_pLoop->setChecked(l_oSt.value(QStringLiteral("export/loop"), true).toBool());
    l_pF->addRow(QString(), m_pLoop);
    m_pHeld = new QCheckBox(tr("With the held clip (View > Held Clip)"), this);
    m_pHeld->setEnabled(!l_pDoc->f_HeldFile().isEmpty());
    m_pHeld->setChecked(!l_pDoc->f_HeldFile().isEmpty());
    m_pHeld->setToolTip(l_pDoc->f_HeldFile().isEmpty() ? tr("No held clip chosen") : l_pDoc->f_HeldFile());
    l_pF->addRow(QString(), m_pHeld);
    auto* l_pDirRow = new QHBoxLayout();
    m_pDir = new QLineEdit(this);
    const QString l_oDefDir = l_pDoc->f_FilePath().isEmpty() ? QDir::homePath() : QFileInfo(l_pDoc->f_FilePath()).absolutePath() + QStringLiteral("/export");
    m_pDir->setText(l_oSt.value(QStringLiteral("export/dir"), l_oDefDir).toString());
    auto* l_pBrowse = new QPushButton(tr("Browse..."), this);
    connect(l_pBrowse, &QPushButton::clicked, this, [this] {
        const QString l_oD = QFileDialog::getExistingDirectory(this, tr("Export to"), m_pDir->text());
        if (!l_oD.isEmpty()) m_pDir->setText(l_oD);
    });
    l_pDirRow->addWidget(m_pDir, 1);
    l_pDirRow->addWidget(l_pBrowse);
    l_pF->addRow(tr("Folder"), l_pDirRow);
    l_pV->addLayout(l_pF);

    auto* l_pButtons = new QDialogButtonBox(this);
    QPushButton* l_pGo = l_pButtons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    l_pButtons->addButton(QDialogButtonBox::Close);
    connect(l_pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(l_pGo, &QPushButton::clicked, this, [this, l_pBg] {
        QSettings l_oS(QStringLiteral("TOMS"), QStringLiteral("AnimEditor"));
        l_oS.setValue(QStringLiteral("export/format"), m_pFormat->currentIndex());
        l_oS.setValue(QStringLiteral("export/fps"), m_pFps->value());
        l_oS.setValue(QStringLiteral("export/scale"), m_pScale->value());
        l_oS.setValue(QStringLiteral("export/transparent"), m_pTransparent->isChecked());
        l_oS.setValue(QStringLiteral("export/background"), l_pBg->property("colour").value<QColor>());
        l_oS.setValue(QStringLiteral("export/loop"), m_pLoop->isChecked());
        l_oS.setValue(QStringLiteral("export/dir"), m_pDir->text());
        f_Run();
    });
    l_pV->addWidget(l_pButtons);
    resize(420, 520);
}

void ExportDialog::f_Run()
{
    Options l_oOpt;
    l_oOpt.m_eFormat = Format(m_pFormat->currentIndex());
    l_oOpt.m_iFps = m_pFps->value();
    l_oOpt.m_dScale = m_pScale->value();
    l_oOpt.m_bLoop = m_pLoop->isChecked();
    if (!m_pTransparent->isChecked()) l_oOpt.m_oBackground = m_pBg->property("colour").value<QColor>();
    std::vector<const toms::anim::Clip*> l_vClips;
    for (int l_iI = 0; l_iI < m_pClips->count(); l_iI++)
        if (m_pClips->item(l_iI)->checkState() == Qt::Checked)
            if (const toms::anim::Clip* l_pC = m_pDoc->f_File().f_Find(m_pClips->item(l_iI)->text().toStdString())) l_vClips.push_back(l_pC);
    if (l_vClips.empty()) {
        QMessageBox::information(this, windowTitle(), tr("Check the clips to export."));
        return;
    }
    const QString l_oHeld = m_pDoc->f_HeldFile();
    if (!m_pHeld->isChecked() && !l_oHeld.isEmpty()) m_pDoc->f_SetHeldFile(QString());   // without it this time
    QProgressDialog l_oProgress(tr("Exporting..."), tr("Cancel"), 0, 1000, this);
    l_oProgress.setWindowModality(Qt::WindowModal);
    l_oProgress.setMinimumDuration(0);
    QStringList l_oWritten, l_oFailed;
    for (size_t l_nK = 0; l_nK < l_vClips.size(); l_nK++) {
        QString l_oErr;
        const QString l_oName = QString::fromStdString(l_vClips[l_nK]->m_strName);
        const QString l_oP = f_ExportClip(m_pView, *m_pDoc, *l_vClips[l_nK], l_oOpt, m_pDir->text(), &l_oErr, [&](int l_iDone, int l_iTotal) {
            l_oProgress.setLabelText(tr("%1 (%2 of %3): frame %4 of %5").arg(l_oName).arg(l_nK + 1).arg(l_vClips.size()).arg(l_iDone).arg(l_iTotal));
            l_oProgress.setValue(int(1000.0 * (double(l_nK) + double(l_iDone) / std::max(1, l_iTotal)) / double(l_vClips.size())));
            return !l_oProgress.wasCanceled();
        });
        if (l_oProgress.wasCanceled()) break;
        if (l_oP.isEmpty()) l_oFailed << l_oName + QStringLiteral(": ") + l_oErr;
        else l_oWritten << QDir::toNativeSeparators(l_oP);
    }
    l_oProgress.close();
    if (!m_pHeld->isChecked() && !l_oHeld.isEmpty()) m_pDoc->f_SetHeldFile(l_oHeld);
    QString l_oText = l_oWritten.isEmpty() ? tr("Nothing was written.") : tr("Written:\n%1").arg(l_oWritten.join(QLatin1Char('\n')));
    if (!l_oFailed.isEmpty()) l_oText += tr("\n\nFailed:\n%1").arg(l_oFailed.join(QLatin1Char('\n')));
    QMessageBox::information(this, windowTitle(), l_oText);
}

}  // namespace animexport
