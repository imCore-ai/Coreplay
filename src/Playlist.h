#pragma once
#include <QListWidget>
#include <QStringList>

// Playlist panel. Items are movie files or image sequences (stored as the path of
// the frame that was opened). Saves and loads .m3u8.
class Playlist : public QListWidget {
    Q_OBJECT
public:
    explicit Playlist(QWidget* parent = nullptr);

    static bool isMediaFile(const QString& path);
    // Expands folders into their media files and sequences (one entry per sequence).
    static QStringList expand(const QStringList& paths);

    void addPaths(const QStringList& paths);     // appends; folders are expanded
    QString pathAt(int row) const;
    int currentRow() const { return current_; }
    void setCurrent(int row);
    int count() const { return QListWidget::count(); }

    bool save(const QString& file) const;
    bool load(const QString& file);

    void sortByName();
    void shuffle();

signals:
    void play(int row);
    void compare(int row);   // A/B compare this item (B) with the playing one (A)
    void dropped(const QStringList& paths, bool append);

protected:
    bool event(QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;

private:
    void restyle();
    int current_ = -1;
};
