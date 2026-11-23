#pragma once
#include <QSet>
#include <QAction>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QPixmap>
#include <functional>
#include <QHash>
#include <QMainWindow>
#include <QTimer>
#include <QVariantMap>

#include "Grade.h"
#include "Compare.h"
#include "Engine.h"
#include "Lut.h"

class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QSlider;
class QStackedLayout;
class QTextEdit;
class QToolButton;
class MpvEngine;
class Osd;
class Playlist;
class SeekBar;
class SequenceEngine;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    // Opens files (replacing the playlist) or appends them.
    void openPaths(const QStringList& paths, bool append = false);
    bool hasMedia() const { return !currentPath_.isEmpty(); }
    static bool newWindowPerFile();          // View > Open Each File in a New Window
    void setInfoOverlay(bool on);
    void refreshInfoOverlay(bool clipChanged);

    // For the end-to-end test driver.
    QVariantMap testState() const;
    bool testGrab(const QString& path);
    QWidget* testView() const;            // the widget that shows the picture now
    QImage testGrabImage();               // the video area as the user sees it
    void testSetLut(const LutChoice& c) { setLut(c); }
    void testMpvSet(const QString& name, const QString& value);
    bool testExport(const QString& presetId, const QString& output, bool colour, bool subs, double start, double end,
                    bool audio = true);
    void testLoadSubtitle(const QString& path) { loadSubtitle(path); }
    void testSubStyle(const QString& key, const QString& value);
    void testUpdate(const QString& step);   // check / download (never installs)
    void testAction(const QString& id) { if (QAction* a = actions_.value(id)) a->trigger(); }
    QString testCompare(const QStringList& args);
    void testPerfReset();
    QString testGrade(const QStringList& args);
    QString testSettings(const QStringList& args);   // drives the Settings dialog

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    enum class Repeat { None, One, All };

    void buildUi();
    void buildActions();
    void buildMenus();
    void applyHotkeys();
    QAction* mouseCommand(int key, Qt::KeyboardModifiers mods) const;
    // The window keeps the picture's shape (no black bars at the sides or top and bottom).
    QSizeF shownSize() const;     // the picture as shown: rotation and side-by-side compare included
    void fitWindowToPicture();
    QTimer* refitTimer_ = nullptr; // after a resize ends (macOS, Linux; Windows fits while dragging)
    QAction* act(const char* id);
    void on(const char* id, std::function<void()> fn, bool checkable = false);

    MpvEngine* mpv();
    SequenceEngine* seq();
    void connectEngine(Engine* e);
    void playRow(int row);
    // A/B compare: A = the playing clip, B = a playlist item (same kind: movie or sequence).
    void startCompare(const QString& pathB);
    void stopCompare(bool closeBar = true);   // false: keep the bar and B (A is changing)
    void showCompareBar(bool on);
    void updateCompareBar();
    void setSlotA(const QString& path);       // a clip dropped on / chosen for slot A
    QString chooseClip(const QString& title);
    void fillLutMenu(QMenu* m, const LutChoice& current, std::function<void(const LutChoice&)> apply);
    void setLutB(const LutChoice& c);
    void applyMpvLut();
    void setCompareMode(CompareMode m);
    void setCompareOffset(int frames);
    void applyCompareView();
    void compareMessage();
    void beginRow(int row);           // the per-clip resets, without opening it
    void queueNext();                 // tell the engine the clip after this one (gapless)
    void onAdvanced(const QString& path);
    void markSwitch(const char* mode);
    void playNext(int dir, bool wrap);
    void nextFileInFolder(int dir);
    void onEnd();
    void closeMedia();

    void seekBy(double seconds);
    void setRate(double r);
    void setVolume(int v);
    void toggleFullscreen();
    void zoomWindow(double factor);
    void autoFit(bool largerOnly);
    void pan(double dx, double dy);
    void applyTransform();
    void zoomAt(QPointF posInVideo, double factor);   // the point under the cursor stays put
    void zoomFit();
    void zoom1to1();
    void layoutOverlays();
    void setFullscreenControls(bool overlay);
    void setLut(const LutChoice& c);
    void setChannel(int c, bool toggle);   // toggle: the same channel again = all
    void styleChannelButtons();
    void addLutActions(QMenu* m);
    void fillContextMenu(QMenu* m);
    void exportDialog();
    void updatesDialog();
    void showAbout();
    void autoCheckUpdates();
    class Updater* updater();
    bool startExport(const struct ExportJob& job, bool showProgress);
    void loadSubtitle(const QString& path);
    void subtitleStyleDialog();
    void setViewPreset(int preset);     // 0 minimal, 1 compact, 2 normal
    void setAb(int which);

    void openDialog();
    void gotoDialog();
    void seqFpsDialog();
    void cacheDialog();
    void saveImage(bool autoName);
    bool saveImageTo(const QString& path);   // the still as shown (grade baked in)
    void recycleCurrent();
    void showProperties();
    void openLocation();
    void addRecent(const QString& path);
    void rebuildRecent();
    void fillColourMenu(QMenu* m);

    void updateStatus();
    void updateTitle();
    QString timeText(double s) const;

    // UI
    QWidget* videoArea_ = nullptr;
    QStackedLayout* stack_ = nullptr;
    QLabel* placeholder_ = nullptr;
    Osd* osd_ = nullptr;
    Osd* stats_ = nullptr;
    Osd* infoOverlay_ = nullptr;   // Tab: clip info + metadata
    class HotkeyOverlay* hotkeyList_ = nullptr;  // F1: every hotkey
    // Colour grading (Alt+G): one grade per clip, kept while the app runs. A clip whose
    // grade is not written to a file yet is 'modified' (* in the title).
    class GradeWindow* gradeWin_ = nullptr;
    QHash<QString, Grade> grades_;
    QSet<QString> unsavedGrades_;
    Grade currentGrade() const { return grades_.value(currentPath_); }
    void toggleGradeWindow();
    void setCurrentGrade(const Grade& g);
    void applyGrade();                // the current clip's grade on the engine that shows it
    void forwardKey(QKeyEvent* e);    // a hotkey typed in the grading window
    void saveGraded(bool asNew);      // Save (in place) / Save As
    bool saveGradedTo(const QString& output, bool inPlace, bool showProgress);
    QString savePresetFor(const QString& ext) const;
    std::function<void(bool)> afterExport_;
    class QLabel* logo_ = nullptr; // the logo on the empty player
    QPixmap logoPm_;
    void updatePlaceholderLogo();
    void checkOpenGL();             // after the first paint: is OpenGL 4.1 there?
    QString glState_;
    QTimer cacheBandTimer_;
    class SettingsDialog* settings_ = nullptr;
    void openSettings(int page);
    QString infoStatic_;          // its part that changes only with the clip
    SeekBar* seekBar_ = nullptr;
    QWidget* controls_ = nullptr;
    QWidget* bottom_ = nullptr;
    class QVBoxLayout* centralLayout_ = nullptr;
    class QToolButton* fitBtn_ = nullptr;
    class QToolButton* oneBtn_ = nullptr;
    bool dragging_ = false;
    QPointF dragLast_;
    QPointF middleFrom_;                 // where the middle button went down (a click, or a pan)
    QHash<int, QString> mouseKeys_;      // mouse key (with modifiers) -> command id
    int wheelAcc_ = 0;                   // wheel delta toward the next notch (non-zoom commands)
    bool wheelZoom_ = false;             // zoom.in / zoom.out triggered by the wheel: at the mouse
    QPointF wheelAt_;
    double wheelSteps_ = 1.0;
    LutChoice lut_;
    QStringList subFiles_;              // subtitle files loaded for the current movie
    class Exporter* exporter_ = nullptr;
    class Updater* updater_ = nullptr;
    QAction* updateAction_ = nullptr;
    QString exportState_ = "idle";      // idle / running / done / failed (tests read it)
    QString exportMessage_;
    double exportProgress_ = 0;
    int channel_ = 0;
    class QToolButton* chanBtn_[3] = {};
    QToolButton* playBtn_ = nullptr;
    QToolButton* loopBtn_ = nullptr;     // loop on / off (Play > Repeat)
    QToolButton* muteBtn_ = nullptr;     // speaker, crossed out while muted
    Repeat lastLoop_ = Repeat::All;      // what the loop button turns on
    void updateLoopButton();
    void updateMuteButton();
    QSlider* volSlider_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* statusLeft_ = nullptr;
    Playlist* playlist_ = nullptr;
    QDockWidget* playlistDock_ = nullptr;
    QDockWidget* infoDock_ = nullptr;
    QTextEdit* infoText_ = nullptr;
    QMenu* recentMenu_ = nullptr;

    QHash<QString, QAction*> actions_;
    QFileSystemWatcher hotkeyWatcher_;
    QTimer statusTimer_;
    QTimer hideCursor_;

    // Engines (created on first use, so start-up stays fast)
    MpvEngine* mpv_ = nullptr;
    SequenceEngine* seq_ = nullptr;
    Engine* cur_ = nullptr;
    QString currentPath_;

    ViewTransform xf_;
    int volume_ = 100;
    bool muted_ = false;
    double rate_ = 1.0;
    double abA_ = -1, abB_ = -1;
    Repeat repeat_ = Repeat::None;
    bool comparing_ = false;
    CompareView cmpView_{CompareMode::Wipe, false, 0.5f};
    QString cmpPath_;              // slot B
    QString pendingB_;             // B waiting for a new A to load
    LutChoice cmpLutB_;            // slot B's LUT (slot A's is lut_)
    class CompareBar* compareBar_ = nullptr;
    int cmpOffset_ = 0;
    // Clip-to-clip timing, for the tests: the gap between the end of one clip and the
    // start of the next on the wall clock, minus what the media itself says.
    QElapsedTimer wall_;
    qint64 lastPosWall_ = -1;
    double lastPos_ = 0, lastDur_ = 0;
    qint64 switchOldEnd_ = -1;        // wall time the old clip's last frame ended
    QString switchMode_, lastSwitch_;
    bool wasMaximized_ = false;
    bool warmedUp_ = false;
};
