#pragma once
// The interface language. Settings > General > Language stores "system" (follow the
// operating system's display language) or one language code; it applies at the next start.
// Translations are compiled into the program (resources :/i18n/coreplay_<code>.qm).
// Names of products, tools, formats, keys and colour spaces stay in English.
#include <QString>
#include <vector>

class QApplication;

namespace I18n {

struct Language {
    QString code;        // "en", "zh_TW", "ar", ...
    QString nativeName;  // the language's name in its own script
};

const std::vector<Language>& languages();  // English first, then the 16 translations
QString setting();                          // "system" or a code (QSettings "ui/language")
void setSetting(const QString& value);
QString systemLanguage();                   // the OS display language, matched to the list
QString resolve(const QString& value);      // "system" -> systemLanguage(), else the code
QString current();                          // the language in use now
// Loads the translation for the setting (tests always run in English). Call once, after
// the application object exists and before any window is made.
void install(QApplication& app, bool testRun);

}  // namespace I18n
