#pragma once
// GPU viewer for decoded frames. Colour goes through OpenColorIO on the GPU:
// file colour space -> display/view. Exposure and channel isolation come first.
#include <QOpenGLExtraFunctions>
#include <QOpenGLWidget>
#include <QStringList>
#include <memory>
#include <vector>

#include "Grade.h"
#include "Compare.h"
#include "Engine.h"
#include "Lut.h"
#include "seq/ImageDecoder.h"

class SeqView : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    explicit SeqView(QWidget* parent = nullptr);
    ~SeqView() override;

    void setFrame(FramePtr f);
    FramePtr frame() const { return frame_; }
    // A/B compare: B's frame (same frame index, plus the B offset) and how to show both.
    void setFrameB(FramePtr f);
    FramePtr frameB() const { return frameB_; }
    void setCompare(const CompareView& c);
    const CompareView& compare() const { return cmp_; }

    // Colour
    QStringList colorSpaces() const;
    QStringList displays() const;
    QStringList views(const QString& display) const;
    QString inputSpace() const { return input_; }
    QString display() const { return display_; }
    QString viewName() const { return view_; }
    void setInputSpace(const QString& cs);
    void setDisplayView(const QString& display, const QString& view);
    QString defaultInputFor(const Frame& f) const;   // role by bit depth and file tag
    QString configName() const { return configName_; }
    // A LUT replaces the input-space / display-view transform: file values -> LUT -> screen.
    void setLut(const LutChoice& lut);
    const LutChoice& lut() const { return lut_; }
    // A/B compare: B's own LUT (None = the same display / view as A without a LUT).
    void setLutB(const LutChoice& lut);
    const LutChoice& lutB() const { return lutB_; }

    void setGrade(const Grade& g) { grade_ = g; update(); }   // after the view / LUT, A only
    const Grade& grade() const { return grade_; }
    void setExposure(double stops) { exposure_ = stops; update(); }
    double exposure() const { return exposure_; }
    void setChannel(int c) { channel_ = c; update(); }   // 0 RGB, 1 R, 2 G, 3 B, 4 A
    int channel() const { return channel_; }
    void setTransform(const ViewTransform& t) { xf_ = t; update(); }
    void setOverlay(const QString& text) { overlay_ = text; updateLabel(); }
    // Speed probe: uploads (count, ns) and paints (count, ns).
    long long uploads = 0, uploadNs = 0, paints = 0, paintNs = 0;

signals:
    void doubleClicked();
    void clicked();
    void contextMenu(const QPoint& globalPos);
    void colourError(const QString& text);
    void wipeMoved(float wipe);              // the user dragged the wipe line

protected:
    void initializeGL() override;
    void paintGL() override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Lut { unsigned tex; unsigned target; QString sampler; };
    struct Uniform { QString name; int loc; };

    void buildProgram();
    void uploadFrame();
    void uploadTexture(const Frame& f, unsigned tex, int& w, int& h, unsigned& fmt);
    float wipeAt(QPointF pos) const;
    void releaseLuts();
    void updateLabel();

    FramePtr frame_;
    bool frameDirty_ = false;
    unsigned tex_ = 0, vao_ = 0, prog_ = 0;
    unsigned pbo_[2] = {0, 0};    // frame uploads go through these (DMA), in turn
    int pboNext_ = 0;
    int texW_ = 0, texH_ = 0;
    unsigned texFmt_ = 0;
    std::vector<Lut> luts_;
    bool programDirty_ = true;

    struct OcioState;
    std::unique_ptr<OcioState> ocio_;
    QString configName_;
    QString input_, display_, view_;
    LutChoice lut_, lutB_;
    Grade grade_;
    double exposure_ = 0.0;
    int channel_ = 0;
    ViewTransform xf_;
    QString overlay_;
    class QLabel* label_ = nullptr;
    bool afterDouble_ = false;
    FramePtr frameB_;
    bool frameBDirty_ = false;
    unsigned texB_ = 0;
    int texBW_ = 0, texBH_ = 0;
    unsigned texBFmt_ = 0;
    CompareView cmp_;
    bool wipeDrag_ = false;
    // The last drawn picture, for mapping the mouse to the wipe position.
    double lastHx_ = 0, lastHy_ = 0, lastOffX_ = 0, lastOffY_ = 0, lastAngle_ = 0, lastVw_ = 1, lastVh_ = 1;
};
