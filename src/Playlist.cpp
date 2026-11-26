#include "Link.h"
#include "Playlist.h"

#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QRandomGenerator>
#include <QTextStream>
#include <algorithm>

#include "seq/Sequence.h"

namespace {
const QStringList kMovieExt = {
    "mp4", "m4v", "mov", "qt", "mkv", "webm", "avi", "wmv", "asf", "flv", "mxf", "mts", "m2ts", "ts",
    "mpg", "mpeg", "m2v", "vob", "3gp", "ogv", "dv", "y4m", "r3d", "braw", "gif", "apng", "hevc",
    "h264", "h265", "264", "265", "ivf", "nut", "rm", "rmvb", "mp3", "wav", "flac", "aac", "m4a",
    "ogg", "opus", "wma", "aiff", "aif", "ac3", "eac3", "dts", "mka", "m3u8"};

QString labelFor(const QString& path) {
    if (SequenceScanner::isImageFile(path)) return SequenceScanner::scan(path).displayName();
    return QFileInfo(path).fileName();
}
}  // namespace

Playlist::Playlist(QWidget* parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setAcceptDrops(true);
    setAlternatingRowColors(true);
    connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { emit play(row(it)); });
    // Keep the current row right after the user drags items around.
    connect(model(), &QAbstractItemModel::rowsMoved, this, [this] {
        for (int i = 0; i < count(); ++i)
            if (item(i)->data(Qt::UserRole + 1).toBool()) current_ = i;
    });
}

bool Playlist::isMediaFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return kMovieExt.contains(ext) || SequenceScanner::isImageFile(path);
}

QStringList Playlist::expand(const QStringList& paths) {
    QStringList out;
    for (const QString& p : paths) {
        if (Link::isRemote(p)) {  // a web address (from a coreplay:// link): not a file path
            out << p;
            continue;
        }
        const QFileInfo fi(p);
        if (!fi.isDir()) {
            out << fi.absoluteFilePath();
            continue;
        }
        QStringList movies;
        for (const QString& n : QDir(p).entryList(QDir::Files, QDir::Name))
            if (kMovieExt.contains(QFileInfo(n).suffix().toLower())) movies << QDir(p).absoluteFilePath(n);
        out << movies << SequenceScanner::sequencesInFolder(p);
        // Sequences are often one folder per shot: look one level down too.
        for (const QString& sub : QDir(p).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            out << SequenceScanner::sequencesInFolder(QDir(p).absoluteFilePath(sub));
    }
    return out;
}

void Playlist::addPaths(const QStringList& paths) {
    for (const QString& p : expand(paths)) {
        auto* it = new QListWidgetItem(labelFor(p), this);
        it->setData(Qt::UserRole, p);
        it->setToolTip(QDir::toNativeSeparators(p));
    }
}

QString Playlist::pathAt(int r) const {
    if (r < 0 || r >= count()) return {};
    return item(r)->data(Qt::UserRole).toString();
}

void Playlist::setCurrent(int r) {
    current_ = r;
    restyle();
    if (r >= 0 && r < count()) scrollToItem(item(r));
}

void Playlist::restyle() {
    for (int i = 0; i < count(); ++i) {
        QFont f = item(i)->font();
        f.setBold(i == current_);
        item(i)->setFont(f);
        item(i)->setData(Qt::UserRole + 1, i == current_);
    }
}

bool Playlist::save(const QString& file) const {
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "#EXTM3U\n";
    for (int i = 0; i < count(); ++i) {
        ts << "#EXTINF:-1," << item(i)->text() << "\n";
        ts << QDir::toNativeSeparators(pathAt(i)) << "\n";
    }
    return true;
}

bool Playlist::load(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    const QDir base = QFileInfo(file).absoluteDir();
    QStringList paths;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        paths << (QFileInfo(line).isAbsolute() ? line : base.absoluteFilePath(line));
    }
    clear();
    current_ = -1;
    addPaths(paths);
    return true;
}

void Playlist::sortByName() {
    QString cur = pathAt(current_);
    sortItems();
    current_ = -1;
    for (int i = 0; i < count(); ++i)
        if (pathAt(i) == cur) current_ = i;
    restyle();
}

void Playlist::shuffle() {
    QList<QListWidgetItem*> items;
    while (count()) items << takeItem(0);
    std::shuffle(items.begin(), items.end(), *QRandomGenerator::global());
    for (auto* it : items) addItem(it);
    current_ = -1;
    for (int i = 0; i < count(); ++i)
        if (item(i)->data(Qt::UserRole + 1).toBool()) current_ = i;
}

void Playlist::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    else QListWidget::dragEnterEvent(e);
}

void Playlist::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    else QListWidget::dragMoveEvent(e);
}

// A drag from the playlist also carries the files (for the A / B compare slots).
QMimeData* Playlist::mimeData(const QList<QListWidgetItem*>& items) const {
    QMimeData* m = QListWidget::mimeData(items);
    QList<QUrl> urls;
    for (QListWidgetItem* it : items) urls << QUrl::fromLocalFile(pathAt(row(it)));
    if (m) m->setUrls(urls);
    return m;
}

void Playlist::dropEvent(QDropEvent* e) {
    if (e->source() == this || !e->mimeData()->hasUrls()) {  // a move inside the list
        QListWidget::dropEvent(e);
        return;
    }
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    e->acceptProposedAction();
    emit dropped(paths, true);  // a drop on the playlist always appends
}

// While the playlist has focus, its own keys (Delete, Enter, arrows, Home/End,
// Page Up/Down) act on the list, not on the player's hotkeys.
bool Playlist::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->modifiers() == Qt::NoModifier || k->modifiers() == Qt::ShiftModifier) {
            switch (k->key()) {
                case Qt::Key_Delete: case Qt::Key_Return: case Qt::Key_Enter:
                case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Home: case Qt::Key_End:
                case Qt::Key_PageUp: case Qt::Key_PageDown:
                    e->accept();
                    return true;
                default: break;
            }
        }
    }
    return QListWidget::event(e);
}

void Playlist::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete) {
        const auto sel = selectedItems();
        for (auto* it : sel) {
            const int r = row(it);
            if (r == current_) current_ = -1;
            else if (r < current_) --current_;
            delete it;
        }
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (currentItem()) emit play(row(currentItem()));
        return;
    }
    QListWidget::keyPressEvent(e);
}

void Playlist::contextMenuEvent(QContextMenuEvent* e) {
    QMenu m(this);
    QListWidgetItem* it = itemAt(e->pos());
    if (it) m.addAction(tr("&Play"), this, [this, it] { emit play(row(it)); });
    if (it && current_ >= 0 && row(it) != current_)
        m.addAction(tr("&Compare with Playing Clip (A/B)"), this, [this, it] { emit compare(row(it)); });
    m.addAction(tr("&Add Files..."), this, [this] {
        const QStringList f = QFileDialog::getOpenFileNames(this, tr("Add Files"));
        if (!f.isEmpty()) addPaths(f);
    });
    m.addAction(tr("Add &Folder..."), this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
        if (!d.isEmpty()) addPaths({d});
    });
    m.addSeparator();
    m.addAction(tr("&Remove"), this, [this] {
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        keyPressEvent(&del);
    })->setEnabled(!selectedItems().isEmpty());
    m.addAction(tr("&Clear"), this, [this] { clear(); current_ = -1; });
    m.addSeparator();
    m.addAction(tr("&Sort by Name"), this, &Playlist::sortByName);
    m.addAction(tr("Shu&ffle"), this, &Playlist::shuffle);
    m.addSeparator();
    m.addAction(tr("&Open Playlist..."), this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Open Playlist"), {}, tr("Playlists") + " (*.m3u8 *.m3u)");
        if (!f.isEmpty()) load(f);
    });
    m.addAction(tr("Sa&ve Playlist..."), this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Save Playlist"), "playlist.m3u8", tr("Playlist") + " (*.m3u8)");
        if (!f.isEmpty()) save(f);
    });
    m.exec(e->globalPos());
}
