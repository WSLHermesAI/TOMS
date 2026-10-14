// mpdi_convert.h -- converts the old engine's (FM79979) .mpdi animations to .anim, and the .pi sprite sheets they
// draw from to the atlas tool's format (.atlasproj + .atlas + .png). docs/15_ANIMATION.md, "Converting MPDI".
//
// What an .mpdi becomes:
//   the file (MPDIList)                -> one .anim; its .pi files (ImageName) -> its atlases (id = the .pi's name)
//   MultiPathDynamicImage              -> a clip: TotalPlayTime -> length, PlayCount -> playCount (-1 = loop),
//                                         StayAtLastFrame -> stayAtLastFrame
//   CueToStartCurvesWithTime (a path)  -> a node with keys at StartTime + each point's time:
//        position  the image centre (MPDI 1.1+ stores the top-left of the untrimmed image; 1.0 and older the
//                  centre); linear, and LOD 2 / 3 curves are smoothed the engine's way (Chaikin) into more keys
//        rotation  Angle / Angles z, degrees
//        scale     Size / the sprite's trimmed size; Mirror = a negative x
//        colour    Color (0..1; the oldest format's 0..255)
//        sprite    PIName + ImageName -> "pi:image", stepped
//        size, rotation and colour are linear with BehaviorUpdate, else stepped (the point's values hold);
//        Loop -> the node loops; StartTime > 0 -> hidden until then; additive blending (destination GL_ONE)
//        -> Blend::Add; RotationPosOffset -> a group that turns, with the sprite offset inside it;
//        HintPoint -> an event key ("sound:<name>" for sound hints)
// Not converted (reported as warnings): x / y rotation (3D), a non-identity CameraView, ViewPort clipping, blend
// modes other than normal and additive.
#pragma once
#include <string>
#include <vector>

namespace animed {

struct MpdiConvertReport {
    int mpdiFiles = 0;          // .mpdi files found
    int animFiles = 0;          // .anim files written
    int clips = 0, nodes = 0;
    int piFiles = 0;            // .pi converted to atlases
    std::vector<std::string> written;    // every file written (the .anim, the .atlasproj / .atlas / .png ...)
    std::vector<std::string> warnings;   // "file: what was approximated or skipped"
    std::vector<std::string> errors;     // "file: why it was not converted"
};

// Converts one .mpdi (and the .pi files it uses, once each per call of convertMpdiFolder). outDir: where the .anim
// and the atlases go ("" = next to the .mpdi).
bool convertMpdiFile(const std::string& mpdiPath, const std::string& outDir, MpdiConvertReport& report);

// Every .mpdi under `folder` (subfolders too). outDir "" = next to each file; else the same subfolders under outDir.
MpdiConvertReport convertMpdiFolder(const std::string& folder, const std::string& outDir = std::string());

// One .pi -> <outDir>/<name>.atlasproj (embedded) + its packed .atlas and .png (+ .plist). Returns the .atlas path
// ("" on error).
std::string convertPi(const std::string& piPath, const std::string& outDir, MpdiConvertReport& report);

}  // namespace animed
