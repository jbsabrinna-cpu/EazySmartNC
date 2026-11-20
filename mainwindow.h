#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QVector>
#include <QHBoxLayout>
#include <QLabel>
#include <functional>
#include <QPair>
#include <QDateTime>
#include "centroidbridgewrapper.h"
#include "centroidstatewrapper.h"
#include "vcpcontroller.h"
#include "uniteditor.h"
#include "programmepage.h"     // BlockData
#include "gcodeengine.h"       // GCodeLine
#include "simulationpage.h"    // SimulationPage
#include "filebrowserpage.h"

QT_BEGIN_NAMESPACE
namespace Ui { class EazyNC_MainWindow; }
QT_END_NAMESPACE

class ExecutionPage;
class StockAlphaSoftkey;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    SimulationPage *simPage() const { return m_simPage; }

private slots:
    void updateDisplay();
    void switchPage(int index);
    void enterSelectionMode();
    void exitSelectionMode();
    void onGCodeReady(const QVector<BlockData> &blocks,
                      const QVector<GCodeLine> &gcode,
                      const QString &name);
    void onUnitEditorSoftkeyContext(UnitEditor::SoftkeyContext ctx,
                                    QStringList labels,
                                    int activeIndex,
                                    QString activeValue);

private:
    // Methode ordinaire (pas un slot — std::function incompatible avec MOC)
    void openToolTableDialog(bool byName,
                             std::function<void(const ToolData&)> callback);

    Ui::EazyNC_MainWindow *ui;

    VcpController         *vcp    = nullptr;
    CentroidBridgeWrapper *bridge = nullptr;
    CentroidStateWrapper   state;

    QTimer *timer = nullptr;

    // Pages injectees en C++
    ProgrammePage   *m_programmePage   = nullptr;
    ExecutionPage   *m_executionPage   = nullptr;
    SimulationPage  *m_simPage         = nullptr;
    FileBrowserPage *m_fileBrowserPage = nullptr;

    // Page softkeys contextuelle (UnitEditor)
    QWidget     *m_ctxSoftkeyPage      = nullptr;
    QHBoxLayout *m_ctxSoftkeyLayout    = nullptr;
    int          m_ctxSoftkeyPageIndex = -1;

    // Etat courant
    int  m_currentPage    = 0;
    int  m_simSkPage      = 0;     // page softkeys Simulation (0 ou 1)
    bool m_simStockOpaque = false; // etat brut plein/transparent
    StockAlphaSoftkey *m_skStockAlpha = nullptr; // softkey custom −/+ opacite brut
    // m_stockTransparent supprimé — mode plein unique
    int  m_previousPage   = 0;
    int  m_progSkPage     = 0;     // 0=insertion 1=edition 2=selection
    int  m_selReturnPage  = 0;     // barre a retrouver en quittant la selection
    void showProgPage(int subPage);

    // Navigation hierarchique des cycles
    void setupProgSoftkeys();
    void showCycleNav(int pageIdx);

    // Pages dynamiques cycle navigation
    QVector<int> m_navHistory;     // pile de navigation pour le <

    // Indices des pages dans softkeyStack
    int m_skIdx_main      = -1;
    int m_skIdx_familles  = -1;
    int m_skIdx_percage   = -1;
    int m_skIdx_poches    = -1;
    int m_skIdx_contour   = -1;
    int m_skIdx_surfacage = -1;
    int m_skIdx_transfo   = -1;

    bool modeInc   = false;
    bool modeRapid = false;
    bool modeMan   = false;
    bool modeMPG   = false;
    int  jogStep   = 1;

    // Setup
    void setupNavigation();
    void showSaveOverlay();
    void setupVcp();
    void setupMdi();
    void setupDroButtons();
    void setupSoftkeys();
    void sendCommand(const QString &cmd);

    // ── Bouton Configuration (softkey mode manuel) ────────
    QToolButton *m_configButton = nullptr;
    QToolButton *m_skGenerer = nullptr;        // softkey « Générer G-code »
    void chargerDansCnc12(const QString &path, std::function<void(bool)> fini);
    QPair<QString, QDateTime> m_dernierEnvoi;   // dernier .nc chargé dans CNC12 (chemin, date)
    int  m_ticsAffichage = 0;
    void setupConfigButton();

    // Reapplique les textes traduits (effet immediat changement langue)
    void retranslateUi();

    // Navigation
    void updateTabStyles(int activeIndex);

    QLabel *m_saveOverlay = nullptr;

    // Rafraichissements
    void refreshIncCont();
    void refreshLentRapide();
    void refreshManAuto();
    void refreshJogSteps();
    void refreshWorkDro();
    void refreshMachineDro();
    void refreshDTG();
    void refreshTool();
    void refreshFeedrate();
    void refreshFeedOverride();
    void refreshSpindle();
    void refreshMessages();
};

#endif // MAINWINDOW_H
