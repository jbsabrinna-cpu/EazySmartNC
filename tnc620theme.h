#pragma once
// ================================================================
//  tnc620theme.h  -  Theme TNC 620 / smarT.NC pour EazyNC
//  Palette extraite du RPM officiel heidenhain640_theme-1.0-517.1
//  Police : TNC640Global (extraite de heidenhain640_font RPM)
// ================================================================

#include <QString>
#include <QApplication>
#include <QFontDatabase>

// -- Couleurs officielles Heidenhain TNC 640/620 --
static constexpr char TNC_WHITE[]       = "#FFFFFF";
static constexpr char TNC_BG[]          = "#D6D6D6";
static constexpr char TNC_BG_LIGHT[]    = "#EBEBEB";
static constexpr char TNC_DISABLED[]    = "#A9A9A9";
static constexpr char TNC_SEL_BG[]      = "#DBDBDB";
static constexpr char TNC_SEL_LIST[]    = "#0000FF";
static constexpr char TNC_HEADER[]      = "#EAEAEA";
static constexpr char TNC_ROW_ODD[]     = "#F5F5F5";
static constexpr char TNC_TEXT[]        = "#000000";
static constexpr char TNC_TEXT_SEL[]    = "#FFFFFF";
static constexpr char TNC_TOOLTIP[]     = "#FFFFC0";
static constexpr char TNC_BORDER[]      = "#808080";
static constexpr char TNC_BORDER_D[]    = "#505050";
static constexpr char TNC_GREEN[]       = "#00AA00";
static constexpr char TNC_RED[]         = "#CC0000";
static constexpr char TNC_ORANGE[]      = "#CC6600";
static constexpr char TNC_BLUE[]        = "#0000FF";
static constexpr char TNC_SCROLL[]      = "#AEDFEC";
static constexpr char TNC_TITLE_BG[]    = "#404040";
static constexpr char TNC_TITLE_TXT[]   = "#FFFFFF";

// -- DXF viewer --
static constexpr char TNC_DXF_BG_W[]   = "#FFFFFF";
static constexpr char TNC_DXF_BG_B[]   = "#0A0A0A";
static constexpr char TNC_DXF_MAUVE[]  = "#9933CC";
static constexpr char TNC_DXF_GREEN[]  = "#00AA00";

inline QString tnc620StyleSheet()
{
    return QStringLiteral(
"QWidget {"
"  background-color:#D6D6D6; color:#000000;"
"  font-family:'TNC640Global','Sans Serif',Arial; font-size:11pt;"
"  border:none;"
"}"
"QMainWindow,QStackedWidget { background-color:#D6D6D6; }"

/* Tabs */
"QTabWidget::pane { background:#FFFFFF; border:1px solid #808080; border-top:none; }"
"QTabBar::tab { background:#EBEBEB; color:#000000; border:1px solid #808080;"
"  border-bottom:none; padding:5px 14px; margin-right:2px;"
"  font-size:10pt; font-weight:bold; min-width:80px; }"
"QTabBar::tab:selected { background:#FFFFFF; border-bottom:1px solid #FFFFFF; }"
"QTabBar::tab:hover:!selected { background:#DBDBDB; }"

/* Boutons */
"QPushButton { background:#EBEBEB; color:#000000; border:1px solid #808080;"
"  border-radius:2px; padding:4px 10px; font-size:10pt; font-weight:bold; min-height:22px; }"
"QPushButton:hover { background:#DBDBDB; border-color:#404040; }"
"QPushButton:pressed { background:#C0C0C0; border-color:#000000; }"
"QPushButton:disabled { background:#A9A9A9; color:#787878; }"
"QPushButton:checked { background:#0000FF; color:#FFFFFF; border-color:#000080; }"

/* Softkeys */
"QPushButton[softkey=\"true\"] { background:#D6D6D6; color:#000000;"
"  border:1px solid #808080; border-top:2px solid #909090;"
"  border-radius:0px; padding:6px 4px; font-size:9pt; font-weight:bold;"
"  min-height:40px; max-height:40px; }"
"QPushButton[softkey=\"true\"]:hover { background:#DBDBDB; }"
"QPushButton[softkey=\"true\"]:pressed { background:#AAAAAA; }"
"QPushButton[softkey=\"true\"]:checked { background:#0000FF; color:#FFFFFF; border-top:2px solid #0000CC; }"
"QPushButton[softkey=\"true\"]:disabled { color:#909090; }"

/* ToolButton */
"QToolButton { background:#EBEBEB; color:#000000; border:1px solid #808080;"
"  padding:4px 10px; font-size:10pt; font-weight:bold; }"
"QToolButton:hover { background:#DBDBDB; }"
"QToolButton:checked,QToolButton:pressed { background:#FFFFFF; border-bottom:none; }"

/* Labels */
"QLabel { background:transparent; color:#000000; }"
"QLabel[title=\"true\"] { background:#404040; color:#FFFFFF; font-weight:bold;"
"  font-size:11pt; padding:4px 8px; }"
"QLabel[value=\"true\"] { background:#FFFFFF; color:#000000; border:1px solid #808080;"
"  padding:2px 6px; font-family:'TNC640Global',monospace; font-size:12pt; }"

/* Saisie */
"QLineEdit { background:#FFFFFF; color:#000000; border:1px solid #808080;"
"  border-radius:0px; padding:3px 6px; font-size:11pt;"
"  selection-background-color:#0000FF; selection-color:#FFFFFF; }"
"QLineEdit:focus { border:2px solid #0000FF; }"
"QLineEdit:disabled { background:#EBEBEB; color:#787878; }"
"QLineEdit:read-only { background:#F0F0F0; }"

/* SpinBox */
"QDoubleSpinBox,QSpinBox { background:#FFFFFF; color:#000000; border:1px solid #808080;"
"  padding:3px 4px; font-size:11pt;"
"  selection-background-color:#0000FF; selection-color:#FFFFFF; }"
"QDoubleSpinBox:focus,QSpinBox:focus { border:2px solid #0000FF; }"
"QDoubleSpinBox::up-button,QSpinBox::up-button,"
"QDoubleSpinBox::down-button,QSpinBox::down-button {"
"  background:#EBEBEB; border:1px solid #808080; border-left:none; width:16px; }"
"QDoubleSpinBox::up-button:hover,QSpinBox::up-button:hover,"
"QDoubleSpinBox::down-button:hover,QSpinBox::down-button:hover { background:#DBDBDB; }"

/* ComboBox */
"QComboBox { background:#FFFFFF; color:#000000; border:1px solid #808080;"
"  border-radius:0px; padding:3px 6px; font-size:11pt; }"
"QComboBox:focus { border:2px solid #0000FF; }"
"QComboBox:hover { border-color:#404040; }"
"QComboBox::drop-down { background:#EBEBEB; border-left:1px solid #808080; width:20px; }"
"QComboBox QAbstractItemView { background:#FFFFFF; color:#000000; border:1px solid #808080;"
"  selection-background-color:#0000FF; selection-color:#FFFFFF;"
"  alternate-background-color:#F5F5F5; }"

/* Listes */
"QListView,QTreeView,QTableView { background:#FFFFFF; color:#000000;"
"  border:1px solid #808080; alternate-background-color:#F5F5F5;"
"  gridline-color:#D0D0D0; selection-background-color:#0000FF;"
"  selection-color:#FFFFFF; font-size:11pt; }"
"QListView::item,QTreeView::item,QTableView::item { padding:3px 4px; min-height:22px; }"
"QListView::item:selected,QTreeView::item:selected,QTableView::item:selected {"
"  background:#0000FF; color:#FFFFFF; }"
"QListView::item:hover,QTreeView::item:hover,QTableView::item:hover { background:#DBDBDB; }"
"QHeaderView::section { background:#EAEAEA; color:#000000; border:1px solid #808080;"
"  padding:4px 6px; font-weight:bold; font-size:10pt; }"
"QHeaderView::section:pressed { background:#C0C0C0; }"

/* Checkbox */
"QCheckBox { color:#000000; spacing:6px; font-size:11pt; }"
"QCheckBox::indicator { width:14px; height:14px; border:1px solid #808080; background:#FFFFFF; }"
"QCheckBox::indicator:checked { background:#0000FF; border-color:#000080; }"
"QCheckBox::indicator:hover { border-color:#000000; }"

/* RadioButton */
"QRadioButton { color:#000000; spacing:6px; font-size:11pt; }"
"QRadioButton::indicator { width:14px; height:14px; border:1px solid #808080;"
"  border-radius:7px; background:#FFFFFF; }"
"QRadioButton::indicator:checked { background:#0000FF; border-color:#000080; }"

/* Scrollbar */
"QScrollBar:vertical { background:#E5E5E5; width:15px; border:1px solid #808080; }"
"QScrollBar::handle:vertical { background:#AEDFEC; border:1px solid #808080; min-height:30px; }"
"QScrollBar::handle:vertical:hover { background:#0000FF; }"
"QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {"
"  background:#EBEBEB; border:1px solid #808080; height:15px; }"
"QScrollBar:horizontal { background:#E5E5E5; height:15px; border:1px solid #808080; }"
"QScrollBar::handle:horizontal { background:#AEDFEC; border:1px solid #808080; min-width:30px; }"
"QScrollBar::handle:horizontal:hover { background:#0000FF; }"
"QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal {"
"  background:#EBEBEB; border:1px solid #808080; width:15px; }"

/* Splitter */
"QSplitter::handle { background:#D6D6D6; border:1px solid #808080; }"
"QSplitter::handle:hover { background:#DBDBDB; }"

/* GroupBox */
"QGroupBox { background:#EBEBEB; border:1px solid #808080; border-radius:0px;"
"  margin-top:18px; padding:6px; font-weight:bold; font-size:10pt; }"
"QGroupBox::title { subcontrol-origin:margin; subcontrol-position:top left;"
"  background:#404040; color:#FFFFFF; padding:2px 8px; left:4px; }"

/* TextEdit */
"QTextEdit,QPlainTextEdit { background:#FFFFFF; color:#000000; border:1px solid #808080;"
"  font-size:11pt; selection-background-color:#0000FF; selection-color:#FFFFFF; }"

/* Menu */
"QMenuBar { background:#D6D6D6; color:#000000; border-bottom:1px solid #808080; }"
"QMenuBar::item:selected { background:#DBDBDB; }"
"QMenu { background:#FFFFFF; color:#000000; border:1px solid #808080; }"
"QMenu::item:selected { background:#0000FF; color:#FFFFFF; }"
"QMenu::separator { background:#808080; height:1px; }"

/* StatusBar */
"QStatusBar { background:#D6D6D6; color:#000000; border-top:1px solid #808080; font-size:10pt; }"

/* Tooltip */
"QToolTip { background:#FFFFC0; color:#000000; border:1px solid #808080; padding:4px; font-size:10pt; }"

/* ProgressBar */
"QProgressBar { background:#FFFFFF; border:1px solid #808080; text-align:center; color:#000000; }"
"QProgressBar::chunk { background:#0000FF; }"

/* Override inline styles */
"* { font-family:'TNC640Global','Sans Serif',Arial; }"
/* Slider */
"QSlider::groove:horizontal { height:6px; background:#E5E5E5; border:1px solid #808080; }"
"QSlider::handle:horizontal { background:#EBEBEB; border:1px solid #808080;"
"  width:15px; height:15px; margin:-5px 0; }"
"QSlider::handle:horizontal:hover { background:#DBDBDB; border-color:#000000; }"
"QSlider::sub-page:horizontal { background:#AEDFEC; border:1px solid #808080; }"

/* ToolBar */
"QToolBar { background:#D6D6D6; border-bottom:1px solid #808080; spacing:2px; padding:2px; }"

/* DXF list */
"QListWidget[dxfList=\"true\"] { background:#FFFFFF; color:#000000; border:1px solid #808080;"
"  alternate-background-color:#F5F5F5; font-size:10pt; }"
"QListWidget[dxfList=\"true\"]::item { padding:2px 4px; min-height:20px; }"
"QListWidget[dxfList=\"true\"]::item:selected { background:#0000FF; color:#FFFFFF; }"
    );
}

inline void loadTncFont(const QString& path = "TNC640Global.ttf")
{
    int id = QFontDatabase::addApplicationFont(path);
    if(id < 0) id = QFontDatabase::addApplicationFont(":/fonts/TNC640Global.ttf");
    if(id >= 0){
        const QStringList fam = QFontDatabase::applicationFontFamilies(id);
        if(!fam.isEmpty()) QApplication::setFont(QFont(fam.first(), 11));
    }
}

inline void applyTnc620Theme(QApplication& app)
{
    app.setStyle("Fusion");
    app.setStyleSheet(tnc620StyleSheet());
    loadTncFont();
}
