// atlas_cli.cpp -- the command line of the atlas tool: atlaspack, and `atlas_editor --headless`.
//
// Commands: new, add, refs, extract, embed, build, import, pack, info -- see kUsage below.
// Exit codes: 0 ok, 1 warnings and --strict, 2 errors, 3 bad command line.
#include "atlas_cli.h"
#include "atlas_build.h"
#include "atlas_export.h"
#include "atlas_store.h"
#include "atlas_file.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace atlas;
namespace fs = std::filesystem;

namespace {

const char* g_pszUsage =
    "atlaspack -- packs sprites into texture atlases (TOMS atlas tool)\n"
    "\n"
    "Projects (.atlasproj) normally keep their images inside the packed atlas next to them\n"
    "(embedded, like the old PI editor); folders are only references to import art from.\n"
    "\n"
    "  atlaspack new     <OUT.atlasproj> [--source DIR]... [--format LIST] [options]\n"
    "                    a new embedded project; --source folders are imported and kept as references\n"
    "  atlaspack add     <project> <png|folder>... [--prefix P] [--variant ID] [--replace]\n"
    "  atlaspack refs    <project> [--variant ID] [--import new|changed|all]\n"
    "                    compare the reference folders with the project; import from them\n"
    "  atlaspack extract <project> --out DIR [--variant ID] [--children]   every sprite as a PNG again\n"
    "  atlaspack embed   <folder-project> [--save-as NEW.atlasproj] [--keep-output]\n"
    "                    turn a folder project into an embedded one (the folders become references)\n"
    "  atlaspack build   <project> [--variant ID | --all-variants] [--out DIR] [--format LIST] [--strict] [--check]\n"
    "                    --check: write nothing; exit 1 if the files on disk differ from a fresh build\n"
    "  atlaspack import  <file.pi|.atlas|.json|.plist> --project OUT.atlasproj [--images DIR] [--format LIST]\n"
    "                    an embedded project from an existing atlas (--images: a folder project instead)\n"
    "  atlaspack pack    <folder>... --out DIR [--name NAME] [--format LIST] [options] [--save-project FILE]\n"
    "                    pack folders straight to files (a folder project)\n"
    "  atlaspack info    <file.pi|.atlas|.json>\n"
    "\n"
    "formats (LIST is comma separated): atlas, atlas-spine3, plist, tp-json, pi, rcss\n"
    "options: --max WxH  --pot  --square  --padding N  --border N  --extrude N  --no-trim\n"
    "         --alpha-threshold N  --no-dedupe  --heuristic auto|bssf|blsf|baf|bl|cp|shelf  --filter Nearest|Linear\n"
    "exit codes: 0 ok, 1 warnings with --strict, 2 errors, 3 bad command line\n";

struct Args {
    std::vector<std::string> m_vPos;
    std::vector<std::pair<std::string, std::string>> m_vOpts;   // --key value (value "" for flags)

    bool f_Has(const std::string& l_strK) const {
        for (const auto& l_pairO : m_vOpts) if (l_pairO.first == l_strK) return true;
        return false;
    }
    std::string f_Get(const std::string& l_strK, const std::string& l_strDef = std::string()) const {
        for (const auto& l_pairO : m_vOpts) if (l_pairO.first == l_strK) return l_pairO.second;
        return l_strDef;
    }
    std::vector<std::string> f_All(const std::string& l_strK) const {
        std::vector<std::string> l_vV;
        for (const auto& l_pairO : m_vOpts) if (l_pairO.first == l_strK) l_vV.push_back(l_pairO.second);
        return l_vV;
    }
};

const std::vector<std::string> g_vFlags = {"--pot", "--square", "--no-trim", "--no-dedupe", "--strict", "--all-variants",
                                         "--quiet", "--help", "--check", "--replace", "--children", "--keep-output",
                                         "--bin"};

bool f_ParseArgs(const std::vector<std::string>& l_vArgv, size_t l_nFrom, Args& l_oA, std::string& l_strErr) {
    const size_t l_nArgc = l_vArgv.size();
    for (size_t l_nI = l_nFrom; l_nI < l_nArgc; l_nI++) {
        std::string l_strS = l_vArgv[l_nI];
        if (l_strS.rfind("--", 0) == 0) {
            std::string l_strVal;
            const size_t l_nEq = l_strS.find('=');
            if (l_nEq != std::string::npos) { l_strVal = l_strS.substr(l_nEq + 1); l_strS = l_strS.substr(0, l_nEq); }
            else if (std::find(g_vFlags.begin(), g_vFlags.end(), l_strS) == g_vFlags.end()) {
                if (l_nI + 1 >= l_nArgc) { l_strErr = l_strS + " needs a value"; return false; }
                l_strVal = l_vArgv[++l_nI];
            }
            l_oA.m_vOpts.emplace_back(l_strS, l_strVal);
        } else {
            l_oA.m_vPos.push_back(l_strS);
        }
    }
    return true;
}

std::vector<std::string> f_SplitList(const std::string& l_strS) {
    std::vector<std::string> l_vV;
    std::string l_strCur;
    for (char l_cC : l_strS + ",") {
        if (l_cC == ',') { if (!l_strCur.empty()) l_vV.push_back(l_strCur); l_strCur.clear(); }
        else if (l_cC != ' ') l_strCur += l_cC;
    }
    return l_vV;
}

bool f_ToInt(const std::string& l_strS, int& l_iOut) {
    char* l_pszEnd = nullptr;
    const long l_iV = strtol(l_strS.c_str(), &l_pszEnd, 10);
    if (l_strS.empty() || *l_pszEnd) return false;
    l_iOut = (int)l_iV;
    return true;
}

// Applies packing options to settings; false on a bad value.
bool f_ApplyPackOptions(const Args& l_oA, Settings& l_oS, std::string& l_strErr) {
    if (l_oA.f_Has("--max")) {
        const std::string l_strM = l_oA.f_Get("--max");
        const size_t l_nX = l_strM.find_first_of("xX");
        int l_iW = 0, l_iH = 0;
        if (l_nX == std::string::npos ? !f_ToInt(l_strM, l_iW) : (!f_ToInt(l_strM.substr(0, l_nX), l_iW) || !f_ToInt(l_strM.substr(l_nX + 1), l_iH))) { l_strErr = "bad --max " + l_strM; return false; }
        l_oS.m_iMaxWidth = l_iW; l_oS.m_iMaxHeight = l_iH ? l_iH : l_iW;
    }
    if (l_oA.f_Has("--pot")) l_oS.m_bPowerOfTwo = true;
    if (l_oA.f_Has("--square")) l_oS.m_bSquare = true;
    if (l_oA.f_Has("--no-trim")) l_oS.m_bTrim = false;
    if (l_oA.f_Has("--no-dedupe")) l_oS.m_bDedupe = false;
    for (const char* l_pszK : {"--padding", "--border", "--extrude", "--alpha-threshold"}) {
        if (!l_oA.f_Has(l_pszK)) continue;
        int l_iV;
        if (!f_ToInt(l_oA.f_Get(l_pszK), l_iV) || l_iV < 0) { l_strErr = std::string("bad ") + l_pszK + " " + l_oA.f_Get(l_pszK); return false; }
        if (!strcmp(l_pszK, "--padding")) l_oS.m_iPadding = l_iV;
        else if (!strcmp(l_pszK, "--border")) l_oS.m_iBorder = l_iV;
        else if (!strcmp(l_pszK, "--extrude")) l_oS.m_iExtrude = l_iV;
        else l_oS.m_iAlphaThreshold = l_iV;
    }
    if (l_oA.f_Has("--heuristic") && !f_ParseHeuristic(l_oA.f_Get("--heuristic"), l_oS.m_eHeuristic)) { l_strErr = "bad --heuristic " + l_oA.f_Get("--heuristic"); return false; }
    if (l_oA.f_Has("--filter")) l_oS.m_strFilter = l_oA.f_Get("--filter");
    return true;
}

bool f_CheckFormats(const std::vector<std::string>& l_vF, std::string& l_strErr) {
    for (const std::string& l_strX : l_vF)
        if (!f_IsExportFormat(l_strX)) { l_strErr = "unknown format '" + l_strX + "'"; return false; }
    return true;
}

// --check: are the files on disk what a build would write? Lists the ones that are not.
bool f_UpToDate(const std::string& l_strDir, const std::vector<ExportFile>& l_vFiles, const std::string& l_strLabel) {
    bool l_bOk = true;
    for (const ExportFile& l_oF : l_vFiles) {
        std::ifstream l_oIn(f_U8path(f_JoinPath(l_strDir, l_oF.m_strName)), std::ios::binary);
        const std::vector<uint8_t> l_vOld((std::istreambuf_iterator<char>(l_oIn)), std::istreambuf_iterator<char>());
        if (l_oIn.is_open() && l_vOld == l_oF.m_vData) continue;
        std::fprintf(stderr, "%s: error: %s is %s -- run atlaspack build\n", l_strLabel.c_str(), f_JoinPath(l_strDir, l_oF.m_strName).c_str(),
                     l_oIn.is_open() ? "out of date" : "missing");
        l_bOk = false;
    }
    return l_bOk;
}

// Builds one atlas and writes it (or with `check`, only compares). Returns the exit code.
int f_BuildAndWrite(const Project& l_oP, const std::string& l_strVariant, const std::string& l_strOutOverride,
                  const std::vector<std::string>& l_vFormatsOverride, bool l_bStrict, bool l_bQuiet, bool l_bCheck = false) {
    const Variant* l_pV = l_strVariant.empty() ? nullptr : l_oP.f_FindVariant(l_strVariant);
    const Output& l_oO = l_pV ? l_pV->m_oOutput : l_oP.m_oOutput;
    const std::string l_strLabel = l_oP.m_strFilePath.empty() ? l_oP.m_strName : l_oP.m_strFilePath;
    BuildResult l_oB = f_Build(l_oP, BuildOptions{l_strVariant, true});
    for (const Diagnostic& l_oD : l_oB.m_vDiagnostics)
        if (l_oD.m_eLevel != Diagnostic::Info || !l_bQuiet) std::fprintf(stderr, "%s: %s\n", l_strLabel.c_str(), f_FormatDiagnostic(l_oD).c_str());
    if (!l_oB.f_Ok()) {
        std::fprintf(stderr, "%s: %d error(s); nothing written\n", l_strLabel.c_str(), l_oB.f_ErrorCount());
        return 2;
    }
    std::vector<std::string> l_vFormats = l_vFormatsOverride.empty() ? l_oO.m_vFormats : l_vFormatsOverride;
    // An embedded project's images live in its .atlas: keep writing it to the project's own folder.
    if (l_oP.m_bEmbedded && l_strOutOverride.empty() && std::find(l_vFormats.begin(), l_vFormats.end(), "atlas") == l_vFormats.end())
        l_vFormats.push_back("atlas");
    std::vector<ExportFile> l_vFiles;
    std::string l_strErr;
    if (!f_ExportFiles(l_oB, l_vFormats, l_vFiles, &l_strErr)) { std::fprintf(stderr, "%s: error: %s\n", l_strLabel.c_str(), l_strErr.c_str()); return 2; }
    const std::string l_strDir = l_strOutOverride.empty() ? l_oP.f_Resolve(l_oO.m_strDir) : l_strOutOverride;
    if (l_bCheck) {
        if (!f_UpToDate(l_strDir, l_vFiles, l_strLabel)) return 1;
        if (!l_bQuiet) std::printf("%s -> %s: up to date%s%s\n", l_strLabel.c_str(), l_strDir.c_str(), l_strVariant.empty() ? "" : ", variant ", l_strVariant.c_str());
        return l_bStrict && l_oB.f_WarningCount() > 0 ? 1 : 0;
    }
    if (!f_WriteFiles(l_strDir, l_vFiles, &l_strErr)) { std::fprintf(stderr, "%s: error: %s\n", l_strLabel.c_str(), l_strErr.c_str()); return 2; }
    if (!l_bQuiet) {
        std::printf("%s -> %s: %zu sprite(s), %zu page(s)", l_strLabel.c_str(), l_strDir.c_str(), l_oB.m_vRegions.size(), l_oB.m_vPages.size());
        if (!l_strVariant.empty()) std::printf(" (variant %s)", l_strVariant.c_str());
        for (const Page& l_oPg : l_oB.m_vPages) std::printf(" %s %dx%d", l_oPg.m_strFile.c_str(), l_oPg.m_oImage.m_iW, l_oPg.m_oImage.m_iH);
        std::printf("\n");
    }
    return l_bStrict && l_oB.f_WarningCount() > 0 ? 1 : 0;
}

int f_CmdBuild(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1) { std::fprintf(stderr, "build: give one project file\n"); return 3; }
    Project l_oP;
    std::string l_strErr;
    if (!f_OpenProject(l_oA.m_vPos[0], l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    const std::vector<std::string> l_vFormats = f_SplitList(l_oA.f_Get("--format"));
    if (!f_CheckFormats(l_vFormats, l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 3; }
    const bool l_bStrict = l_oA.f_Has("--strict"), l_bQuiet = l_oA.f_Has("--quiet");
    const std::string l_strOut = l_oA.f_Get("--out");
    std::vector<std::string> l_vVariants;
    if (l_oA.f_Has("--variant")) l_vVariants.push_back(l_oA.f_Get("--variant"));
    else {
        l_vVariants.push_back("");
        if (l_oA.f_Has("--all-variants"))
            for (const Variant& l_oV : l_oP.m_vVariants) l_vVariants.push_back(l_oV.m_strId);
    }
    if (!l_strOut.empty() && l_vVariants.size() > 1) { std::fprintf(stderr, "build: --out cannot be used with --all-variants\n"); return 3; }
    int l_iCode = 0;
    for (const std::string& l_strV : l_vVariants)
        l_iCode = std::max(l_iCode, f_BuildAndWrite(l_oP, l_strV, l_strOut, l_vFormats, l_bStrict, l_bQuiet, l_oA.f_Has("--check")));
    return l_iCode;
}

int f_CmdPack(const Args& l_oA) {
    if (l_oA.m_vPos.empty() || !l_oA.f_Has("--out")) { std::fprintf(stderr, "pack: give at least one folder and --out DIR\n"); return 3; }
    Project l_oP;
    std::string l_strErr;
    const std::string l_strOutDir = f_NormalizePath(l_oA.f_Get("--out"));
    l_oP.m_strName = l_oA.f_Get("--name", f_FileStem(f_U8str(f_U8path(l_oA.m_vPos[0]).lexically_normal())));
    if (l_oP.m_strName.empty() || l_oP.m_strName == ".") l_oP.m_strName = "atlas";
    if (!f_ApplyPackOptions(l_oA, l_oP.m_oSettings, l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 3; }
    if (l_oA.f_Has("--format")) l_oP.m_oOutput.m_vFormats = f_SplitList(l_oA.f_Get("--format"));
    if (!f_CheckFormats(l_oP.m_oOutput.m_vFormats, l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 3; }
    const std::string l_strSave = l_oA.f_Get("--save-project");
    l_oP.m_strFilePath = l_strSave.empty() ? std::string() : f_NormalizePath(l_strSave);
    for (const std::string& l_strDir : l_oA.m_vPos) {
        const std::string l_strAbs = f_U8str(fs::absolute(f_U8path(l_strDir)).lexically_normal());
        l_oP.m_vSources.push_back({l_oP.m_strFilePath.empty() ? l_strAbs : l_oP.f_Relativize(l_strAbs), true, ""});
    }
    const std::string l_strAbsOut = f_U8str(fs::absolute(f_U8path(l_strOutDir)).lexically_normal());
    l_oP.m_oOutput.m_strDir = l_oP.m_strFilePath.empty() ? l_strAbsOut : l_oP.f_Relativize(l_strAbsOut);
    if (!l_strSave.empty() && !f_SaveProject(l_strSave, l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    return f_BuildAndWrite(l_oP, "", "", {}, l_oA.f_Has("--strict"), l_oA.f_Has("--quiet"));
}

// Saves an embedded project with its packed atlas and reports it. Returns the exit code.
int f_SaveAll(const std::string& l_strPath, Project& l_oP, bool l_bQuiet) {
    SaveResult l_oR;
    std::string l_strErr;
    const bool l_bOk = f_SaveProjectAll(l_strPath, l_oP, {}, &l_oR, &l_strErr);
    for (const Diagnostic& l_oD : l_oR.m_vDiagnostics)
        if (l_oD.m_eLevel != Diagnostic::Info || !l_bQuiet) std::fprintf(stderr, "%s: %s\n", l_strPath.c_str(), f_FormatDiagnostic(l_oD).c_str());
    if (!l_bOk) { std::fprintf(stderr, "%s: error: %s; nothing written\n", l_strPath.c_str(), l_strErr.c_str()); return 2; }
    if (!l_bQuiet) for (const std::string& l_strF : l_oR.m_vWritten) std::printf("  wrote %s\n", l_strF.c_str());
    return 0;
}

int f_CmdImport(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1 || !l_oA.f_Has("--project")) { std::fprintf(stderr, "import: give one atlas file and --project OUT.atlasproj\n"); return 3; }
    BuildResult l_oIn;
    std::string l_strErr;
    if (!f_ImportAtlasFile(l_oA.m_vPos[0], l_oIn, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    for (const Diagnostic& l_oD : l_oIn.m_vDiagnostics) std::fprintf(stderr, "%s: %s\n", l_oA.m_vPos[0].c_str(), f_FormatDiagnostic(l_oD).c_str());
    const std::string l_strProjPath = l_oA.f_Get("--project");
    const std::string l_strImages = l_oA.f_Get("--images");   // empty: keep the images inside the project
    Project l_oP;
    if (!f_ImportToProject(l_oIn, l_strProjPath, l_strImages, l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    std::string l_strExt = f_U8str(f_U8path(l_oA.m_vPos[0]).extension());
    for (char& l_cC : l_strExt) l_cC = (char)tolower((unsigned char)l_cC);
    l_oP.m_oOutput.m_vFormats = l_oA.f_Has("--format") ? f_SplitList(l_oA.f_Get("--format"))
                                         : std::vector<std::string>{l_strExt == ".pi" ? "pi" : l_strExt == ".json" ? "tp-json" : "atlas"};
    if (!f_CheckFormats(l_oP.m_oOutput.m_vFormats, l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 3; }
    l_oP.m_oOutput.m_strDir = ".";
    int l_iChildren = 0;
    for (const Region& l_oR : l_oIn.m_vRegions) l_iChildren += l_oR.m_bChild;
    std::printf("%s -> %s: %zu sprite(s) (%d child)%s%s\n", l_oA.m_vPos[0].c_str(), l_strProjPath.c_str(), l_oIn.m_vRegions.size(), l_iChildren,
                l_strImages.empty() ? "" : ", images in ", l_strImages.c_str());
    if (!l_oP.m_bEmbedded) {
        if (!f_SaveProject(l_strProjPath, l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
        return 0;
    }
    return f_SaveAll(l_strProjPath, l_oP, l_oA.f_Has("--quiet"));
}

int f_CmdInfo(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1) { std::fprintf(stderr, "info: give one atlas file\n"); return 3; }
    BuildResult l_oIn;
    std::string l_strErr;
    if (!f_ImportAtlasFile(l_oA.m_vPos[0], l_oIn, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    for (size_t l_nI = 0; l_nI < l_oIn.m_vPages.size(); l_nI++)
        std::printf("page %zu: %s %dx%d\n", l_nI, l_oIn.m_vPages[l_nI].m_strFile.c_str(), l_oIn.m_vPages[l_nI].m_oImage.m_iW, l_oIn.m_vPages[l_nI].m_oImage.m_iH);
    int l_iBad = 0;
    for (const Region& l_oR : l_oIn.m_vRegions) {
        std::printf("  %-32s page %d  frame %d,%d %dx%d  orig %dx%d  offset %d,%d", l_oR.m_strName.c_str(), l_oR.m_iPage, l_oR.m_oFrame.m_iX,
                    l_oR.m_oFrame.m_iY, l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH, l_oR.m_iOrigW, l_oR.m_iOrigH, l_oR.m_iOffsetX, l_oR.m_iOffsetY);
        if (l_oR.m_bChild) std::printf("  child of %s @ %d,%d", l_oR.m_strParent.c_str(), l_oR.m_oLocal.m_iX, l_oR.m_oLocal.m_iY);
        std::printf("\n");
        const Image& l_oPg = l_oIn.m_vPages[l_oR.m_iPage].m_oImage;
        if (l_oR.m_oFrame.m_iX < 0 || l_oR.m_oFrame.m_iY < 0 || l_oR.m_oFrame.f_Right() > l_oPg.m_iW || l_oR.m_oFrame.f_Bottom() > l_oPg.m_iH) {
            std::fprintf(stderr, "%s: error: %s: frame lies outside its page\n", l_oA.m_vPos[0].c_str(), l_oR.m_strName.c_str());
            l_iBad++;
        }
        if (l_oR.m_iOffsetX + l_oR.m_oFrame.m_iW > l_oR.m_iOrigW || l_oR.m_iOffsetY + l_oR.m_oFrame.m_iH > l_oR.m_iOrigH) {
            std::fprintf(stderr, "%s: warning: %s: trimmed rect is larger than the original size\n", l_oA.m_vPos[0].c_str(), l_oR.m_strName.c_str());
        }
    }
    for (const Animation& l_oAn : l_oIn.m_vAnimations) std::printf("animation %s: %zu frame(s)\n", l_oAn.m_strName.c_str(), l_oAn.m_vFrames.size());
    for (const Diagnostic& l_oD : l_oIn.m_vDiagnostics) std::fprintf(stderr, "%s: %s\n", l_oA.m_vPos[0].c_str(), f_FormatDiagnostic(l_oD).c_str());
    return l_iBad ? 2 : 0;
}

// Adds every PNG from `paths` (files, or folders searched below) to the project. Returns how many.
int f_AddPngs(Project& l_oP, const std::vector<std::string>& l_vPaths, const std::string& l_strPrefix, const std::string& l_strVariant,
            bool l_bReplace, bool l_bQuiet, int& l_iErrors) {
    const ImageSource l_oSrc = f_FileImageSource();
    int l_iAdded = 0;
    for (const std::string& l_strArg : l_vPaths) {
        std::vector<std::pair<std::string, std::string>> l_vFiles;   // file, name
        std::error_code l_oEc;
        if (fs::is_directory(f_U8path(l_strArg), l_oEc)) {
            const std::string l_strDir = f_U8str(fs::absolute(f_U8path(l_strArg)).lexically_normal());
            for (const std::string& l_strF : l_oSrc.m_fnListPngs(l_strDir, true)) l_vFiles.push_back({l_strF, f_ImageNameFor(l_strF, l_strDir, l_strPrefix)});
        } else {
            l_vFiles.push_back({l_strArg, f_ImageNameFor(l_strArg, "", l_strPrefix)});
        }
        for (const auto& [l_strFile, l_strName] : l_vFiles) {
            const bool l_bExists = l_strVariant.empty() ? l_oP.m_mapImages.count(l_strName) > 0
                                                : (l_oP.f_FindVariant(l_strVariant) && l_oP.f_FindVariant(l_strVariant)->m_mapImages.count(l_strName) > 0);
            if (l_bExists && !l_bReplace) {
                if (!l_bQuiet) std::printf("  skipped %s (already in the project; --replace to update)\n", l_strName.c_str());
                continue;
            }
            Image l_oImg;
            std::string l_strErr;
            if (!f_LoadImage(l_strFile, l_oImg, &l_strErr) || !f_SetImage(l_oP, l_strName, l_oImg, l_strVariant, &l_strErr)) {
                std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
                l_iErrors++;
                continue;
            }
            if (!l_bQuiet) std::printf("  %s %s\n", l_bExists ? "replaced" : "added", l_strName.c_str());
            l_iAdded++;
        }
    }
    return l_iAdded;
}

int f_CmdNew(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1) { std::fprintf(stderr, "new: give the project file to create\n"); return 3; }
    const std::string l_strPath = l_oA.m_vPos[0];
    if (fs::exists(f_U8path(l_strPath))) { std::fprintf(stderr, "new: %s already exists\n", l_strPath.c_str()); return 2; }
    Project l_oP;
    l_oP.m_strFilePath = f_NormalizePath(l_strPath);
    l_oP.m_strName = f_FileStem(l_strPath);
    l_oP.m_bEmbedded = true;
    std::string l_strErr;
    if (!f_ApplyPackOptions(l_oA, l_oP.m_oSettings, l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 3; }
    if (l_oA.f_Has("--format")) l_oP.m_oOutput.m_vFormats = f_SplitList(l_oA.f_Get("--format"));
    if (!f_CheckFormats(l_oP.m_oOutput.m_vFormats, l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 3; }
    // --source folders: their PNGs go in now, and the folders stay as references.
    int l_iErrors = 0;
    const std::vector<std::string> l_vSources = l_oA.f_All("--source");
    f_AddPngs(l_oP, l_vSources, "", "", false, l_oA.f_Has("--quiet"), l_iErrors);
    for (const std::string& l_strS : l_vSources) l_oP.m_vReferences.push_back({l_oP.f_Relativize(f_U8str(fs::absolute(f_U8path(l_strS)))), true, ""});
    if (l_iErrors) return 2;
    return f_SaveAll(l_strPath, l_oP, l_oA.f_Has("--quiet"));
}

int f_CmdEmbed(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1) { std::fprintf(stderr, "embed: give one project file\n"); return 3; }
    Project l_oP;
    std::string l_strErr;
    if (!f_LoadProject(l_oA.m_vPos[0], l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    if (l_oP.m_bEmbedded) { std::fprintf(stderr, "embed: %s is already an embedded project\n", l_oA.m_vPos[0].c_str()); return 2; }
    std::vector<Diagnostic> l_vDiags;
    const int l_iN = f_EmbedSources(l_oP, f_FileImageSource(), &l_vDiags);
    int l_iErrors = 0;
    for (const Diagnostic& l_oD : l_vDiags) {
        std::fprintf(stderr, "%s: %s\n", l_oA.m_vPos[0].c_str(), f_FormatDiagnostic(l_oD).c_str());
        l_iErrors += l_oD.m_eLevel == Diagnostic::Error;
    }
    if (l_iErrors) { std::fprintf(stderr, "%s: %d error(s); nothing written\n", l_oA.m_vPos[0].c_str(), l_iErrors); return 2; }
    const std::string l_strTarget = l_oA.f_Get("--save-as", l_oA.m_vPos[0]);
    // The packed atlas (the store) goes next to the project file, unless asked to keep the output folder.
    if (!l_oA.f_Has("--keep-output")) l_oP.m_oOutput.m_strDir = l_oP.f_Relativize(f_U8str(fs::absolute(f_U8path(f_ParentDir(l_strTarget)))));
    std::printf("%s: %d image(s) taken in\n", l_oA.m_vPos[0].c_str(), l_iN);
    return f_SaveAll(l_strTarget, l_oP, l_oA.f_Has("--quiet"));
}

int f_CmdAdd(const Args& l_oA) {
    if (l_oA.m_vPos.size() < 2) { std::fprintf(stderr, "add: give the project and at least one PNG or folder\n"); return 3; }
    Project l_oP;
    std::string l_strErr;
    if (!f_OpenProject(l_oA.m_vPos[0], l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    if (!l_oP.m_bEmbedded) { std::fprintf(stderr, "add: %s reads its images from folders; put the PNGs there (or run embed first)\n", l_oA.m_vPos[0].c_str()); return 2; }
    int l_iErrors = 0;
    const std::vector<std::string> l_vPaths(l_oA.m_vPos.begin() + 1, l_oA.m_vPos.end());
    const int l_iN = f_AddPngs(l_oP, l_vPaths, l_oA.f_Get("--prefix"), l_oA.f_Get("--variant"), l_oA.f_Has("--replace"), l_oA.f_Has("--quiet"), l_iErrors);
    if (l_iErrors) return 2;
    if (l_iN == 0) { std::printf("nothing to add\n"); return 0; }
    return f_SaveAll(l_oA.m_vPos[0], l_oP, l_oA.f_Has("--quiet"));
}

int f_CmdRefs(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1) { std::fprintf(stderr, "refs: give one project file\n"); return 3; }
    Project l_oP;
    std::string l_strErr;
    if (!f_OpenProject(l_oA.m_vPos[0], l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    const std::string l_strVariant = l_oA.f_Get("--variant");
    if (!l_strVariant.empty() && !l_oP.f_FindVariant(l_strVariant)) { std::fprintf(stderr, "error: unknown variant '%s'\n", l_strVariant.c_str()); return 2; }
    const std::string l_strMode = l_oA.f_Get("--import");   // new | changed | all
    if (!l_strMode.empty() && l_strMode != "new" && l_strMode != "changed" && l_strMode != "all") { std::fprintf(stderr, "refs: --import new|changed|all\n"); return 3; }
    const char* l_arrNames[] = {"new", "changed", "same"};
    int l_iTaken = 0;
    for (const ReferenceImage& l_oR : f_ScanReferences(l_oP, f_FileImageSource(), l_strVariant)) {
        std::printf("  %-8s %-32s %s\n", l_arrNames[l_oR.m_eStatus], l_oR.m_strName.c_str(), l_oR.m_strFile.c_str());
        const bool l_bWant = (l_oR.m_eStatus == ReferenceImage::New && (l_strMode == "new" || l_strMode == "all")) ||
                          (l_oR.m_eStatus == ReferenceImage::Changed && (l_strMode == "changed" || l_strMode == "all"));
        if (!l_bWant) continue;
        Image l_oImg;
        if (!f_LoadImage(l_oR.m_strFile, l_oImg, &l_strErr) || !f_SetImage(l_oP, l_oR.m_strName, l_oImg, l_strVariant, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
        l_iTaken++;
    }
    if (!l_iTaken) return 0;
    std::printf("imported %d image(s)\n", l_iTaken);
    return f_SaveAll(l_oA.m_vPos[0], l_oP, l_oA.f_Has("--quiet"));
}

int f_CmdExtract(const Args& l_oA) {
    if (l_oA.m_vPos.size() != 1 || !l_oA.f_Has("--out")) { std::fprintf(stderr, "extract: give one project file and --out DIR\n"); return 3; }
    Project l_oP;
    std::string l_strErr;
    if (!f_OpenProject(l_oA.m_vPos[0], l_oP, &l_strErr)) { std::fprintf(stderr, "error: %s\n", l_strErr.c_str()); return 2; }
    std::vector<std::string> l_vWritten;
    if (!f_ExtractImages(l_oP, l_oA.f_Get("--out"), l_oA.f_Get("--variant"), l_oA.f_Has("--children"), &l_vWritten, &l_strErr)) {
        std::fprintf(stderr, "error: %s\n", l_strErr.c_str());
        return 2;
    }
    std::printf("%zu image(s) written to %s\n", l_vWritten.size(), l_oA.f_Get("--out").c_str());
    return 0;
}

}  // namespace

std::vector<std::string> f_Utf8CommandLine(int l_iArgc, char** l_pArgv) {
    std::vector<std::string> l_vOut;
#ifdef _WIN32
    // argv is in the ANSI code page; the wide command line keeps every character.
    int l_iN = 0;
    if (LPWSTR* l_pW = CommandLineToArgvW(GetCommandLineW(), &l_iN)) {
        for (int l_iI = 1; l_iI < l_iN; l_iI++) {
            const int l_iLen = WideCharToMultiByte(CP_UTF8, 0, l_pW[l_iI], -1, nullptr, 0, nullptr, nullptr);
            std::string l_strS(l_iLen > 0 ? (size_t)l_iLen - 1 : 0, '\0');
            if (l_iLen > 1) WideCharToMultiByte(CP_UTF8, 0, l_pW[l_iI], -1, &l_strS[0], l_iLen, nullptr, nullptr);
            l_vOut.push_back(l_strS);
        }
        LocalFree(l_pW);
        SetConsoleOutputCP(CP_UTF8);
        return l_vOut;
    }
#endif
    for (int l_iI = 1; l_iI < l_iArgc; l_iI++) l_vOut.push_back(l_pArgv[l_iI]);
    return l_vOut;
}

int f_AtlasCliMain(const std::vector<std::string>& l_vArgs) {
    if (l_vArgs.empty() || l_vArgs[0] == "--help" || l_vArgs[0] == "-h" || l_vArgs[0] == "help") {
        std::fputs(g_pszUsage, l_vArgs.empty() ? stderr : stdout);
        return l_vArgs.empty() ? 3 : 0;
    }
    const std::string& l_strCmd = l_vArgs[0];
    Args l_oA;
    std::string l_strErr;
    if (!f_ParseArgs(l_vArgs, 1, l_oA, l_strErr)) { std::fprintf(stderr, "error: %s\n%s", l_strErr.c_str(), g_pszUsage); return 3; }
    if (l_strCmd == "build") return f_CmdBuild(l_oA);
    if (l_strCmd == "pack") return f_CmdPack(l_oA);
    if (l_strCmd == "import") return f_CmdImport(l_oA);
    if (l_strCmd == "info") return f_CmdInfo(l_oA);
    if (l_strCmd == "new") return f_CmdNew(l_oA);
    if (l_strCmd == "embed") return f_CmdEmbed(l_oA);
    if (l_strCmd == "add") return f_CmdAdd(l_oA);
    if (l_strCmd == "refs") return f_CmdRefs(l_oA);
    if (l_strCmd == "extract") return f_CmdExtract(l_oA);
    if (l_strCmd == "formats") { for (const std::string& l_strF : f_ExportFormats()) std::printf("%s\n", l_strF.c_str()); return 0; }
    std::fprintf(stderr, "unknown command '%s'\n%s", l_strCmd.c_str(), g_pszUsage);
    return 3;
}
