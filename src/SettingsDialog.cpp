#include "SettingsDialog.h"

#include "I18n.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>

namespace {
QString keysToText(const QList<QKeySequence>& keys) {
    QStringList l;
    for (const auto& k : keys) l << Hotkeys::text(k, true);
    return l.join(",  ");
}
// "Pan&&Scan" -> "Pan&Scan"; a CJK/Thai/Arabic/Hindi menu key "(&P)" goes as a whole.
QString plainText(QString s) {
    s.remove(QRegularExpression(R"(\s*\(&[^&\s]\))"));
    return s.replace("&&", QChar(1)).remove('&').replace(QChar(1), '&');
}
// A command's label as shown: translated (context "Hotkeys"), then without its marks.
QString plainLabel(const char* label) { return plainText(QCoreApplication::translate("Hotkeys", label)); }
// The English label, so a search in English still finds the command.
QString englishLabel(const char* label) { return plainText(QString(label)); }
}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), keys_(Hotkeys::load()) {
    setWindowTitle(tr("Settings"));
    setObjectName("settingsDialog");
    resize(760, 620);
    tabs_ = new QTabWidget(this);

    // ---- General
    auto* general = new QWidget;
    auto* gl = new QVBoxLayout(general);
    newWindow_ = new QCheckBox(tr("Open each file in a new window when a file is already playing"));
    //: "Coreplay" is the product name; keep it in English.
    newWindow_->setToolTip(tr("Double-clicking a second file opens a second Coreplay window instead of\n"
                              "replacing the file that plays. Files selected together open in one window."));
    newWindow_->setChecked(QSettings().value("player/newWindow", false).toBool());
    autoUpdate_ = new QCheckBox(tr("Check for updates automatically (once a day)"));
    autoUpdate_->setChecked(QSettings().value("updates/auto", true).toBool());
    software_ = new QCheckBox(tr("Software rendering (no GPU; applies at the next start)"));
    //: "Coreplay" is the product name; keep it in English.
    software_->setToolTip(tr("Coreplay turns this on by itself when the GPU driver cannot draw its picture.\n"
                             "Turn it off after installing a GPU driver."));
    software_->setChecked(QSettings().value("gl/software", false).toBool());
    gl->addWidget(newWindow_);
    gl->addWidget(autoUpdate_);
#ifndef Q_OS_WIN
    autoUpdate_->hide();  // no update feed on Linux (new versions come as a package)
#endif
    gl->addWidget(software_);

    // Language: follow the system's display language, or pick one. Applies at the next start.
    gl->addSpacing(14);
    auto* langTitle = new QLabel(tr("Language"));
    QFont tf = langTitle->font();
    tf.setBold(true);
    langTitle->setFont(tf);
    gl->addWidget(langTitle);
    language_ = new QComboBox;
    language_->setObjectName("language");
    {
        QString sysName = "English";
        for (const auto& l : I18n::languages())
            if (l.code == I18n::systemLanguage()) sysName = l.nativeName;
        language_->addItem(tr("Follow system (%1)").arg(sysName), "system");
        language_->insertSeparator(1);
        for (const auto& l : I18n::languages()) language_->addItem(l.nativeName, l.code);
        const int at = language_->findData(I18n::setting());
        language_->setCurrentIndex(at >= 0 ? at : 0);
    }
    auto* langRow = new QHBoxLayout;
    langRow->addWidget(language_);
    restartBtn_ = new QPushButton(tr("Restart Now"));
    restartBtn_->setObjectName("restartNow");
    langRow->addWidget(restartBtn_);
    langRow->addStretch();
    gl->addLayout(langRow);
    langNote_ = new QLabel(tr("The new language applies when Coreplay starts again."));
    langNote_->setObjectName("languageNote");
    gl->addWidget(langNote_);
    auto updateLangNote = [this] {
        const bool changed = I18n::resolve(language_->currentData().toString()) != I18n::current();
        langNote_->setVisible(changed);
        restartBtn_->setVisible(changed);
    };
    updateLangNote();
    connect(language_, &QComboBox::currentIndexChanged, this, updateLangNote);
    connect(restartBtn_, &QPushButton::clicked, this, [this] {
        if (!apply()) return;
        // A new Coreplay (its own window, not handed to this one), then this one closes.
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--new"});
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    });
    gl->addStretch();
    tabs_->addTab(general, tr("General"));

    // ---- Hotkeys
    auto* hot = new QWidget;
    auto* hl = new QVBoxLayout(hot);
    filter_ = new QLineEdit;
    filter_->setPlaceholderText(tr("Search commands or keys"));
    filter_->setClearButtonEnabled(true);
    hl->addWidget(filter_);
    table_ = new QTableWidget(0, 2);
    table_->setHorizontalHeaderLabels({tr("Command"), tr("Keys")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hl->addWidget(table_, 1);

    current_ = new QLabel(tr("Select a command."));
    hl->addWidget(current_);
    auto* row = new QHBoxLayout;
    edit_ = new QKeySequenceEdit;
    edit_->setMaximumSequenceLength(1);
    edit_->setToolTip(tr("Click here, then press the new key, turn the mouse wheel, or click the middle or a side mouse button"));
    // The wheel and the extra mouse buttons are keys too (Settings > Hotkeys, hotkeys.json).
    edit_->installEventFilter(this);
    if (auto* le = edit_->findChild<QLineEdit*>()) le->installEventFilter(this);
    setBtn_ = new QPushButton(tr("Set"));
    setBtn_->setToolTip(tr("Replace this command's keys with the pressed key"));
    addBtn_ = new QPushButton(tr("Add"));
    addBtn_->setToolTip(tr("Keep the current keys and add the pressed key"));
    clearBtn_ = new QPushButton(tr("Clear"));
    clearBtn_->setToolTip(tr("No key for this command"));
    resetBtn_ = new QPushButton(tr("Default"));
    resetBtn_->setToolTip(tr("This command's default keys"));
    row->addWidget(new QLabel(tr("New key:")));
    row->addWidget(edit_, 1);
    for (auto* b : {setBtn_, addBtn_, clearBtn_, resetBtn_}) row->addWidget(b);
    hl->addLayout(row);
    message_ = new QLabel;
    message_->setWordWrap(true);
    hl->addWidget(message_);
    auto* resetAllBtn = new QPushButton(tr("Reset All Hotkeys"));
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(resetAllBtn);
    bottom->addStretch();
    hl->addLayout(bottom);
    tabs_->addTab(hot, tr("Hotkeys"));

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(tabs_);
    lay->addWidget(box);

    connect(filter_, &QLineEdit::textChanged, this, [this] { fill(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto rows = table_->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        const QString id = table_->item(rows.first().row(), 0)->data(Qt::UserRole).toString();
        if (id == selected_) return;  // the list was refilled: keep the message
        selected_ = id;
        message_->clear();
        showSelected();
    });
    connect(setBtn_, &QPushButton::clicked, this, [this] { assignKey(edit_->keySequence(), false); });
    connect(addBtn_, &QPushButton::clicked, this, [this] { assignKey(edit_->keySequence(), true); });
    connect(clearBtn_, &QPushButton::clicked, this, [this] { clearKeys(); });
    connect(resetBtn_, &QPushButton::clicked, this, [this] { resetCommand(); });
    connect(resetAllBtn, &QPushButton::clicked, this, [this] { resetAll(); });
    connect(box, &QDialogButtonBox::accepted, this, [this] { if (apply()) accept(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { apply(); });
    fill();
    showSelected();
}

void SettingsDialog::showPage(int page) { tabs_->setCurrentIndex(page); }

// New key: a wheel turn or a middle / side button click over the field is taken as that
// mouse key, with the modifiers held (Ctrl+Wheel Up, Middle Click, ...).
bool SettingsDialog::eventFilter(QObject* o, QEvent* e) {
    if (o == edit_ || o->parent() == edit_) {
        int key = 0;
        Qt::KeyboardModifiers mods;
        if (e->type() == QEvent::Wheel) {
            auto* w = static_cast<QWheelEvent*>(e);
            const int d = w->angleDelta().y() ? w->angleDelta().y() : w->angleDelta().x();
            if (d) key = d > 0 ? Hotkeys::WheelUp : Hotkeys::WheelDown;
            mods = w->modifiers();
        } else if (e->type() == QEvent::MouseButtonPress) {
            auto* m = static_cast<QMouseEvent*>(e);
            key = m->button() == Qt::MiddleButton    ? Hotkeys::MiddleClick
                  : m->button() == Qt::BackButton    ? Hotkeys::MouseBack
                  : m->button() == Qt::ForwardButton ? Hotkeys::MouseForward
                                                     : 0;
            mods = m->modifiers();
        }
        if (key) {
            setNewKey(Hotkeys::mouseKey(key, mods));
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

void SettingsDialog::setNewKey(const QKeySequence& ks) {
    edit_->setKeySequence(ks);
    if (auto* le = edit_->findChild<QLineEdit*>()) le->setText(Hotkeys::text(ks, true));
}

int SettingsDialog::indexOf(const QString& id) const {
    for (size_t i = 0; i < keys_.size(); ++i)
        if (id == keys_[i].id) return int(i);
    return -1;
}

void SettingsDialog::fill() {
    const QString f = filter_->text().trimmed();
    table_->setRowCount(0);
    for (const auto& d : keys_) {
        const QString label = plainLabel(d.label), keys = keysToText(d.keys);
        if (!f.isEmpty() && !label.contains(f, Qt::CaseInsensitive) &&
            !englishLabel(d.label).contains(f, Qt::CaseInsensitive) && !keys.contains(f, Qt::CaseInsensitive))
            continue;
        const int r = table_->rowCount();
        table_->insertRow(r);
        auto* name = new QTableWidgetItem(label);
        name->setData(Qt::UserRole, QString(d.id));
        table_->setItem(r, 0, name);
        table_->setItem(r, 1, new QTableWidgetItem(keys));
        if (selected_ == d.id) table_->selectRow(r);
    }
}

void SettingsDialog::showSelected() {
    const int i = indexOf(selected_);
    for (auto* w : std::initializer_list<QWidget*>{edit_, setBtn_, addBtn_, clearBtn_, resetBtn_}) w->setEnabled(i >= 0);
    if (i < 0) {
        current_->setText(tr("Select a command, click in New key and press the key."));
        return;
    }
    const auto& defs = Hotkeys::defaults();
    const QString def = keysToText(defs[i].keys);
    //: %1 is a command, %2 its keys, %3 its default keys.
    current_->setText(tr("%1:  %2   (default: %3)")
                          .arg(plainLabel(keys_[i].label), keys_[i].keys.isEmpty() ? tr("no key") : keysToText(keys_[i].keys),
                               def.isEmpty() ? tr("no key") : def));
}

void SettingsDialog::selectCommand(const QString& id) {
    selected_ = id;
    filter_->clear();
    fill();
    showSelected();
}

QString SettingsDialog::assignKey(const QKeySequence& key, bool add) {
    const int i = indexOf(selected_);
    if (i < 0 || key.isEmpty()) {
        message_->setText(tr("Press a key in New key first."));
        return message_->text();
    }
    // One key, one command: take it away from any other command that had it.
    QStringList movedFrom;
    for (auto& d : keys_) {
        if (selected_ == d.id) continue;
        if (d.keys.removeAll(key)) movedFrom << plainLabel(d.label);
    }
    auto& ks = keys_[i].keys;
    if (!add) ks.clear();
    if (!ks.contains(key)) ks << key;
    const QString k = Hotkeys::text(key, true);
    message_->setText(movedFrom.isEmpty()
                          //: %1 is a key, %2 a command.
                          ? tr("%1 is now on %2.").arg(k, plainLabel(keys_[i].label))
                          //: %1 is a key, %2 a command, %3 the commands that had the key.
                          : tr("%1 is now on %2. It was removed from: %3.").arg(k, plainLabel(keys_[i].label), movedFrom.join(", ")));
    edit_->clear();
    fill();
    showSelected();
    return message_->text();
}

void SettingsDialog::clearKeys() {
    const int i = indexOf(selected_);
    if (i < 0) return;
    keys_[i].keys.clear();
    message_->setText(tr("%1 has no key now.").arg(plainLabel(keys_[i].label)));
    fill();
    showSelected();
}

void SettingsDialog::resetCommand() {
    const int i = indexOf(selected_);
    if (i < 0) return;
    const auto& def = Hotkeys::defaults()[i].keys;
    QStringList movedFrom;
    for (auto& d : keys_) {
        if (selected_ == d.id) continue;
        for (const auto& k : def)
            if (d.keys.removeAll(k)) movedFrom << plainLabel(d.label);
    }
    keys_[i].keys = def;
    message_->setText(tr("%1 has its default keys.").arg(plainLabel(keys_[i].label)) +
                      (movedFrom.isEmpty() ? QString() : tr(" Removed from: %1.").arg(movedFrom.join(", "))));
    fill();
    showSelected();
}

void SettingsDialog::resetAll() {
    keys_ = Hotkeys::defaults();
    message_->setText(tr("All hotkeys are back to the defaults (saved when you press OK or Apply)."));
    fill();
    showSelected();
}

bool SettingsDialog::apply() {
    QSettings st;
    st.setValue("player/newWindow", newWindow_->isChecked());
    st.setValue("updates/auto", autoUpdate_->isChecked());
    st.setValue("gl/software", software_->isChecked());
    I18n::setSetting(language_->currentData().toString());
    if (!Hotkeys::saveOverrides(keys_)) {
        message_->setText(tr("Cannot write %1").arg(Hotkeys::configPath()));
        return false;
    }
    emit applied();
    return true;
}

QString SettingsDialog::keysText(const QString& id) const {
    const int i = indexOf(id);
    return i < 0 ? QString() : keys_[i].keys.isEmpty() ? QString("none") : keysToText(keys_[i].keys);
}
QString SettingsDialog::message() const { return message_->text(); }
int SettingsDialog::visibleRows() const { return table_->rowCount(); }
void SettingsDialog::setFilter(const QString& text) { filter_->setText(text); }
void SettingsDialog::setNewWindow(bool on) { newWindow_->setChecked(on); }
void SettingsDialog::setLanguage(const QString& code) {
    const int at = language_->findData(code);
    if (at >= 0) language_->setCurrentIndex(at);
}
bool SettingsDialog::languageNoteVisible() const { return !langNote_->isHidden(); }
