// StageText.h -- the stage editor's messages in the user's language: the checks' and the story check's issues (codes
// from stage_core, the HTML editor's string keys) and a few shared words.
#pragma once

#include "stage_checks.h"

#include <QString>

namespace StageText {

QString issue(const stage::Issue& i);
QString keyColor(const std::string& c);   // "yellow" -> yellow / 黃
QString q(const std::string& s);          // std::string -> QString
std::string lang();                       // "en" | "zh_TW": the editors' language now (Language.h)

}  // namespace StageText
