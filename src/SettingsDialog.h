#pragma once
// Settings: General (new window per file, automatic update check) and Hotkeys (every
// command with its keys; change, add, clear, reset). Hotkeys are saved as overrides in
// hotkeys.json, the same file a user can edit by hand.
#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <vector>

#include "Hotkeys.h"

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    void showPage(int page);                    // 0 General, 1 Hotkeys

    // The same steps as the controls, for the tests too.
    void selectCommand(const QString& id);
    QString assignKey(const QKeySequence& key, bool add);  // returns the message shown
    void clearKeys();
    void resetCommand();
    void resetAll();
    void setNewKey(const QKeySequence& ks);   // what the New key field holds (a key or a mouse key)
    bool apply();                               // writes the settings and hotkeys.json
    QString keysText(const QString& id) const;
    QString message() const;
    int visibleRows() const;
    void setFilter(const QString& text);
    void setNewWindow(bool on);
    void setLanguage(const QString& code);  // "system" or a language code
    bool languageNoteVisible() const;

signals:
    void applied();                             // the window reloads its keys

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void fill();
    void showSelected();
    int indexOf(const QString& id) const;

    std::vector<HotkeyDef> keys_;               // being edited
    QString selected_;
    QTabWidget* tabs_;
    QCheckBox* newWindow_;
    QCheckBox* autoUpdate_;
    QCheckBox* software_;
    QComboBox* language_;
    QPushButton* restartBtn_;
    QLabel* langNote_;
    QLineEdit* filter_;
    QTableWidget* table_;
    QLabel* current_;
    QKeySequenceEdit* edit_;
    QPushButton *setBtn_, *addBtn_, *clearBtn_, *resetBtn_;
    QLabel* message_;
};
