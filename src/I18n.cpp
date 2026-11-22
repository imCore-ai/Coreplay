#include "I18n.h"

#include <QApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace I18n {
namespace {
QString g_current = "en";

// Qt's own texts that Coreplay's windows show (dialog buttons, the menus of text fields
// and spin boxes). Qt's translation files do not cover all of Coreplay's languages, so
// these are translated in Coreplay's own files; listed here so lupdate keeps them.
[[maybe_unused]] const char* const kQtTexts[] = {
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK"), QT_TRANSLATE_NOOP("QPlatformTheme", "Save"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Save All"), QT_TRANSLATE_NOOP("QPlatformTheme", "Open"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&Yes"), QT_TRANSLATE_NOOP("QPlatformTheme", "Yes to &All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&No"), QT_TRANSLATE_NOOP("QPlatformTheme", "N&o to All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Abort"), QT_TRANSLATE_NOOP("QPlatformTheme", "Retry"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Ignore"), QT_TRANSLATE_NOOP("QPlatformTheme", "Close"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel"), QT_TRANSLATE_NOOP("QPlatformTheme", "Discard"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Help"), QT_TRANSLATE_NOOP("QPlatformTheme", "Apply"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Reset"), QT_TRANSLATE_NOOP("QPlatformTheme", "Restore Defaults"),
    QT_TRANSLATE_NOOP("QLineEdit", "&Undo"), QT_TRANSLATE_NOOP("QLineEdit", "&Redo"),
    QT_TRANSLATE_NOOP("QLineEdit", "Cu&t"), QT_TRANSLATE_NOOP("QLineEdit", "&Copy"),
    QT_TRANSLATE_NOOP("QLineEdit", "&Paste"), QT_TRANSLATE_NOOP("QLineEdit", "Delete"),
    QT_TRANSLATE_NOOP("QLineEdit", "Select All"),
    QT_TRANSLATE_NOOP("QWidgetTextControl", "&Undo"), QT_TRANSLATE_NOOP("QWidgetTextControl", "&Redo"),
    QT_TRANSLATE_NOOP("QWidgetTextControl", "Cu&t"), QT_TRANSLATE_NOOP("QWidgetTextControl", "&Copy"),
    QT_TRANSLATE_NOOP("QWidgetTextControl", "Copy &Link Location"),
    QT_TRANSLATE_NOOP("QWidgetTextControl", "&Paste"), QT_TRANSLATE_NOOP("QWidgetTextControl", "Delete"),
    QT_TRANSLATE_NOOP("QWidgetTextControl", "Select All"),
    QT_TRANSLATE_NOOP("QAbstractSpinBox", "&Select All"), QT_TRANSLATE_NOOP("QAbstractSpinBox", "&Step up"),
    QT_TRANSLATE_NOOP("QAbstractSpinBox", "Step &down"),
};
}  // namespace

const std::vector<Language>& languages() {
    static const std::vector<Language> list = {
        {"en", "English"},
        {"zh_TW", QString::fromUtf8("繁體中文")},
        {"zh_CN", QString::fromUtf8("简体中文")},
        {"ja", QString::fromUtf8("日本語")},
        {"ko", QString::fromUtf8("한국어")},
        {"es", QString::fromUtf8("Español")},
        {"fr", QString::fromUtf8("Français")},
        {"de", "Deutsch"},
        {"pt", QString::fromUtf8("Português")},
        {"ru", QString::fromUtf8("Русский")},
        {"th", QString::fromUtf8("ไทย")},
        {"ar", QString::fromUtf8("العربية")},
        {"hi", QString::fromUtf8("हिन्दी")},
        {"id", "Bahasa Indonesia"},
        {"vi", QString::fromUtf8("Tiếng Việt")},
        {"ms", "Bahasa Melayu"},
        {"fil", "Filipino"},
    };
    return list;
}

QString setting() { return QSettings().value("ui/language", "system").toString(); }
void setSetting(const QString& value) { QSettings().setValue("ui/language", value); }

QString systemLanguage() {
    // uiLanguages() is the user's display-language list, most preferred first, as BCP 47
    // tags ("zh-Hant-TW", "pt-BR", "fil-PH"). The first one Coreplay has wins.
    for (QString tag : QLocale::system().uiLanguages()) {
        tag = tag.toLower();
        const QString base = tag.section('-', 0, 0);
        if (base == "zh") {
            const bool traditional = tag.contains("hant") || tag.contains("-tw") || tag.contains("-hk") || tag.contains("-mo");
            return traditional ? "zh_TW" : "zh_CN";
        }
        if (base == "tl") return "fil";
        for (const Language& l : languages())
            if (l.code == base) return base;
    }
    return "en";
}

QString resolve(const QString& value) {
    if (value == "system" || value.isEmpty()) return systemLanguage();
    for (const Language& l : languages())
        if (l.code == value) return value;
    return "en";
}

QString current() { return g_current; }

void install(QApplication& app, bool testRun) {
    // Test runs check English text: they run in English, unless COREPLAY_TEST_LANG names a
    // language (for screenshots of a translation).
    const QString testLang = qEnvironmentVariable("COREPLAY_TEST_LANG");
    g_current = testRun ? (testLang.isEmpty() ? QString("en") : resolve(testLang)) : resolve(setting());
    if (g_current == "en") return;
    auto* tr = new QTranslator(&app);
    // Only the text is translated: number formats stay as they are (mpv and the time
    // fields read "0.5", never "0,5").
    if (tr->load(":/i18n/coreplay_" + g_current + ".qm")) {
        QApplication::installTranslator(tr);
    } else {
        delete tr;
        g_current = "en";
        return;
    }
    if (g_current == "ar") QApplication::setLayoutDirection(Qt::RightToLeft);
}

}  // namespace I18n
