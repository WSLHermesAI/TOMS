// stage_save.h -- what the stage editor's Save writes, without Qt (docs/gameplay/02_STAGE_EDITOR.md, section 3).
//
//   changedFiles()   the floors' stage and spec files, the type records (enemies.json, items.json, npcs/*.json; a
//                    deleted NPC type removes its file) and text.json's keys changed here
//   writeFiles()     each file backed up to Build/editor_backups/<stamp>/ first; a floor spec the event editor saved
//                    meanwhile keeps its other changes (only this editor's events-list change is applied to the file
//                    as it is now); text.json gets this editor's keys merged into it as it is now
//   floor index      assets/data/events/floor_index.json, rebuilt from the files on disk exactly as
//                    tools/web_editors/floor_index.js builds it (byte for byte), written only when it changed
#pragma once

#include "stage_project.h"

#include <string>
#include <utility>
#include <vector>

namespace stage {

struct ChangedFile {
    enum Kind { Stage, Spec, Data, Delete, Text } m_eKind = Data;
    std::string m_strPath, m_strText, m_strBefore, m_strFloorId;
};
std::vector<ChangedFile> f_ChangedFiles(const Project& l_oP);

// A small LCS diff over lines with two lines of context around each change: ('+' | '-' | ' ', line); "…" marks a gap.
std::vector<std::pair<char, std::string>> f_LineDiff(const std::string& l_strBefore, const std::string& l_strAfter);

std::string f_TimeStamp();   // 2026-10-16_183005
// Writes every changed file; false + err on the first failure. `written` gets the paths written or removed.
bool f_WriteFiles(Project& l_oP, const std::string& l_strStamp, std::vector<std::string>& l_vWritten, std::string& l_strErr);

Json f_BuildFloorIndex(const std::string& l_strRoot);
// true when the index changed and was written
bool f_SyncFloorIndex(const std::string& l_strRoot, std::string* l_pErr = nullptr);

}  // namespace stage
