#pragma once
#pragma execution_character_set("utf-8")

#include <QWidget>
#include <QVector>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QSplitter>
#include <QPlainTextEdit>

#include "gcodeengine.h"
#include "programmepage.h"
#include "uniteditor.h"

class ExecutionPage : public QWidget
{
    Q_OBJECT
public:
    explicit ExecutionPage(QWidget *parent = nullptr);

    // ── Chargement ───────────────────────────────────────────
    // Depuis ProgrammePage (.enc) → vue blocs EazyNC
    void offerProgram(const QVector<BlockData> &blocks,
                      const QVector<GCodeLine> &gcode,
                      const QString            &name);

    // Charger un .nc/.cnc directement → vue programme brut
    void loadRawFile(const QString &path);

    // ── Mises à jour DRO ─────────────────────────────────────
    void updateDro(double wx, double wy, double wz,
                   double mx, double my, double mz,
                   double dtgx, double dtgy, double dtgz);
    void updateSpindle(int rpm, int spindleOvr);
    void updateFeed(double feed_mmmin, int feedOvr);
    void updateTool(int tNum, int hNum);
    void updateMessages(const QString &msg);

    // Surligner la ligne/bloc en cours d'exécution
    void highlightBlock(int blockId);
    void highlightLine(int lineNumber);

    QString currentNcPath() const { return m_ncPath; }

    // Mode courant
    enum ViewMode { ModeEnc, ModeRaw };
    ViewMode viewMode() const { return m_viewMode; }

signals:
    void requestOpenFile();   // demande ouverture gestionnaire fichiers
    void cycleStartRequested();
    void feedHoldRequested();
    void cycleStopRequested();
    void resetRequested();
    void toolTableRequested();

    void jogXPlus (bool pressed);
    void jogXMinus(bool pressed);
    void jogYPlus (bool pressed);
    void jogYMinus(bool pressed);
    void jogZPlus (bool pressed);
    void jogZMinus(bool pressed);
    void feedPlusPressed (bool pressed);
    void feedMinusPressed(bool pressed);
    void spindlePlusPressed (bool pressed);
    void spindleMinusPressed(bool pressed);
    void incContToggled();
    void rapidToggled();
    void setX1();
    void setX10();
    void setX100();
    void spindleCWRequested();
    void spindleCCWRequested();
    void spindleStopRequested();

private slots:
    void onBlockSelected(int row);
    void onChargerClicked();

private:
    void setupUi();
    void setupLeftPanel(QWidget *parent);
    void setupRightPanel(QWidget *parent);
    void switchToEncMode();
    void switchToRawMode();

    static QLabel*      makeDroLabel(const QString &init = "+0.0000");
    static QLabel*      makeSectionLabel(const QString &t);
    static QLabel*      makeAxisLabel(const QString &t);
    static QPushButton* makeJogBtn(const QString &label,
                                    const QString &color = "#2a2d36");
    static QPushButton* makeCycleBtn(const QString &label,
                                      const QString &bg,
                                      const QString &border,
                                      const QString &color);

    // ── DRO ──────────────────────────────────────────────────
    QLabel *m_droWX=nullptr, *m_droWY=nullptr, *m_droWZ=nullptr;
    QLabel *m_droMX=nullptr, *m_droMY=nullptr, *m_droMZ=nullptr;
    QLabel *m_droDX=nullptr, *m_droDY=nullptr, *m_droDZ=nullptr;
    QLabel *m_lblRpm=nullptr,     *m_lblFeed=nullptr;
    QLabel *m_lblFeedOvr=nullptr, *m_lblSpinOvr=nullptr;
    QLabel *m_lblTool=nullptr;
    QLabel *m_lblMessages=nullptr;

    // ── Barre du haut ────────────────────────────────────────
    QLabel      *m_lblProgName = nullptr;
    QLabel      *m_lblModeBadge = nullptr;   // "ENC" ou "NC"

    // ── Vue blocs EazyNC (mode .enc) ─────────────────────────
    QWidget     *m_encView    = nullptr;
    QListWidget *m_blockList  = nullptr;
    UnitEditor  *m_editor     = nullptr;

    // ── Vue programme brut (mode .nc/.cnc) ───────────────────
    QWidget       *m_rawView     = nullptr;
    QListWidget   *m_rawList     = nullptr;   // une ligne = un item
    QLabel        *m_rawHdr      = nullptr;

    // ── Stack droite ─────────────────────────────────────────
    QStackedWidget *m_viewStack  = nullptr;   // 0=enc  1=raw

    // ── Données ──────────────────────────────────────────────
    QVector<BlockData> m_blocks;
    QVector<GCodeLine> m_gcode;
    QStringList        m_rawLines;
    QString            m_ncPath;
    int                m_currentBlock = -1;
    ViewMode           m_viewMode     = ModeEnc;
};
