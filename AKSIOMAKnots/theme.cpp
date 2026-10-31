#include "theme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QRegularExpression>

#include <algorithm>

namespace
{

bool currentDark = true;

int currentScale = 100;

const Theme::Colors DarkColors =
{
    QColor(QStringLiteral("#060606")),
    QColor(QStringLiteral("#0D0D0D")),
    QColor(QStringLiteral("#BAB9B4")),
    QColor(QStringLiteral("#53514C")),
    QColor(QStringLiteral("#8A8780")),
    QColor(QStringLiteral("#D99B42"))
};

// The same roles on warm paper: dark text, recessed fields, a deeper amber
// that stays readable on the light panels.
const Theme::Colors LightColors =
{
    QColor(QStringLiteral("#E4E2DC")),
    QColor(QStringLiteral("#F3F2EE")),
    QColor(QStringLiteral("#1F1E1B")),
    QColor(QStringLiteral("#8C8981")),
    QColor(QStringLiteral("#5F5C56")),
    QColor(QStringLiteral("#A5660F"))
};

// dark reference colour -> light theme colour
const QHash<QString, QString> &lightMapping()
{
    static const QHash<QString, QString> mapping =
    {
        {QStringLiteral("#060606"), QStringLiteral("#E4E2DC")},
        {QStringLiteral("#0D0D0D"), QStringLiteral("#F3F2EE")},
        {QStringLiteral("#BAB9B4"), QStringLiteral("#1F1E1B")},
        {QStringLiteral("#53514C"), QStringLiteral("#8C8981")},
        {QStringLiteral("#8A8780"), QStringLiteral("#5F5C56")},
        {QStringLiteral("#D99B42"), QStringLiteral("#A5660F")},
        {QStringLiteral("#1A1A1A"), QStringLiteral("#D6D4CE")},
        // blue strand in the unknotting texts
        {QStringLiteral("#5B9BFF"), QStringLiteral("#2C6BD1")},
        // the two loops of a Seifert matrix entry
        {QStringLiteral("#46CD78"), QStringLiteral("#1C8A47")},
        {QStringLiteral("#E150C8"), QStringLiteral("#B02A9A")}
    };

    return mapping;
}

// In one pass, so that a mapped colour is never mapped again.
QString mapColors(
    const QString &source)
{
    if (currentDark)
        return source;

    static const QRegularExpression hex(
        QStringLiteral("#[0-9A-Fa-f]{6}\\b"));

    QString result;
    qsizetype last = 0;

    QRegularExpressionMatchIterator it = hex.globalMatch(source);

    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();

        result += QStringView(source).mid(last, match.capturedStart() - last);
        result += lightMapping().value(
            match.captured().toUpper(),
            match.captured());

        last = match.capturedEnd();
    }

    result += QStringView(source).mid(last);

    return result;
}

QString scalePointSizes(
    const QString &source)
{
    if (currentScale == 100)
        return source;

    static const QRegularExpression size(
        QStringLiteral("(\\d+(?:\\.\\d+)?)pt\\b"));

    QString result;
    qsizetype last = 0;

    QRegularExpressionMatchIterator it = size.globalMatch(source);

    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();

        const double points =
            match.captured(1).toDouble() * currentScale / 100.0;

        result += QStringView(source).mid(last, match.capturedStart() - last);
        result += QString::number(points, 'f', 1) + QStringLiteral("pt");

        last = match.capturedEnd();
    }

    result += QStringView(source).mid(last);

    return result;
}

// Flat, high-contrast, no gradients, no glow (dark reference colours).
const char *const StyleSheet = R"(
    * {
        font-family: "Segoe UI", "Inter", "Arial";
        font-size: 10pt;
    }

    QMainWindow {
        background: #060606;
    }

    QWidget {
        color: #BAB9B4;
    }

    QDialog {
        background: #0D0D0D;
    }

    /* ---------- layout surfaces ---------- */

    QWidget#TopBar {
        background: #0D0D0D;
        border-bottom: 1px solid #1A1A1A;
    }

    QWidget#ViewBar {
        background: #0D0D0D;
        border-top: 1px solid #1A1A1A;
    }

    /* the tools down the left edge; the chosen one runs into its panel */

    QWidget#Rail {
        background: #060606;
        border-right: 1px solid #1A1A1A;
    }

    QToolButton#RailButton {
        background: transparent;
        color: #8A8780;
        border: none;
        border-left: 2px solid transparent;
        padding: 9px 0px 8px 0px;
        font-size: 8pt;
        font-weight: 600;
    }

    QToolButton#RailButton:hover {
        color: #BAB9B4;
    }

    QToolButton#RailButton:checked {
        background: #0D0D0D;
        color: #BAB9B4;
        border-left: 2px solid #BAB9B4;
    }

    QToolButton#RailButton[running="true"] {
        color: #D99B42;
    }

    QWidget#Panel,
    QScrollArea#PanelScroll,
    QScrollArea#PanelScroll > QWidget > QWidget {
        background: #0D0D0D;
    }

    QScrollArea#PanelScroll {
        border: none;
    }

    QSplitter::handle {
        background: #1A1A1A;
    }

    QFrame#Rule {
        background: #53514C;
        border: none;
        max-height: 1px;
        min-height: 1px;
    }

    QFrame#VRule {
        background: #53514C;
        border: none;
    }

    QFrame#Tile {
        background: #060606;
        border: 1px solid #53514C;
        border-radius: 2px;
    }

    /* ---------- text roles ---------- */

    QLabel {
        background: transparent;
        color: #BAB9B4;
    }

    QLabel#Brand {
        color: #D99B42;
        font-size: 15pt;
        font-weight: 800;
        letter-spacing: 3px;
    }

    QLabel#BrandSub {
        color: #BAB9B4;
        font-size: 8pt;
        font-weight: 600;
        letter-spacing: 2.5px;
    }

    QLabel#Caption {
        color: #8A8780;
        font-size: 8pt;
        font-weight: 700;
        letter-spacing: 1.4px;
    }

    QLabel#SectionIndex {
        color: #D99B42;
        font-size: 8pt;
        font-weight: 700;
        letter-spacing: 1px;
    }

    QLabel#Section {
        color: #BAB9B4;
        font-size: 8pt;
        font-weight: 700;
        letter-spacing: 2.2px;
    }

    QLabel#Field {
        color: #BAB9B4;
        font-size: 9pt;
    }

    QLabel#Value {
        color: #BAB9B4;
        font-family: "Consolas", "Cascadia Mono", monospace;
        font-size: 9pt;
    }

    QLabel#Subtitle {
        color: #8A8780;
        font-size: 9pt;
    }

    QLabel#Status {
        color: #BAB9B4;
        font-size: 9pt;
    }

    QLabel#Headline {
        color: #BAB9B4;
        font-size: 17pt;
        font-weight: 700;
    }

    QLabel#Stat {
        color: #BAB9B4;
        font-size: 16pt;
        font-weight: 700;
    }

    QLabel#Polynomial {
        color: #D99B42;
        font-family: "Consolas", "Cascadia Mono", monospace;
        font-size: 12pt;
        font-weight: 700;
    }

    /* ---------- inputs ---------- */

    QComboBox,
    QSpinBox,
    QDoubleSpinBox,
    QLineEdit,
    QTextEdit,
    QPlainTextEdit {
        background: #060606;
        color: #BAB9B4;
        border: 1px solid #53514C;
        border-radius: 2px;
        padding: 4px 10px;
        selection-background-color: #BAB9B4;
        selection-color: #060606;
    }

    QTextEdit,
    QPlainTextEdit {
        padding: 8px 10px;
        font-family: "Consolas", "Cascadia Mono", monospace;
        font-size: 9pt;
    }

    QComboBox:hover,
    QSpinBox:hover,
    QDoubleSpinBox:hover,
    QLineEdit:hover,
    QTextEdit:hover,
    QPlainTextEdit:hover,
    QComboBox:focus,
    QSpinBox:focus,
    QLineEdit:focus,
    QTextEdit:focus,
    QPlainTextEdit:focus {
        border: 1px solid #BAB9B4;
    }

    QComboBox:disabled,
    QSpinBox:disabled,
    QTextEdit:disabled {
        color: #53514C;
    }

    QComboBox::drop-down {
        subcontrol-origin: padding;
        subcontrol-position: center right;
        width: 28px;
        border: none;
    }

    QComboBox::down-arrow {
        image: url(:/ui/icons/arrow_down.png);
        width: 12px;
        height: 12px;
    }

    QComboBox QAbstractItemView {
        background: #0D0D0D;
        color: #BAB9B4;
        border: 1px solid #53514C;
        padding: 4px;
        selection-background-color: #BAB9B4;
        selection-color: #060606;
        outline: none;
    }

    QSpinBox::up-button,
    QSpinBox::down-button {
        width: 22px;
        border: none;
        background: transparent;
    }

    QSpinBox::up-arrow {
        image: url(:/ui/icons/arrow_up.png);
        width: 10px;
        height: 10px;
    }

    QSpinBox::down-arrow {
        image: url(:/ui/icons/arrow_down.png);
        width: 10px;
        height: 10px;
    }

    /* ---------- buttons ---------- */

    QPushButton {
        background: transparent;
        color: #BAB9B4;
        border: 1px solid #53514C;
        border-radius: 2px;
        padding: 6px 14px;
        font-weight: 600;
    }

    QPushButton:hover {
        border-color: #BAB9B4;
    }

    QPushButton:pressed {
        background: #BAB9B4;
        color: #060606;
    }

    QPushButton:disabled {
        color: #53514C;
        border-color: #1a1a1a;
    }

    QPushButton#GenerateButton {
        background: #BAB9B4;
        color: #060606;
        border: 1px solid #BAB9B4;
        font-weight: 800;
        font-size: 9pt;
        letter-spacing: 1.6px;
    }

    QPushButton#GenerateButton:hover {
        background: #060606;
        color: #BAB9B4;
    }

    QPushButton#GenerateButton:pressed {
        background: #53514C;
        color: #BAB9B4;
    }

    QPushButton#GenerateButton:disabled {
        background: #060606;
        color: #53514C;
        border-color: #53514C;
    }

    /* text-only buttons: About in the top bar, "More options" */

    QPushButton#TopBarButton,
    QPushButton#DisclosureButton {
        background: transparent;
        border: none;
        padding: 4px 0px;
        color: #8A8780;
    }

    QPushButton#TopBarButton {
        font-size: 9pt;
        letter-spacing: 1px;
    }

    QPushButton#DisclosureButton {
        font-size: 9pt;
        font-weight: 600;
    }

    QPushButton#TopBarButton:hover,
    QPushButton#DisclosureButton:hover,
    QPushButton#DisclosureButton:checked {
        color: #BAB9B4;
    }

    QPushButton#TopBarButton:pressed,
    QPushButton#DisclosureButton:pressed {
        background: transparent;
        color: #D99B42;
    }

    /* File menu in the top bar */

    QPushButton#TopBarButton::menu-indicator {
        image: none;
        width: 0px;
    }

    QMenu {
        background: #0D0D0D;
        color: #BAB9B4;
        border: 1px solid #53514C;
        padding: 4px;
    }

    QMenu::item {
        padding: 6px 28px 6px 14px;
    }

    QMenu::item:selected {
        background: #BAB9B4;
        color: #060606;
    }

    QMenu::item:disabled {
        color: #53514C;
    }

    QMenu::separator {
        height: 1px;
        background: #53514C;
        margin: 4px 8px;
    }

    /* colour choice in Practice */

    QPushButton#BrushButton:checked {
        border: 2px solid #D99B42;
        color: #BAB9B4;
    }

    /* ---------- slider ---------- */

    QSlider {
        min-height: 18px;
    }

    QSlider::groove:horizontal {
        height: 2px;
        background: #53514C;
    }

    QSlider::sub-page:horizontal {
        background: #BAB9B4;
    }

    QSlider::add-page:horizontal {
        background: #53514C;
    }

    QSlider::handle:horizontal {
        width: 12px;
        height: 12px;
        margin: -5px 0;
        border-radius: 6px;
        background: #BAB9B4;
        border: none;
    }

    QSlider::handle:horizontal:hover {
        background: #D99B42;
    }

    QSlider::sub-page:horizontal:disabled {
        background: #53514C;
    }

    QSlider::handle:horizontal:disabled {
        background: #53514C;
    }

    /* ---------- progress bar (video memory) ---------- */

    QProgressBar {
        border: none;
        border-radius: 3px;
        background: #1A1A1A;
    }

    QProgressBar::chunk {
        border-radius: 3px;
        background: #D99B42;
    }

    /* ---------- checkbox ---------- */

    QCheckBox {
        background: transparent;
        color: #BAB9B4;
        spacing: 10px;
        min-height: 24px;
    }

    QCheckBox::indicator {
        width: 14px;
        height: 14px;
        border: 1px solid #53514C;
        border-radius: 2px;
        background: #060606;
    }

    QCheckBox::indicator:hover {
        border-color: #BAB9B4;
    }

    QCheckBox::indicator:checked {
        background: #BAB9B4;
        border-color: #BAB9B4;
        image: url(:/ui/icons/check.png);
    }

    /* ---------- tabs ---------- */

    QTabWidget::pane {
        border: none;
        background: #0D0D0D;
    }

    QTabBar {
        background: #0D0D0D;
    }

    QTabBar::tab {
        background: transparent;
        color: #8A8780;
        padding: 14px 20px 12px 20px;
        margin: 0;
        border: none;
        border-bottom: 2px solid #060606;
        font-size: 8pt;
        font-weight: 700;
        letter-spacing: 2px;
    }

    QTabBar::tab:hover {
        color: #BAB9B4;
    }

    QTabBar::tab:selected {
        color: #BAB9B4;
        border-bottom: 2px solid #BAB9B4;
    }

    /* ---------- text browser, scrollbar, tooltip ---------- */

    QTextBrowser {
        background: #0D0D0D;
        color: #BAB9B4;
        border: none;
        padding: 4px 8px;
        font-family: "Segoe UI", "Inter", "Arial";
        font-size: 9pt;
    }

    QScrollBar:vertical {
        background: transparent;
        width: 8px;
        margin: 0;
    }

    QScrollBar::handle:vertical {
        background: #53514C;
        min-height: 30px;
        border-radius: 4px;
    }

    QScrollBar::add-line:vertical,
    QScrollBar::sub-line:vertical {
        height: 0;
    }

    QScrollBar::add-page:vertical,
    QScrollBar::sub-page:vertical {
        background: transparent;
    }

    QToolTip {
        background: #0D0D0D;
        color: #BAB9B4;
        border: 1px solid #53514C;
        padding: 4px 8px;
    }
)";

} // namespace

namespace Theme
{

bool isDark()
{
    return currentDark;
}

const Colors &colors()
{
    return currentDark ? DarkColors : LightColors;
}

int textScale()
{
    return currentScale;
}

void apply(
    bool dark,
    int textScale)
{
    currentDark = dark;
    currentScale = std::clamp(textScale, 70, 160);

    const Colors &c = colors();

    QPalette palette;

    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.panel);
    palette.setColor(QPalette::AlternateBase, c.window);
    palette.setColor(QPalette::ToolTipBase, c.panel);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::PlaceholderText, c.muted);
    palette.setColor(QPalette::Button, c.panel);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.text);
    palette.setColor(QPalette::Highlight, c.text);
    palette.setColor(QPalette::HighlightedText, c.window);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.muted);

    QApplication::setPalette(palette);

    QString styleSheet =
        scalePointSizes(
            mapColors(
                QString::fromUtf8(StyleSheet)));

    // the arrows and the tick are drawn in the text colour or on it
    if (!dark)
    {
        styleSheet.replace(
            QStringLiteral("url(:/ui/icons/"),
            QStringLiteral("url(:/ui/icons/light/"));
    }

    qApp->setStyleSheet(
        styleSheet);
}

QString html(
    const QString &source)
{
    return scalePointSizes(
        mapColors(
            source));
}

} // namespace Theme
