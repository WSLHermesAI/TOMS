// StageText.h -- the stage editor's messages in the user's language: the checks' and the story check's issues (codes
// from stage_core, the HTML editor's string keys) and a few shared words.
#pragma once

#include "stage_checks.h"

#include <QString>

namespace StageText {

QString f_Issue(const stage::Issue& l_oI);
QString f_KeyColor(const std::string& l_strC);   // "yellow" -> yellow / 黃
QString f_Q(const std::string& l_strS);          // std::string -> QString
std::string f_Lang();                       // "en" | "zh_TW": the editors' language now (Language.h)

}  // namespace StageText
