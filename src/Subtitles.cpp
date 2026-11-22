#include "Subtitles.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// mpv colour as "r/g/b/a" floats: no ambiguity about the alpha position.
QString mpvColor(const QColor& c, double alpha) {
    return QString("%1/%2/%3/%4").arg(c.redF(), 0, 'f', 4).arg(c.greenF(), 0, 'f', 4).arg(c.blueF(), 0, 'f', 4)
        .arg(alpha, 0, 'f', 4);
}
}  // namespace

SubStyle SubStyle::load() {
    QSettings st;
    st.beginGroup("subtitles");
    SubStyle s;
    s.font = st.value("font", s.font).toString();
    s.size = st.value("size", s.size).toInt();
    s.bold = st.value("bold", s.bold).toBool();
    s.italic = st.value("italic", s.italic).toBool();
    s.text = QColor(st.value("text", s.text.name(QColor::HexArgb)).toString());
    s.outline = st.value("outline", s.outline).toInt();
    s.outlineColor = QColor(st.value("outlineColor", s.outlineColor.name(QColor::HexArgb)).toString());
    s.shadow = st.value("shadow", s.shadow).toInt();
    s.shadowColor = QColor(st.value("shadowColor", s.shadowColor.name(QColor::HexArgb)).toString());
    s.box = st.value("box", s.box).toBool();
    s.boxColor = QColor(st.value("boxColor", s.boxColor.name(QColor::HexArgb)).toString());
    s.boxOpacity = st.value("boxOpacity", s.boxOpacity).toInt();
    s.position = st.value("position", s.position).toInt();
    s.margin = st.value("margin", s.margin).toInt();
    s.overrideAss = st.value("overrideAss", s.overrideAss).toBool();
    return s;
}

void SubStyle::save() const {
    QSettings st;
    st.beginGroup("subtitles");
    st.setValue("font", font);
    st.setValue("size", size);
    st.setValue("bold", bold);
    st.setValue("italic", italic);
    st.setValue("text", text.name(QColor::HexArgb));
    st.setValue("outline", outline);
    st.setValue("outlineColor", outlineColor.name(QColor::HexArgb));
    st.setValue("shadow", shadow);
    st.setValue("shadowColor", shadowColor.name(QColor::HexArgb));
    st.setValue("box", box);
    st.setValue("boxColor", boxColor.name(QColor::HexArgb));
    st.setValue("boxOpacity", boxOpacity);
    st.setValue("position", position);
    st.setValue("margin", margin);
    st.setValue("overrideAss", overrideAss);
}

QList<QPair<QByteArray, QString>> SubStyle::mpvOptions() const {
    QList<QPair<QByteArray, QString>> o;
    o << qMakePair(QByteArray("sub-font"), font);
    o << qMakePair(QByteArray("sub-font-size"), QString::number(size));
    o << qMakePair(QByteArray("sub-bold"), QString(bold ? "yes" : "no"));
    o << qMakePair(QByteArray("sub-italic"), QString(italic ? "yes" : "no"));
    o << qMakePair(QByteArray("sub-color"), mpvColor(text, text.alphaF()));
    o << qMakePair(QByteArray("sub-outline-color"), mpvColor(outlineColor, outlineColor.alphaF()));
    if (box) {
        // libass draws one box behind the whole line in back-color; the outline size
        // is its padding.
        o << qMakePair(QByteArray("sub-border-style"), QString("background-box"));
        o << qMakePair(QByteArray("sub-back-color"), mpvColor(boxColor, boxOpacity / 100.0));
        o << qMakePair(QByteArray("sub-outline-size"), QString::number(std::max(outline, 4)));
        o << qMakePair(QByteArray("sub-shadow-offset"), QString("0"));
    } else {
        // Outline and drop shadow; mpv draws the shadow in back-color.
        o << qMakePair(QByteArray("sub-border-style"), QString("outline-and-shadow"));
        o << qMakePair(QByteArray("sub-back-color"), mpvColor(shadowColor, shadowColor.alphaF()));
        o << qMakePair(QByteArray("sub-outline-size"), QString::number(outline));
        o << qMakePair(QByteArray("sub-shadow-offset"), QString::number(shadow));
    }
    o << qMakePair(QByteArray("sub-pos"), QString::number(position));
    o << qMakePair(QByteArray("sub-margin-y"), QString::number(margin));
    o << qMakePair(QByteArray("sub-ass-override"), QString(overrideAss ? "force" : "scale"));
    return o;
}

namespace Subtitles {
QStringList extensions() { return {"srt", "ass", "ssa", "vtt", "sub", "smi", "sami", "lrc", "mks", "sup", "idx"}; }
bool isSubtitleFile(const QString& path) { return extensions().contains(QFileInfo(path).suffix().toLower()); }
}  // namespace Subtitles

SubtitleDialog::SubtitleDialog(const SubStyle& start, QWidget* parent) : QDialog(parent), s_(start) {
    setWindowTitle("Subtitle Style");
    auto* form = new QFormLayout;
    auto emitChange = [this] {
        s_.save();
        emit changed(s_);
    };

    // A colour button shows its colour and opens a picker (with alpha where asked).
    auto colourButton = [this, emitChange](QColor* target, bool alpha) {
        auto* b = new QPushButton(this);
        auto paint = [b, target] {
            QPixmap px(36, 16);
            px.fill(*target);
            b->setIcon(QIcon(px));
            b->setText(target->name(QColor::HexRgb).toUpper());
        };
        paint();
        connect(b, &QPushButton::clicked, this, [this, b, target, alpha, paint, emitChange] {
            const QColor c = QColorDialog::getColor(*target, this, "Colour",
                                                    alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
            if (!c.isValid()) return;
            *target = c;
            paint();
            emitChange();
        });
        return b;
    };
    auto spin = [this, emitChange](int* target, int lo, int hi, const QString& suffix = {}) {
        auto* s = new QSpinBox(this);
        s->setRange(lo, hi);
        s->setValue(*target);
        s->setSuffix(suffix);
        connect(s, &QSpinBox::valueChanged, this, [target, emitChange](int v) { *target = v; emitChange(); });
        return s;
    };
    auto check = [this, emitChange](bool* target, const QString& text) {
        auto* c = new QCheckBox(text, this);
        c->setChecked(*target);
        connect(c, &QCheckBox::toggled, this, [target, emitChange](bool v) { *target = v; emitChange(); });
        return c;
    };

    auto* font = new QFontComboBox(this);
    font->setCurrentFont(QFont(s_.font));
    connect(font, &QFontComboBox::currentFontChanged, this, [this, emitChange](const QFont& f) {
        s_.font = f.family();
        emitChange();
    });
    form->addRow("Font", font);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(spin(&s_.size, 8, 200));
    sizeRow->addWidget(check(&s_.bold, "Bold"));
    sizeRow->addWidget(check(&s_.italic, "Italic"));
    sizeRow->addStretch(1);
    form->addRow("Size", sizeRow);
    form->addRow("Text colour", colourButton(&s_.text, true));
    auto* outlineRow = new QHBoxLayout;
    outlineRow->addWidget(spin(&s_.outline, 0, 20));
    outlineRow->addWidget(colourButton(&s_.outlineColor, false));
    outlineRow->addStretch(1);
    form->addRow("Outline", outlineRow);
    auto* shadowRow = new QHBoxLayout;
    shadowRow->addWidget(spin(&s_.shadow, 0, 20));
    shadowRow->addWidget(colourButton(&s_.shadowColor, true));
    shadowRow->addStretch(1);
    form->addRow("Shadow", shadowRow);

    form->addRow("Background", check(&s_.box, "Draw a box behind the text"));
    form->addRow("Background colour", colourButton(&s_.boxColor, false));
    auto* opRow = new QHBoxLayout;
    auto* op = new QSlider(Qt::Horizontal, this);
    op->setRange(0, 100);
    op->setValue(s_.boxOpacity);
    auto* opNum = spin(&s_.boxOpacity, 0, 100, " %");
    connect(op, &QSlider::valueChanged, opNum, &QSpinBox::setValue);
    connect(opNum, &QSpinBox::valueChanged, op, &QSlider::setValue);
    opRow->addWidget(op, 1);
    opRow->addWidget(opNum);
    form->addRow("Background opacity", opRow);

    form->addRow("Vertical position", spin(&s_.position, 0, 150, "  (100 = bottom)"));
    form->addRow("Margin", spin(&s_.margin, 0, 300));
    form->addRow("", check(&s_.overrideAss, "Also restyle .ass / .ssa subtitles"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    auto* reset = buttons->addButton("Defaults", QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, this, [this] {
        s_ = SubStyle();
        s_.save();
        emit changed(s_);
        accept();  // reopen to see the defaults in the controls
    });
    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(buttons);
}
