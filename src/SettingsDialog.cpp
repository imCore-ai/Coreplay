#include "SettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString keysToText(const QList<QKeySequence>& keys) {
    QStringList l;
    for (const auto& k : keys) l << k.toString(QKeySequence::NativeText);
    return l.join(",  ");
}
QString plainLabel(const char* label) { return QString(label).remove('&').replace("&&", "&"); }
}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), keys_(Hotkeys::load()) {
    setWindowTitle("Settings");
    resize(760, 620);
    tabs_ = new QTabWidget(this);

    // ---- General
    auto* general = new QWidget;
    auto* gl = new QVBoxLayout(general);
    newWindow_ = new QCheckBox("Open each file in a new window when a file is already playing");
    newWindow_->setToolTip("Double-clicking a second file opens a second Coreplay window instead of\n"
                           "replacing the file that plays. Files selected together open in one window.");
    newWindow_->setChecked(QSettings().value("player/newWindow", false).toBool());
    autoUpdate_ = new QCheckBox("Check for updates automatically (once a day)");
    autoUpdate_->setChecked(QSettings().value("updates/auto", true).toBool());
    software_ = new QCheckBox("Software rendering (no GPU; applies at the next start)");
    software_->setToolTip("Coreplay turns this on by itself when the GPU driver cannot draw its picture.\n"
                          "Turn it off after installing a GPU driver.");
    software_->setChecked(QSettings().value("gl/software", false).toBool());
    gl->addWidget(newWindow_);
    gl->addWidget(autoUpdate_);
#ifndef Q_OS_WIN
    autoUpdate_->hide();  // no update feed on Linux (new versions come as a package)
#endif
    gl->addWidget(software_);
    gl->addStretch();
    tabs_->addTab(general, "General");

    // ---- Hotkeys
    auto* hot = new QWidget;
    auto* hl = new QVBoxLayout(hot);
    filter_ = new QLineEdit;
    filter_->setPlaceholderText("Search commands or keys");
    filter_->setClearButtonEnabled(true);
    hl->addWidget(filter_);
    table_ = new QTableWidget(0, 2);
    table_->setHorizontalHeaderLabels({"Command", "Keys"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hl->addWidget(table_, 1);

    current_ = new QLabel("Select a command.");
    hl->addWidget(current_);
    auto* row = new QHBoxLayout;
    edit_ = new QKeySequenceEdit;
    edit_->setMaximumSequenceLength(1);
    edit_->setToolTip("Click here, then press the new key");
    setBtn_ = new QPushButton("Set");
    setBtn_->setToolTip("Replace this command's keys with the pressed key");
    addBtn_ = new QPushButton("Add");
    addBtn_->setToolTip("Keep the current keys and add the pressed key");
    clearBtn_ = new QPushButton("Clear");
    clearBtn_->setToolTip("No key for this command");
    resetBtn_ = new QPushButton("Default");
    resetBtn_->setToolTip("This command's default keys");
    row->addWidget(new QLabel("New key:"));
    row->addWidget(edit_, 1);
    for (auto* b : {setBtn_, addBtn_, clearBtn_, resetBtn_}) row->addWidget(b);
    hl->addLayout(row);
    message_ = new QLabel;
    message_->setWordWrap(true);
    hl->addWidget(message_);
    auto* resetAllBtn = new QPushButton("Reset All Hotkeys");
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(resetAllBtn);
    bottom->addStretch();
    hl->addLayout(bottom);
    tabs_->addTab(hot, "Hotkeys");

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
        if (!f.isEmpty() && !label.contains(f, Qt::CaseInsensitive) && !keys.contains(f, Qt::CaseInsensitive)) continue;
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
        current_->setText("Select a command, click in New key and press the key.");
        return;
    }
    const auto& defs = Hotkeys::defaults();
    const QString def = keysToText(defs[i].keys);
    current_->setText(QString("%1:  %2   (default: %3)")
                          .arg(plainLabel(keys_[i].label), keys_[i].keys.isEmpty() ? QString("no key") : keysToText(keys_[i].keys),
                               def.isEmpty() ? QString("no key") : def));
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
        message_->setText("Press a key in New key first.");
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
    const QString k = key.toString(QKeySequence::NativeText);
    message_->setText(movedFrom.isEmpty()
                          ? QString("%1 is now on %2.").arg(k, plainLabel(keys_[i].label))
                          : QString("%1 is now on %2. It was removed from: %3.").arg(k, plainLabel(keys_[i].label), movedFrom.join(", ")));
    edit_->clear();
    fill();
    showSelected();
    return message_->text();
}

void SettingsDialog::clearKeys() {
    const int i = indexOf(selected_);
    if (i < 0) return;
    keys_[i].keys.clear();
    message_->setText(plainLabel(keys_[i].label) + " has no key now.");
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
    message_->setText(plainLabel(keys_[i].label) + " has its default keys." +
                      (movedFrom.isEmpty() ? QString() : " Removed from: " + movedFrom.join(", ") + "."));
    fill();
    showSelected();
}

void SettingsDialog::resetAll() {
    keys_ = Hotkeys::defaults();
    message_->setText("All hotkeys are back to the defaults (saved when you press OK or Apply).");
    fill();
    showSelected();
}

bool SettingsDialog::apply() {
    QSettings st;
    st.setValue("player/newWindow", newWindow_->isChecked());
    st.setValue("updates/auto", autoUpdate_->isChecked());
    st.setValue("gl/software", software_->isChecked());
    if (!Hotkeys::saveOverrides(keys_)) {
        message_->setText("Cannot write " + Hotkeys::configPath());
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
