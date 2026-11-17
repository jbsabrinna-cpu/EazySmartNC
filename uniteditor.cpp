#pragma execution_character_set("utf-8")

#include "uniteditor.h"
#include "pointfile.h"
#include "ui_UnitEditor.h"
#include "tooldatabase.h"
#include "pointspreviewwidget.h"
#include "fraisagetrougenerator.h"
#include <QFontMetrics>

#include "modeusin_widget.h"   // ← ajout pour le widget Mode Usinage

#include <QFileInfo>
#include <QApplication>
#include <QFocusEvent>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QListWidget>
#include <QFileDialog>
#include <cmath>
#include "eazyfiledialog.h"

// ═══════════════════════════════════════════════════════════════
// PlongeeIconWidget supprimé — icônes gérées via :/options/ (QPixmap)



// ═══════════════════════════════════════════════════════════════
//  BadgeG — widget rond indiquant "paramètre hérité du BLK FORM"
//  - VERT  : valeur héritée (inchangée par l'utilisateur)
//  - ROUGE : valeur modifiée localement (n'hérite plus)
//  Un clic sur le badge rouge restaure la valeur héritée (→ vert).
// ═══════════════════════════════════════════════════════════════
class BadgeG : public QPushButton
{
public:
    explicit BadgeG(QWidget *parent = nullptr) : QPushButton(parent) {
        setText("G");
        setFixedSize(22, 22);
        setCursor(Qt::PointingHandCursor);
        setFlat(true);
        setFocusPolicy(Qt::NoFocus);
        setHeritance(true);
    }
    bool heritance() const { return m_inherit; }
    void setHeritance(bool on) {
        m_inherit = on;
        QString col  = on ? "#00AA00" : "#CC0000";
        QString tip  = on ? QStringLiteral("Valeur héritée du BLK FORM")
                          : QStringLiteral("Valeur modifiée localement\nCliquer pour restaurer la valeur du BLK FORM");
        setToolTip(tip);
        setStyleSheet(QString(
            "QPushButton{background:%1;color:#FFFFFF;"
            "  font-weight:bold;font-size:10pt;"
            "  border-radius:11px;border:none;}"
            "QPushButton:hover{background:%2;}").arg(col, on?"#00CC00":"#EE3333"));
    }
    // Callback à appeler quand le badge rouge est cliqué
    std::function<void()> onReset;
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (!m_inherit && onReset) onReset();
        QPushButton::mousePressEvent(e);
    }
private:
    bool m_inherit = true;
};

UnitEditor::UnitEditor(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::UnitEditor)
{
    ui->setupUi(this);

    // ── Nom outil affiché À CÔTÉ du numéro (même ligne) ──────────
    //  Le champ T reste TOUJOURS numérique (obligatoire pour que le
    //  générateur G-code retrouve l'outil via toolByT()). Le nom
    //  n'est qu'informatif, mis à jour dès qu'un outil est sélectionné.
    //  Deux cadres sur la même ligne : carré numéro + rectangle nom.

    // ── Alignement des labels du Sommaire sur "Profondeur" ───────
    //  lblSomT/lblSomS sont figés à 30px dans le .ui, bien plus étroit
    //  que "Profondeur" (label le plus long du bloc Ø fini/Profondeur/
    //  Pas hélico, qui utilise lui un vrai QFormLayout auto-aligné).
    //  On harmonise pour que tous les champs commencent à la même
    //  position X, comme sur la page PM (déjà parfaitement alignée).
    if (ui->lblSomProfondeur) {
        int wRef = QFontMetrics(ui->lblSomProfondeur->font())
                       .horizontalAdvance(ui->lblSomProfondeur->text()) + 10;
        if (ui->lblSomT) ui->lblSomT->setMinimumWidth(wRef);
        if (ui->lblSomS) ui->lblSomS->setMinimumWidth(wRef);
    }

    // Sommaire : frameT contient déjà un QHBoxLayout (layoutT) —
    // on insère juste le nouveau label dedans, après editSomT.
    // GARDE-FOU : sans "if (!m_lblSomToolName)", ce bloc dupliquerait
    // le label à chaque rechargement (setBlockMode rappelé).
    if (ui->layoutT && !m_lblSomToolName) {
        int idxSomT = ui->layoutT->indexOf(ui->editSomT);
        ui->editSomT->setMaximumWidth(70);  // "carré" numéro
        m_lblSomToolName = new QLabel;
        m_lblSomToolName->setStyleSheet(
            "background:#FFFFFF;color:#204060;font-weight:bold;font-size:15px;"
            "border:1px solid #909090;border-radius:3px;padding:2px 6px;");
        // Pas de stretch=1 (débordait au-delà de lblSomTMode) — largeur
        // alignée sur les autres champs (S, avances) de la même colonne.
        ui->layoutT->insertWidget(idxSomT + 1, m_lblSomToolName);
        ui->layoutT->setAlignment(m_lblSomToolName, Qt::AlignVCenter);
        m_lblSomToolName->setFixedHeight(ui->editSomT->sizeHint().height());
        if (ui->editSomS) m_lblSomToolName->setMinimumWidth(ui->editSomS->sizeHint().width());
    }

    // Tool : reconstruction de la ligne T en côte-à-côte (numéro +
    // nom), via takeRow() — méthode Qt conçue pour restructurer une
    // ligne de QFormLayout proprement. GARDE-FOU obligatoire : sans
    // "if (!m_lblToolName)", ce bloc se ré-exécutait à CHAQUE appel
    // de setBlockMode(FraisageTrou) (donc à chaque rechargement de
    // bloc/programme), ré-appliquant takeRow()+insertRow() sur une
    // ligne déjà restructurée — corrompant le formulaire (labels
    // "F desc."/"F pendul." tronqués en "(mm/min)"/"(mm/tr)").
    if (ui->formTool && !m_lblToolName) {
        int row; QFormLayout::ItemRole role;
        ui->formTool->getWidgetPosition(ui->editToolT, &row, &role);
        if (row >= 0) {
            QFormLayout::TakeRowResult taken = ui->formTool->takeRow(row);
            QWidget *labelW = taken.labelItem ? taken.labelItem->widget() : nullptr;

            auto *wrap = new QWidget;
            auto *hl = new QHBoxLayout(wrap);
            hl->setContentsMargins(0,0,0,0); hl->setSpacing(4);
            ui->editToolT->setMaximumWidth(70);  // "carré" numéro
            m_lblToolName = new QLabel;
            m_lblToolName->setStyleSheet(
                "background:#FFFFFF;color:#204060;font-weight:bold;font-size:15px;"
                "border:1px solid #909090;border-radius:3px;padding:2px 6px;");
            hl->addWidget(ui->editToolT);
            hl->addWidget(m_lblToolName, 1);  // "rectangle" nom, extensible
            hl->setAlignment(m_lblToolName, Qt::AlignVCenter);
            m_lblToolName->setFixedHeight(ui->editToolT->sizeHint().height());

            if (labelW) ui->formTool->insertRow(row, labelW, wrap);
            else        ui->formTool->insertRow(row, "", wrap);

            delete taken.labelItem;    // ne détruit QUE le QLayoutItem, pas le widget
            delete taken.fieldItem;
        }

        // ── Réorganisation de l'onglet Outil en sections ─────────
        //  (outil, broche, avances, corrections, fonctions M) avec des
        //  libellés complets : les anciens libellés-icônes 24 px
        //  tronquaient le texte ("(m", "I. (n"...).
        {
            QFormLayout *f = ui->formTool;
            auto prendre = [f](QWidget *champ) -> QPair<QWidget*, QWidget*> {
                int r = -1; QFormLayout::ItemRole ro;
                f->getWidgetPosition(champ, &r, &ro);
                if (r < 0) return {nullptr, champ};
                QFormLayout::TakeRowResult t = f->takeRow(r);
                QWidget *l = t.labelItem ? t.labelItem->widget() : nullptr;
                QWidget *c = t.fieldItem ? t.fieldItem->widget() : champ;
                delete t.labelItem; delete t.fieldItem;
                return {l, c};
            };
            auto section = [](const QString &t) {
                auto *l = new QLabel(t);
                l->setStyleSheet("color:#3a8f34;font-size:15px;font-weight:700;"
                                 "letter-spacing:2px;padding-top:6px;");
                return l;
            };
            auto ligneSep = []() {
                auto *s = new QFrame; s->setFrameShape(QFrame::HLine);
                s->setStyleSheet("QFrame{background:#c8ccd8;}"); return s;
            };
            QWidget *wrapT = ui->editToolT->parentWidget();
            const auto rT   = prendre(wrapT && wrapT != ui->tabTool ? wrapT : ui->editToolT);
            const auto rS   = prendre(ui->editToolS);
            const auto rBr  = prendre(ui->widgetBroche);
            const auto rDe  = prendre(ui->editToolDescente);
            const auto rPe  = prendre(ui->editToolPendul);
            const auto rAv  = prendre(ui->editToolAvance);
            const auto rDL  = prendre(ui->editToolDL);
            const auto rDR  = prendre(ui->editToolDR);
            const auto rDR2 = prendre(ui->editToolDR2);
            const auto rM1  = prendre(ui->editToolFM1);
            const auto rM2  = prendre(ui->editToolFM2);

            // Libellés : texte simple, plus d'icône à taille fixe
            for (QLabel *l : {ui->lblToolT, ui->lblToolS, ui->lblToolDescente, ui->lblToolPendul,
                              ui->lblToolAvance, ui->lblToolDL, ui->lblToolDR, ui->lblToolDR2,
                              ui->lblToolFM1, ui->lblToolFM2, ui->lblToolBroche}) {
                if (!l) continue;
                l->setPixmap(QPixmap());
                l->setScaledContents(false);
                l->setMinimumSize(0, 0);
                l->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
                QFont fo = l->font(); fo.setBold(false); l->setFont(fo);
            }
            ui->lblToolT->setText("Numéro / nom");
            ui->lblToolS->setText("Vitesse broche S (tr/min)");
            ui->lblToolBroche->setText("Sens de rotation");
            ui->lblToolDescente->setText("Avance plongée (mm/min)");
            ui->lblToolPendul->setText("Avance rampe (mm/min)");
            ui->lblToolAvance->setText("Avance travail (mm/min)");
            ui->lblToolDL->setText("Correction longueur DL (mm)");
            ui->lblToolDR->setText("Correction rayon DR (mm)");
            ui->lblToolDR2->setText("Correction rayon d'angle DR2 (mm)");
            ui->lblToolFM1->setText("Fonction M 1");
            ui->lblToolFM2->setText("Fonction M 2");
            ui->radioM03->setText("M3  (sens horaire)");
            ui->radioM04->setText("M4  (sens anti-horaire)");
            ui->editToolDL->setPlaceholderText("0 = pas de correction");
            ui->editToolDR->setPlaceholderText("0 = pas de correction");
            ui->editToolDR2->setPlaceholderText("0 = pas de correction");
            ui->editToolFM1->setPlaceholderText("ex. M8 (arrosage)");
            ui->editToolFM2->setPlaceholderText("ex. M7 (brouillard)");

            f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            auto ajouter = [f](const QPair<QWidget*, QWidget*> &r) {
                if (r.first) f->addRow(r.first, r.second); else f->addRow(QString(), r.second);
            };
            f->addRow(section("OUTIL"));            ajouter(rT);
            f->addRow(ligneSep());
            f->addRow(section("BROCHE"));           ajouter(rS);  ajouter(rBr);
            f->addRow(ligneSep());
            f->addRow(section("AVANCES"));          ajouter(rDe); ajouter(rPe); ajouter(rAv);
            f->addRow(ligneSep());
            f->addRow(section("CORRECTIONS OUTIL")); ajouter(rDL); ajouter(rDR); ajouter(rDR2);
            f->addRow(ligneSep());
            f->addRow(section("FONCTIONS M"));      ajouter(rM1); ajouter(rM2);
        }
    }

    // ── Recherche automatique du nom quand un NUMÉRO est tapé ────
    //  directement (sans passer par le tableau) — le nom déjà connu
    //  dans la base outils s'affiche quand même, pour la lisibilité.
    {
        auto lookupAndShowName = [this](const QString &text){
            bool ok = false;
            int t = text.trimmed().toInt(&ok);
            QString name;
            if (ok && t > 0) {
                if (const ToolData *td = ToolDatabase::instance().toolByT(t))
                    name = td->description;
            }
            if (m_lblSomToolName) m_lblSomToolName->setText(name);
            if (m_lblToolName)    m_lblToolName->setText(name);
        };
        connect(ui->editSomT,  &QLineEdit::textChanged, this, lookupAndShowName);
        connect(ui->editToolT, &QLineEdit::textChanged, this, lookupAndShowName);
    }

    // ─────────────────────────────────────────────
    //  Mode Usinage (Ébauche / Finition)
    // ─────────────────────────────────────────────
    m_modeUsin = new ModeUsinWidget(this);

    // On insère le widget tout en haut du panneau PM Poche
    ui->layoutPMpoche->insertWidget(0, m_modeUsin);

    connect(m_modeUsin, &ModeUsinWidget::modeChanged, this, [this](int m) {
        m_modeUsinValue = m;
        if (!m_syncing) emit dataChanged();
    });
    connect(m_modeUsin, &ModeUsinWidget::helpHover, this, &UnitEditor::helpRequested);

    // ─────────────────────────────────────────────
    //  Initialisation standard
    // ─────────────────────────────────────────────
    setupPositionPocheGroup();
    setupPlongeeIcons();
    setupPositionnementWidget();
    setupTrouTab();
    setupContourTab();
    setupFraisageTrouTab();
    setupConnections();
    setupToolSelectButton();
    ui->radioM03->setChecked(true);
    updateBadgeS();
    updateBadgeDescente();
    updateBadgePendul();
    updateBadgeAvance();
    connect(this, &UnitEditor::dataChanged, this, &UnitEditor::updateFtPointsPreview);
    QTimer::singleShot(0, this, [this]{ applyStyleSheet(); });
}

UnitEditor::~UnitEditor()
{
    delete ui;
}

static const QString CSS = QStringLiteral(
    // Taille de texte augmentée : la colonne de saisie a de la place,
    // on privilégie la lisibilité (pupitre / écran d'atelier).
    "QWidget { background:#D6D6D6; color:#202020; font-size:12.5pt; }"
    "QTabWidget::pane { background:#D6D6D6; border:1px solid #808080; }"
    "QTabBar::tab { background:#C0C0C0; color:#404040; padding:6px 16px; font-size:11.5pt;"
    "  border:1px solid #909090; border-bottom:none; border-radius:3px 3px 0 0; }"
    "QTabBar::tab:selected { background:#D6D6D6; color:#0000FF; font-weight:bold; }"
    "QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {"
    "  background:#FFFFFF; color:#0000CC; font-size:13pt;"
    "  border:1px solid #909090; border-radius:3px; padding:2px 5px; }"
    "QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus {"
    "  border:2px solid #0000FF; }"
    "QCheckBox { color:#202020; font-size:12pt; }"
    "QRadioButton { color:#202020; font-size:12pt; }"
    "QPushButton { background:#E0E0E0; color:#202020; font-size:12pt;"
    "  border:1px solid #909090; border-radius:3px; padding:4px 10px; }"
    "QPushButton:pressed { background:#C0C0C0; }"
    "QLabel[badge=true] { background:#0000FF; color:#FFFFFF;"
    "  border-radius:3px; padding:1px 4px; font-size:9pt; }"
    // Sélecteurs préfixés par UnitEditor : plus spécifiques que les règles
    // de la feuille de style globale (TNC620 : QLineEdit/QLabel en 11pt)
    "UnitEditor QLabel { font-size:13pt; }"
    "UnitEditor QRadioButton, UnitEditor QCheckBox { font-size:13pt; }"
    "UnitEditor QLineEdit, UnitEditor QSpinBox, UnitEditor QDoubleSpinBox, UnitEditor QComboBox {"
    "  font-size:14pt; padding:3px 6px; }"
    "UnitEditor QTabBar::tab { font-size:12pt; padding:7px 16px; }"
    "UnitEditor QLabel[badge=true] { font-size:10pt; }"
);

BadgeG* UnitEditor::installBadgeG(QWidget *field, const QString &tooltip,
                                   std::function<void()> onReset)
{
    if (!field) return nullptr;
    QWidget *parent = field->parentWidget();
    auto *badge = new BadgeG(parent);
    badge->setToolTip(tooltip);
    badge->onReset = [this, badge, onReset]{
        if (onReset) onReset();
        badge->setHeritance(true);
        if (!m_syncing) emit dataChanged();
    };

    QLayout *pl = parent ? parent->layout() : nullptr;
    if (!pl) { badge->show(); return badge; }

    if (auto *fl = qobject_cast<QFormLayout*>(pl)) {
        int row; QFormLayout::ItemRole role;
        fl->getWidgetPosition(field, &row, &role);
        if (row >= 0) {
            auto *wrap = new QWidget(parent);
            auto *wl = new QHBoxLayout(wrap);
            wl->setContentsMargins(0,0,0,0); wl->setSpacing(6);
            fl->removeWidget(field);
            field->setParent(wrap);
            wl->addWidget(field, 1);
            wl->addWidget(badge, 0);
            fl->setWidget(row, role, wrap);
            return badge;
        }
    }
    if (auto *bl = qobject_cast<QBoxLayout*>(pl)) {
        int i = bl->indexOf(field);
        if (i >= 0) { bl->insertWidget(i + 1, badge); return badge; }
    }
    if (auto *gl = qobject_cast<QGridLayout*>(pl)) {
        int r, c, rs, cs;
        gl->getItemPosition(gl->indexOf(field), &r, &c, &rs, &cs);
        gl->addWidget(badge, r, c + cs);
        return badge;
    }
    badge->show();
    return badge;
}


void UnitEditor::setBlkFormHerited(bool origineDefinie, int origine,
                                    double zSautBride, double zLiaison,
                                    double zApproche,  double avanceLiaison)
{
    m_heritedOrigineDef    = origineDefinie;
    m_heritedOrigine       = origine;
    m_heritedSautBride     = zSautBride;
    m_heritedZLiaison      = zLiaison;
    m_heritedApproche      = zApproche;
    m_heritedAvanceLiaison = avanceLiaison;
}

void UnitEditor::applyStyleSheet()
{
    setStyleSheet(CSS);
    // Icônes des avances du Sommaire : 24 → 32 px
    for (QLabel *ico : {ui->lblSomDescente, ui->lblSomPendul, ui->lblSomAvance})
        if (ico) { ico->setMinimumSize(32, 32); ico->setMaximumSize(32, 32); }
    for (QLabel *lbl : {ui->lblSomTMode, ui->lblSomSMode,
                        ui->lblSomDescenteMode, ui->lblSomAvanceMode})
        lbl->setProperty("badge", false);
}

void UnitEditor::setReadOnly(bool ro)
{
    m_readOnly = ro;
    if (ro) {
        setStyleSheet(CSS +
                      "QLineEdit, QDoubleSpinBox, QSpinBox {"
                      "  background:#EBEBEB; color:#888888; border-color:#B0B0B0; }"
                      "QTabWidget::pane { background:#D6D6D6; }"
                      "QWidget { color:#6a7090; }");
    } else {
        setStyleSheet(CSS);
    }
    for (QLineEdit    *w : findChildren<QLineEdit*>())    w->setReadOnly(ro);
    for (QAbstractButton *w : findChildren<QAbstractButton*>()) w->setEnabled(!ro);
    for (QSpinBox     *w : findChildren<QSpinBox*>())     w->setReadOnly(ro);
    for (QDoubleSpinBox *w : findChildren<QDoubleSpinBox*>()) w->setReadOnly(ro);
}

void UnitEditor::refreshPmTabIndexes()
{
    m_trouTabIndex         = m_trouTabW         ? ui->tabWidget->indexOf(m_trouTabW)         : -1;
    m_contourTabIndex      = m_contourTabW      ? ui->tabWidget->indexOf(m_contourTabW)      : -1;
    m_fraisageTrouTabIndex = m_fraisageTrouTabW ? ui->tabWidget->indexOf(m_fraisageTrouTabW) : -1;
}

void UnitEditor::setBlockMode(BlockMode mode)
{
    refreshPmTabIndexes();
    if (m_blockMode == mode) return;
    m_blockMode = mode;

    const bool isPocheVariant = (mode == Poche || mode == PocheRect || mode == PocheCirc);
    const bool isFraisageTrou = (mode == FraisageTrou);
    if (mode == Poche) m_blockMode = PocheRect;

    int pmPocheIdx = -1;
    for (int i = 0; i < ui->tabWidget->count(); ++i)
        if (ui->tabWidget->widget(i) == ui->tabPMpoche)
            pmPocheIdx = i;

    int posIdx   = -1;
    int globIdx  = -1;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        if (ui->tabWidget->widget(i) == ui->tabPosition)        posIdx  = i;
        if (ui->tabWidget->widget(i) == ui->tabDonneesGlobales) globIdx = i;
    }

    if (mode == Trou) {
        if (pmPocheIdx >= 0)              ui->tabWidget->setTabVisible(pmPocheIdx, false);
        if (m_trouTabIndex >= 0)          ui->tabWidget->setTabVisible(m_trouTabIndex, true);
        if (m_contourTabIndex >= 0)       ui->tabWidget->setTabVisible(m_contourTabIndex, false);
        if (m_fraisageTrouTabIndex >= 0)  ui->tabWidget->setTabVisible(m_fraisageTrouTabIndex, false);
        if (posIdx >= 0)                  ui->tabWidget->setTabVisible(posIdx, true);
        if (globIdx >= 0)                 ui->tabWidget->setTabVisible(globIdx, false);
    } else if (isFraisageTrou) {
        if (pmPocheIdx >= 0)              ui->tabWidget->setTabVisible(pmPocheIdx, false);
        if (m_trouTabIndex >= 0)          ui->tabWidget->setTabVisible(m_trouTabIndex, false);
        if (m_contourTabIndex >= 0)       ui->tabWidget->setTabVisible(m_contourTabIndex, false);
        if (m_fraisageTrouTabIndex >= 0)  ui->tabWidget->setTabVisible(m_fraisageTrouTabIndex, true);
        if (posIdx >= 0)                  ui->tabWidget->setTabVisible(posIdx, true);
        if (globIdx >= 0)                 ui->tabWidget->setTabVisible(globIdx, false);
    } else if (mode == Contour) {
        if (pmPocheIdx >= 0)         ui->tabWidget->setTabVisible(pmPocheIdx, false);
        if (m_trouTabIndex >= 0)     ui->tabWidget->setTabVisible(m_trouTabIndex, false);
        if (m_contourTabIndex >= 0)  ui->tabWidget->setTabVisible(m_contourTabIndex, true);
        if (m_fraisageTrouTabIndex >= 0) ui->tabWidget->setTabVisible(m_fraisageTrouTabIndex, false);
        if (posIdx >= 0)             ui->tabWidget->setTabVisible(posIdx, false);
        if (globIdx >= 0)            ui->tabWidget->setTabVisible(globIdx, false);
    } else {
        if (m_trouTabIndex >= 0)          ui->tabWidget->setTabVisible(m_trouTabIndex, false);
        if (m_contourTabIndex >= 0)       ui->tabWidget->setTabVisible(m_contourTabIndex, false);
        if (m_fraisageTrouTabIndex >= 0)  ui->tabWidget->setTabVisible(m_fraisageTrouTabIndex, false);
        if (pmPocheIdx >= 0)              ui->tabWidget->setTabVisible(pmPocheIdx, true);
        if (posIdx >= 0)                  ui->tabWidget->setTabVisible(posIdx, true);
        if (globIdx >= 0)                 ui->tabWidget->setTabVisible(globIdx, true);
        // Restaurer les labels et widgets modifiés par FraisageTrou
        if (ui->lblSomPasse)    ui->lblSomPasse->setText("Passe");
        if (ui->lblSomDescente) ui->lblSomDescente->setPixmap(QPixmap(":/unit/avance descente.png"));  // icône (un texte était tronqué en "sce")
        if (ui->lblCRayon)      ui->lblCRayon->setText("Rayon");
        if (ui->editPositionFichier) ui->editPositionFichier->setVisible(true);
        for (QSpinBox *sb : {ui->spinLigneNbPts, ui->spinGrilleNbX,
                             ui->spinGrilleNbY,  ui->spinCercleNbPts}) {
            if (sb) sb->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        }
        if (ui->frameDescente)  ui->frameDescente->setVisible(true);
        if (ui->lblSomDescenteMode) ui->lblSomDescenteMode->setVisible(true);
        if (ui->lblSomAvanceMode)   ui->lblSomAvanceMode->setVisible(true);
        if (ui->lblSomSMode)        ui->lblSomSMode->setVisible(true);
        if (m_somFtRbG0)   m_somFtRbG0->setVisible(false);
        if (m_somFtRbFpos) m_somFtRbFpos->setVisible(false);
        if (m_somFtRbF)    m_somFtRbF->setVisible(false);
        if (m_somFtRbFz)   m_somFtRbFz->setVisible(false);
        if (m_somFtRbRpm)  m_somFtRbRpm->setVisible(false);
        if (m_somFtRbVc)   m_somFtRbVc->setVisible(false);
        if (ui->editSomDescente) ui->editSomDescente->setEnabled(true);
        for (int i = 0; i < kFtSomPts; ++i)
            if (m_somFtPX[i]) {
                m_somFtPX[i]->setVisible(false);
                m_somFtPX[i]->parentWidget()->setVisible(false);
                if (m_somFtLbl[i]) m_somFtLbl[i]->setVisible(false);
            }
        if (m_somFtModeIcon) m_somFtModeIcon->setVisible(false);
        if (m_ftPointsPreview) m_ftPointsPreview->setVisible(false);
        if (ui->lblSomModelePts)  ui->lblSomModelePts->setVisible(true);
        if (ui->editSomModelePts) ui->editSomModelePts->setVisible(true);
    }

    // ── Contour : pas de positions ni de fichier de points ─────────
    //  Le contour porte sa propre géométrie (.cnt). Avant, en passant
    //  d'un Fraisage trou à un Contour, l'aperçu des positions, les
    //  champs Point 1..4 et « Modèle de points / fichier » restaient
    //  affichés dans le Sommaire.
    if (mode == Contour) {
        if (ui->lblSomPasse)    ui->lblSomPasse->setText("Passe");
        if (ui->lblSomDescente) ui->lblSomDescente->setPixmap(QPixmap(":/unit/avance descente.png"));  // icône (un texte était tronqué en "sce")
        if (ui->frameDescente)  ui->frameDescente->setVisible(true);
        if (ui->lblSomDescenteMode) ui->lblSomDescenteMode->setVisible(true);
        if (ui->lblSomAvanceMode)   ui->lblSomAvanceMode->setVisible(true);
        if (ui->lblSomSMode)        ui->lblSomSMode->setVisible(true);
        for (QWidget *w : std::initializer_list<QWidget*>{m_somFtRbG0, m_somFtRbFpos, m_somFtRbF,
                                                         m_somFtRbFz, m_somFtRbRpm, m_somFtRbVc})
            if (w) w->setVisible(false);
        if (ui->editSomDescente) ui->editSomDescente->setEnabled(true);
        for (int i = 0; i < kFtSomPts; ++i)
            if (m_somFtPX[i]) {
                m_somFtPX[i]->parentWidget()->setVisible(false);
                if (m_somFtLbl[i]) m_somFtLbl[i]->setVisible(false);
            }
        if (m_somFtModeIcon)      m_somFtModeIcon->setVisible(false);
        if (m_ftPointsPreview)    m_ftPointsPreview->setVisible(false);
        // Le champ « Modèle de points » devient le choix du CONTOUR :
        // un clic dessus propose Nouveau / Fichier .cnt / Éditer / DXF.
        if (ui->lblSomModelePts)  { ui->lblSomModelePts->setVisible(true);
                                    ui->lblSomModelePts->setText("Contour (cliquer pour choisir)"); }
        if (ui->editSomModelePts) { ui->editSomModelePts->setVisible(true);
                                    ui->editSomModelePts->setPlaceholderText("Aucun contour - cliquer ici"); }
        if (ui->editPositionFichier) ui->editPositionFichier->setVisible(false);
    } else if (ui->editSomModelePts) {
        ui->editSomModelePts->setPlaceholderText("Aucun fichier");
    }

    // Avance pendulaire : inutile en perçage → masquée en mode Trou,
    // visible pour Poche et Contour.
    setPendulVisible(mode != Trou && !isFraisageTrou);

    if (ui->frameAvance)  ui->frameAvance->setVisible(mode != Trou && !isFraisageTrou);

    if (ui->framePocheType)  ui->framePocheType->setVisible(false);

    if (isFraisageTrou) {
        // Sommaire FraisageTrou : Ø fini + profondeur + pas hélico + avance travail
        const QString rbStyle = R"(
            QRadioButton { color:#888; background:#222; border:1px solid #444;
                padding:1px 6px; border-radius:3px; font-size:10px; }
            QRadioButton::indicator { width:0; height:0; }
            QRadioButton:checked { color:#000; background:#A6CE39;
                border:1px solid #A6CE39; font-weight:bold; }
        )";
        if (ui->widgetDimRect)    ui->widgetDimRect->setVisible(false);
        if (ui->widgetDimCirc)    ui->widgetDimCirc->setVisible(true);
        if (ui->widgetPMRect)     ui->widgetPMRect->setVisible(false);
        if (ui->lblSomLongueur)   ui->lblSomLongueur->setVisible(false);
        if (ui->lblSomLargeur)    ui->lblSomLargeur->setVisible(false);
        if (ui->lblSomRayon)      ui->lblSomRayon->setVisible(false);
        if (ui->editSomLongueur)  ui->editSomLongueur->setVisible(false);
        if (ui->editSomLargeur)   ui->editSomLargeur->setVisible(false);
        if (ui->editSomRayon)     ui->editSomRayon->setVisible(false);
        if (ui->lblSomDiametre)   ui->lblSomDiametre->setText("Ø fini");
        if (ui->editSomDiametre)  ui->editSomDiametre->setVisible(true);
        if (ui->lblSomProfondeur) ui->lblSomProfondeur->setVisible(true);
        if (ui->editSomProfondeur)ui->editSomProfondeur->setVisible(true);
        if (ui->lblSomPasse)      { ui->lblSomPasse->setText("Pas hélico"); ui->lblSomPasse->setVisible(true); }
        if (ui->editSomPasse)     ui->editSomPasse->setVisible(true);
        if (ui->lblSomSurepLat)   ui->lblSomSurepLat->setVisible(false);
        if (ui->editSomSurepLat)  ui->editSomSurepLat->setVisible(false);
        if (ui->lblSomSurepProf)  ui->lblSomSurepProf->setVisible(false);
        if (ui->editSomSurepProf) ui->editSomSurepProf->setVisible(false);
        if (ui->frameAvance)      ui->frameAvance->setVisible(true);
        if (ui->frameDescente)    ui->frameDescente->setVisible(true);

        // ── Radios broche S (frameS) ──────────────────────────
        if (!m_somFtRbRpm) {
            m_somFtRbRpm = new QRadioButton("tr/min");
            m_somFtRbVc  = new QRadioButton("Vc");
            m_somFtRbRpm->setStyleSheet(rbStyle);
            m_somFtRbVc->setStyleSheet(rbStyle);
            m_somFtRbRpm->setChecked(true);
            auto *bgS2 = new QButtonGroup(this);
            bgS2->addButton(m_somFtRbRpm, 0);
            bgS2->addButton(m_somFtRbVc,  1);
            if (ui->layoutS) {
                if (ui->lblSomSMode) ui->lblSomSMode->setVisible(false);
                ui->layoutS->addWidget(m_somFtRbRpm);
                ui->layoutS->addWidget(m_somFtRbVc);
            }
            connect(m_somFtRbVc, &QRadioButton::toggled, this, [this](bool vc){
                if (m_blockMode == FraisageTrou) {
                    m_modeS = vc ? 1 : 0;
                    updateBadgeS();
                    if (m_ftRbVc) m_ftRbVc->setChecked(vc);
                    if (m_ftRbRpm) m_ftRbRpm->setChecked(!vc);
                }
            });
        }
        m_somFtRbRpm->setChecked(m_modeS == 0);
        m_somFtRbVc->setChecked(m_modeS == 1);
        m_somFtRbRpm->setVisible(true);
        m_somFtRbVc->setVisible(true);
        if (ui->lblSomSMode) ui->lblSomSMode->setVisible(false);

        // ── Radios positionnement (frameDescente) ─────────────
        if (!m_somFtRbG0) {
            m_somFtRbG0   = new QRadioButton("G0");
            m_somFtRbFpos = new QRadioButton("F");
            m_somFtRbG0->setStyleSheet(rbStyle);
            m_somFtRbFpos->setStyleSheet(rbStyle);
            auto *bgPos = new QButtonGroup(this);
            bgPos->addButton(m_somFtRbG0,   0);
            bgPos->addButton(m_somFtRbFpos, 1);
            // Insérer dans layoutDescente à la place du lblSomDescenteMode
            if (ui->layoutDescente) {
                if (ui->lblSomDescenteMode) ui->lblSomDescenteMode->setVisible(false);
                ui->layoutDescente->addWidget(m_somFtRbG0);
                ui->layoutDescente->addWidget(m_somFtRbFpos);
            }
            // Activer/désactiver le champ selon le radio
            connect(m_somFtRbFpos, &QRadioButton::toggled,
                    ui->editSomDescente, &QLineEdit::setEnabled);
            connect(m_somFtRbG0, &QRadioButton::toggled, this, [this](bool g0){
                if (m_blockMode == FraisageTrou) {
                    m_posG0 = g0;
                    ui->editSomDescente->setEnabled(!g0);
                    ui->editSomDescente->setPlaceholderText(g0 ? "G0 F-Rapide" : "mm/min");
                    ui->editSomDescente->setStyleSheet(g0 ? "font-weight:bold;" : "");
                    if (g0) ui->editSomDescente->clear();
                    // Synchro vers PM
                    if (!m_syncing) {
                        m_syncing = true;
                        if (m_ftRbLiaisonG0) m_ftRbLiaisonG0->setChecked(g0);
                        if (m_ftRbLiaisonF)  m_ftRbLiaisonF->setChecked(!g0);
                        m_syncing = false;
                    }
                }
            });
            // setChecked APRÈS le connect() — sinon le tout premier état
            // (G0 par défaut) ne déclenche jamais le handler ci-dessus,
            // et le champ reste avec son texte initial au lieu du
            // placeholder "G0 F-Rapide" (obligeant à recliquer F puis G0).
            m_somFtRbG0->setChecked(true);
        }
        m_somFtRbG0->setVisible(true);
        m_somFtRbFpos->setVisible(true);
        ui->editSomDescente->setEnabled(m_somFtRbFpos->isChecked());
        ui->editSomDescente->setPlaceholderText(m_somFtRbG0->isChecked() ? "G0 F-Rapide" : "mm/min");
        ui->editSomDescente->setStyleSheet(m_somFtRbG0->isChecked() ? "font-weight:bold;" : "");
        if (m_somFtRbG0->isChecked()) ui->editSomDescente->clear();

        // ── Radios avance travail (frameAvance) ───────────────
        if (!m_somFtRbF) {
            m_somFtRbF  = new QRadioButton("F");
            m_somFtRbFz = new QRadioButton("Fz");
            m_somFtRbF->setStyleSheet(rbStyle);
            m_somFtRbFz->setStyleSheet(rbStyle);
            m_somFtRbFz->setChecked(true);
            auto *bgAv = new QButtonGroup(this);
            bgAv->addButton(m_somFtRbF,  0);
            bgAv->addButton(m_somFtRbFz, 1);
            if (ui->layoutAvance) {
                if (ui->lblSomAvanceMode) ui->lblSomAvanceMode->setVisible(false);
                ui->layoutAvance->addWidget(m_somFtRbF);
                ui->layoutAvance->addWidget(m_somFtRbFz);
            }
            connect(m_somFtRbF,  &QRadioButton::toggled, this, [this](bool v){
                if (v && m_blockMode == FraisageTrou) { m_modeAvance=0; if(m_ftRbFmin) m_ftRbFmin->setChecked(true); }
            });
            connect(m_somFtRbFz, &QRadioButton::toggled, this, [this](bool v){
                if (v && m_blockMode == FraisageTrou) { m_modeAvance=2; if(m_ftRbFz) m_ftRbFz->setChecked(true); }
            });
        }
        m_somFtRbF->setVisible(true);
        m_somFtRbFz->setVisible(true);
        if (ui->lblSomDescenteMode) ui->lblSomDescenteMode->setVisible(false);
        if (ui->lblSomAvanceMode)   ui->lblSomAvanceMode->setVisible(false);

        // ── Champ "Fichier de points" — TOUJOURS visible ──────────
        //  Nécessaire : c'est en cliquant dedans (focus) que le menu
        //  softkey "Nouveau/Fichier/Éditer/DXF" apparaît (comme pour
        //  tous les autres cycles). Le masquer supprimait l'accès à
        //  la création/ouverture d'un fichier .pnt.
        if (ui->lblSomModelePts)  ui->lblSomModelePts->setVisible(true);
        if (ui->editSomModelePts) ui->editSomModelePts->setVisible(true);

        // ── "Fichier de points" (onglet Position) masqué aussi ────
        //  Widget partagé au-dessus des pages Ligne/Grille/Cercle/
        //  Tableau — inutile ici, Point/Tableau étant gérés par nos
        //  champs dédiés, et Ligne/Grille/Cercle par leurs propres
        //  paramètres numériques.
        if (ui->editPositionFichier) ui->editPositionFichier->setVisible(false);

        // ── Flèches +/- retirées des Nb points/colonnes/lignes ────
        //  Saisie directe au clavier uniquement, plus cohérent avec
        //  le reste de l'UI (aucun autre champ n'a de flèches).
        for (QSpinBox *sb : {ui->spinLigneNbPts, ui->spinGrilleNbX,
                             ui->spinGrilleNbY,  ui->spinCercleNbPts}) {
            if (sb) sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
        }

        // ── Icône du mode position actif, dupliquée dans le Sommaire ──
        if (!m_somFtModeIcon) {
            QLayout *parentLay2 = ui->editSomModelePts
                ? ui->editSomModelePts->parentWidget()->layout() : nullptr;
            if (auto *vbox2 = qobject_cast<QVBoxLayout*>(parentLay2)) {
                int idx2 = vbox2->indexOf(ui->editSomModelePts) + 1;
                m_somFtModeIcon = new QLabel;
                m_somFtModeIcon->setAlignment(Qt::AlignCenter);
                m_somFtModeIcon->setMinimumHeight(60);
                vbox2->insertWidget(idx2, m_somFtModeIcon);
            }
        }
        if (m_somFtModeIcon) m_somFtModeIcon->setVisible(true);

        // ── Aperçu graphique live des positions (tous modèles) ────
        //  Réutilise EXACTEMENT le même calcul que le générateur
        //  (FraisageTrouGenerator::computePositions) — l'aperçu ne
        //  peut donc jamais diverger du G-code réellement produit.
        if (!m_ftPointsPreview) {
            QLayout *parentLay3 = ui->editSomModelePts
                ? ui->editSomModelePts->parentWidget()->layout() : nullptr;
            if (auto *vbox3 = qobject_cast<QVBoxLayout*>(parentLay3)) {
                int idx3 = vbox3->indexOf(ui->editSomModelePts) + 1;
                m_ftPointsPreview = new PointsPreviewWidget;
                vbox3->insertWidget(idx3, m_ftPointsPreview);
            }
        }
        if (m_ftPointsPreview) {
            m_ftPointsPreview->setVisible(true);
            updateFtPointsPreview();
        }

        // ── Points 1..4 (Sommaire) — accès rapide ────────────────
        //  par défaut — c'est à l'opérateur de remplir, pas de
        //  "0.000" pré-rempli. Même style que la page Position
        //  dédiée ci-dessous (juste moins de lignes, pour la place).
        if (!m_somFtPX[0]) {
            QLayout *parentLay = ui->editSomModelePts
                ? ui->editSomModelePts->parentWidget()->layout() : nullptr;
            if (auto *vbox = qobject_cast<QVBoxLayout*>(parentLay)) {
                int insertIdx = vbox->indexOf(ui->editSomModelePts) + 1;
                for (int i = 0; i < kFtSomPts; ++i) {
                    auto *lbl = new QLabel(QString("Point %1").arg(i+1));
                    lbl->setStyleSheet("font-weight:bold;font-size:10px;color:#555;");
                    m_somFtLbl[i] = lbl;
                    auto *row = new QWidget;
                    auto *lay = new QHBoxLayout(row);
                    lay->setContentsMargins(0,0,0,0); lay->setSpacing(4);
                    m_somFtPX[i] = new QLineEdit; m_somFtPX[i]->setPlaceholderText("X");
                    m_somFtPY[i] = new QLineEdit; m_somFtPY[i]->setPlaceholderText("Y");
                    m_somFtPZ[i] = new QLineEdit; m_somFtPZ[i]->setPlaceholderText("Z");
                    lay->addWidget(new QLabel("X")); lay->addWidget(m_somFtPX[i]);
                    lay->addWidget(new QLabel("Y")); lay->addWidget(m_somFtPY[i]);
                    lay->addWidget(new QLabel("Z")); lay->addWidget(m_somFtPZ[i]);
                    vbox->insertWidget(insertIdx++, lbl);
                    vbox->insertWidget(insertIdx++, row);
                }
                auto ftPosDc = [this](){ if (!m_syncing) emit dataChanged(); };
                for (int i = 0; i < kFtSomPts; ++i)
                    for (QLineEdit *le : {m_somFtPX[i], m_somFtPY[i], m_somFtPZ[i]})
                        connect(le, &QLineEdit::textChanged, this, ftPosDc);
            }
        }

        // ── Onglet Position : page dédiée FraisageTrou — 10 points,
        //  même style. Remplace le mode "Tableau" générique (devenu
        //  redondant avec "Point" pour ce cycle).
        if (!m_ftPositionPage) {
            m_ftPositionPage = new QWidget;
            auto *pform = new QFormLayout(m_ftPositionPage);
            pform->setContentsMargins(8,8,8,8); pform->setSpacing(4);
            for (int i = 0; i < kFtPosPts; ++i) {
                auto *row = new QWidget;
                auto *lay = new QHBoxLayout(row);
                lay->setContentsMargins(0,0,0,0); lay->setSpacing(4);
                m_ftPosX[i] = new QLineEdit; m_ftPosX[i]->setPlaceholderText("X");
                m_ftPosY[i] = new QLineEdit; m_ftPosY[i]->setPlaceholderText("Y");
                m_ftPosZ[i] = new QLineEdit; m_ftPosZ[i]->setPlaceholderText("Z");
                lay->addWidget(new QLabel("X")); lay->addWidget(m_ftPosX[i]);
                lay->addWidget(new QLabel("Y")); lay->addWidget(m_ftPosY[i]);
                lay->addWidget(new QLabel("Z")); lay->addWidget(m_ftPosZ[i]);
                pform->addRow(QString("Point %1").arg(i+1), row);
            }
            auto *scroll = new QScrollArea;
            scroll->setWidget(m_ftPositionPage);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            ui->stackPosition->addWidget(scroll);
            m_ftPositionPage = scroll;  // le widget inséré dans le stack est le scroll

            // Synchro bidirectionnelle Sommaire (4 pts) ↔ Position (10 pts, mêmes 4 premiers)
            for (int i = 0; i < kFtSomPts; ++i) {
                connect(m_somFtPX[i], &QLineEdit::textChanged, this, [this,i](const QString &v){
                    if (m_syncing) return; m_syncing=true; if(m_ftPosX[i]) m_ftPosX[i]->setText(v); m_syncing=false;
                });
                connect(m_somFtPY[i], &QLineEdit::textChanged, this, [this,i](const QString &v){
                    if (m_syncing) return; m_syncing=true; if(m_ftPosY[i]) m_ftPosY[i]->setText(v); m_syncing=false;
                });
                connect(m_somFtPZ[i], &QLineEdit::textChanged, this, [this,i](const QString &v){
                    if (m_syncing) return; m_syncing=true; if(m_ftPosZ[i]) m_ftPosZ[i]->setText(v); m_syncing=false;
                });
                connect(m_ftPosX[i], &QLineEdit::textChanged, this, [this,i](const QString &v){
                    if (m_syncing) return; m_syncing=true; if(m_somFtPX[i]) m_somFtPX[i]->setText(v); m_syncing=false;
                });
                connect(m_ftPosY[i], &QLineEdit::textChanged, this, [this,i](const QString &v){
                    if (m_syncing) return; m_syncing=true; if(m_somFtPY[i]) m_somFtPY[i]->setText(v); m_syncing=false;
                });
                connect(m_ftPosZ[i], &QLineEdit::textChanged, this, [this,i](const QString &v){
                    if (m_syncing) return; m_syncing=true; if(m_somFtPZ[i]) m_somFtPZ[i]->setText(v); m_syncing=false;
                });
            }
            auto ftPosDc2 = [this](){ if (!m_syncing) emit dataChanged(); };
            for (int i = 0; i < kFtPosPts; ++i)
                for (QLineEdit *le : {m_ftPosX[i], m_ftPosY[i], m_ftPosZ[i]})
                    connect(le, &QLineEdit::textChanged, this, ftPosDc2);
        }

        for (int i = 0; i < kFtSomPts; ++i) {
            if (m_somFtPX[i]) {
                m_somFtPX[i]->setVisible(true); m_somFtPX[i]->parentWidget()->setVisible(true);
                if (m_somFtLbl[i]) m_somFtLbl[i]->setVisible(true);
            }
        }

        return;

    }

    const bool isCirc = (m_blockMode == PocheCirc);

    if (isPocheVariant) {
        updatePocheTypeVisibility(isCirc);
        if (ui->lblSomLongueur)   ui->lblSomLongueur->setVisible(!isCirc);
        if (ui->lblSomLargeur)    ui->lblSomLargeur->setVisible(!isCirc);
        if (ui->lblSomRayon)      ui->lblSomRayon->setVisible(!isCirc);
        if (ui->editSomLongueur)  ui->editSomLongueur->setVisible(!isCirc);
        if (ui->editSomLargeur)   ui->editSomLargeur->setVisible(!isCirc);
        if (ui->editSomRayon)     ui->editSomRayon->setVisible(!isCirc);
        if (ui->lblSomDiametre)   ui->lblSomDiametre->setText("Diametre");
        if (ui->editSomDiametre)  ui->editSomDiametre->setVisible(isCirc);
        if (ui->lblSomProfondeur) ui->lblSomProfondeur->setVisible(true);
        if (ui->editSomProfondeur)ui->editSomProfondeur->setVisible(true);
        if (ui->lblSomPasse)      ui->lblSomPasse->setVisible(true);
        if (ui->editSomPasse)     ui->editSomPasse->setVisible(true);
        if (ui->lblSomSurepLat)   ui->lblSomSurepLat->setVisible(true);
        if (ui->editSomSurepLat)  ui->editSomSurepLat->setVisible(true);
        if (ui->lblSomSurepProf)  ui->lblSomSurepProf->setVisible(true);
        if (ui->editSomSurepProf) ui->editSomSurepProf->setVisible(true);
    } else if (mode == Trou) {
        // Perçage : le Sommaire permet la programmation rapide → on y garde
        // la Profondeur (synchronisée avec l'onglet PM Trou). Le diamètre
        // reste masqué : il vient de l'outil. L'avance travail (horizontale)
        // n'a pas de sens pour un foret → masquée ; seule l'avance descente
        // sert d'avance de perçage.
        if (ui->widgetDimRect)    ui->widgetDimRect->setVisible(false);
        if (ui->widgetPMRect)     ui->widgetPMRect->setVisible(false);
        if (ui->widgetDimCirc)    ui->widgetDimCirc->setVisible(false);
        if (ui->lblSomLongueur)   ui->lblSomLongueur->setVisible(false);
        if (ui->lblSomLargeur)    ui->lblSomLargeur->setVisible(false);
        if (ui->lblSomRayon)      ui->lblSomRayon->setVisible(false);
        if (ui->editSomLongueur)  ui->editSomLongueur->setVisible(false);
        if (ui->editSomLargeur)   ui->editSomLargeur->setVisible(false);
        if (ui->editSomRayon)     ui->editSomRayon->setVisible(false);
        if (ui->editSomDiametre)  ui->editSomDiametre->setVisible(false);
        if (ui->lblSomProfondeur) ui->lblSomProfondeur->setVisible(true);
        if (ui->editSomProfondeur)ui->editSomProfondeur->setVisible(true);
        if (ui->lblSomPasse)      ui->lblSomPasse->setVisible(false);
        if (ui->editSomPasse)     ui->editSomPasse->setVisible(false);
        if (ui->lblSomSurepLat)   ui->lblSomSurepLat->setVisible(false);
        if (ui->editSomSurepLat)  ui->editSomSurepLat->setVisible(false);
        if (ui->lblSomSurepProf)  ui->lblSomSurepProf->setVisible(false);
        if (ui->editSomSurepProf) ui->editSomSurepProf->setVisible(false);
    } else {
        if (ui->widgetDimRect)    ui->widgetDimRect->setVisible(false);
        if (ui->widgetDimCirc)    ui->widgetDimCirc->setVisible(false);
        if (ui->lblSomLongueur)   ui->lblSomLongueur->setVisible(false);
        if (ui->lblSomLargeur)    ui->lblSomLargeur->setVisible(false);
        if (ui->lblSomRayon)      ui->lblSomRayon->setVisible(false);
        if (ui->editSomLongueur)  ui->editSomLongueur->setVisible(false);
        if (ui->editSomLargeur)   ui->editSomLargeur->setVisible(false);
        if (ui->editSomRayon)     ui->editSomRayon->setVisible(false);
        if (ui->editSomDiametre)  ui->editSomDiametre->setVisible(false);
        if (ui->lblSomProfondeur) ui->lblSomProfondeur->setVisible(false);
        if (ui->editSomProfondeur)ui->editSomProfondeur->setVisible(false);
        if (ui->lblSomPasse)      ui->lblSomPasse->setVisible(false);
        if (ui->editSomPasse)     ui->editSomPasse->setVisible(false);
        if (ui->lblSomSurepLat)   ui->lblSomSurepLat->setVisible(false);
        if (ui->editSomSurepLat)  ui->editSomSurepLat->setVisible(false);
        if (ui->lblSomSurepProf)  ui->lblSomSurepProf->setVisible(false);
        if (ui->editSomSurepProf) ui->editSomSurepProf->setVisible(false);
    }
}

void UnitEditor::setupTrouTab()
{
    m_trouTab = new QWidget;
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_trouTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *form = new QFormLayout(m_trouTab);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setSpacing(8);
    form->setContentsMargins(12, 12, 12, 12);

    auto secLabel = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#3a8f34;font-size:15px;font-weight:700;letter-spacing:2px;padding-top:4px;");
        return l;
    };
    auto sepLine = []() {
        auto *f = new QFrame; f->setFrameShape(QFrame::HLine);
        f->setStyleSheet("QFrame{background:#c8ccd8;}"); return f;
    };

    form->addRow(secLabel("GÉOMÉTRIE"));
    m_trouProf = new QLineEdit("20"); m_trouProf->setPlaceholderText("mm");
    m_trouTraversant = new QCheckBox("Traversant");
    form->addRow("Profondeur", m_trouProf);
    form->addRow("",           m_trouTraversant);

    // Profondeur synchronisée entre l'onglet Sommaire (programmation rapide)
    // et l'onglet PM Trou (réglages détaillés). On modifie l'un, l'autre suit.
    connect(m_trouProf, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_syncing) return; m_syncing = true;
        if (ui->editSomProfondeur) ui->editSomProfondeur->setText(v);
        m_syncing = false;
    });
    connect(ui->editSomProfondeur, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_syncing) return; m_syncing = true;
        if (m_trouProf) m_trouProf->setText(v);
        m_syncing = false;
    });

    form->addRow(sepLine());
    form->addRow(secLabel("PASSES"));

    // Profondeur de passe — OBLIGATOIRE. Increment de plongee.
    // 0 ou vide => cycle incomplet (verifie a la generation).
    m_trouProfPasse = new QLineEdit("");
    m_trouProfPasse->setPlaceholderText("mm — obligatoire");
    form->addRow("Profondeur de passe", m_trouProfPasse);

    // Reduction de passe — la passe diminue de cette valeur a
    // chaque increment. 0 = passes constantes.
    m_trouReduction = new QLineEdit("0");
    m_trouReduction->setPlaceholderText("mm — 0 = passes constantes");
    form->addRow("Réduction de passe", m_trouReduction);

    // Passe minimale — plancher quand la reduction est active.
    m_trouPasseMin = new QLineEdit("0");
    m_trouPasseMin->setPlaceholderText("mm");
    form->addRow("Passe minimale", m_trouPasseMin);

    form->addRow(sepLine());
    form->addRow(secLabel("BRISE-COPEAUX"));

    // Profondeur brise-copeaux — petit retrait casse-copeau tous
    // les X mm. 0 = pas de brise-copeaux.
    m_trouProfBrise = new QLineEdit("0");
    m_trouProfBrise->setPlaceholderText("mm — 0 = désactivé");
    form->addRow("Profondeur brise-copeaux", m_trouProfBrise);

    // Retrait brise-copeaux — valeur du petit recul. Defaut 0.2.
    m_trouRetraitBrise = new QLineEdit("0.2");
    m_trouRetraitBrise->setPlaceholderText("mm");
    form->addRow("Retrait brise-copeaux", m_trouRetraitBrise);

    form->addRow(sepLine());
    form->addRow(secLabel("DIVERS"));

    // Temporisation en fond de percage (secondes). 0 = pas de pause.
    m_trouTempoFond = new QLineEdit("0");
    m_trouTempoFond->setPlaceholderText("s — 0 = pas de pause");
    form->addRow("Tempo fond", m_trouTempoFond);

    form->addRow(sepLine());
    form->addRow(secLabel("SÉCURITÉS"));
    m_trouApproche = new QLineEdit("2"); m_trouApproche->setPlaceholderText("mm au-dessus du brut");
    m_trouSautBride = new QLineEdit("50"); m_trouSautBride->setPlaceholderText("mm");
    form->addRow("Approche",      m_trouApproche);
    form->addRow("Saut de bride", m_trouSautBride);

    m_trouZLiaison      = new QLineEdit("5");   m_trouZLiaison->setPlaceholderText("mm");
    m_trouAvanceLiaison = new QLineEdit("0");   m_trouAvanceLiaison->setPlaceholderText("mm/min (0=G0)");
    form->addRow("Z liaison",      m_trouZLiaison);
    form->addRow("Avance liaison", m_trouAvanceLiaison);

    m_trouChkOrigine = new QCheckBox("Définir");
    m_trouCbOrigine  = new QComboBox;
    m_trouCbOrigine->addItems({"G54","G55","G56","G57","G58","G59"});
    m_trouCbOrigine->setEnabled(false);
    connect(m_trouChkOrigine, &QCheckBox::toggled, m_trouCbOrigine, &QComboBox::setEnabled);
    auto *trouOriginWrap = new QWidget;
    auto *trouOriginLay = new QHBoxLayout(trouOriginWrap);
    trouOriginLay->setContentsMargins(0,0,0,0); trouOriginLay->setSpacing(8);
    trouOriginLay->addWidget(m_trouChkOrigine);
    trouOriginLay->addWidget(m_trouCbOrigine);
    trouOriginLay->addStretch();
    form->addRow("N° origine", trouOriginWrap);

    m_bgTrouApproche = installBadgeG(m_trouApproche,
        QStringLiteral("Z approche — hérité du BLK FORM"),
        [this]{ if (m_trouApproche) m_trouApproche->setText(QString::number(m_heritedApproche)); });
    m_bgTrouSautBride = installBadgeG(m_trouSautBride,
        QStringLiteral("Z saut de bride — hérité du BLK FORM"),
        [this]{ if (m_trouSautBride) m_trouSautBride->setText(QString::number(m_heritedSautBride)); });
    m_bgTrouZLiaison = installBadgeG(m_trouZLiaison,
        QStringLiteral("Z liaison — hérité du BLK FORM"),
        [this]{ if (m_trouZLiaison) m_trouZLiaison->setText(QString::number(m_heritedZLiaison)); });
    m_bgTrouAvanceLiaison = installBadgeG(m_trouAvanceLiaison,
        QStringLiteral("Avance liaison — héritée du BLK FORM"),
        [this]{ if (m_trouAvanceLiaison) m_trouAvanceLiaison->setText(QString::number(m_heritedAvanceLiaison)); });
    m_bgTrouOrigine = installBadgeG(trouOriginWrap,
        QStringLiteral("Numéro d'origine — hérité du BLK FORM"),
        [this]{
            if (m_trouChkOrigine) m_trouChkOrigine->setChecked(m_heritedOrigineDef);
            if (m_trouCbOrigine)  m_trouCbOrigine->setCurrentIndex(qBound(0, m_heritedOrigine, 5));
        });

    auto trouRedBadge = [this](BadgeG *b){ if (b) b->setHeritance(false); };
    connect(m_trouApproche,       &QLineEdit::textEdited, this, [this,trouRedBadge](const QString&){ trouRedBadge(m_bgTrouApproche); });
    connect(m_trouSautBride,      &QLineEdit::textEdited, this, [this,trouRedBadge](const QString&){ trouRedBadge(m_bgTrouSautBride); });
    connect(m_trouZLiaison,       &QLineEdit::textEdited, this, [this,trouRedBadge](const QString&){ trouRedBadge(m_bgTrouZLiaison); });
    connect(m_trouAvanceLiaison,  &QLineEdit::textEdited, this, [this,trouRedBadge](const QString&){ trouRedBadge(m_bgTrouAvanceLiaison); });
    connect(m_trouChkOrigine, &QCheckBox::toggled, this, [this,trouRedBadge](bool){ trouRedBadge(m_bgTrouOrigine); });
    connect(m_trouCbOrigine, qOverload<int>(&QComboBox::currentIndexChanged), this, [this,trouRedBadge](int){ trouRedBadge(m_bgTrouOrigine); });

    auto trouDc = [this](){ if (!m_syncing) emit dataChanged(); };
    connect(m_trouZLiaison,      &QLineEdit::textChanged, this, trouDc);
    connect(m_trouAvanceLiaison, &QLineEdit::textChanged, this, trouDc);
    connect(m_trouChkOrigine,    &QCheckBox::toggled,      this, [this,trouDc](bool){ trouDc(); });
    connect(m_trouCbOrigine,     qOverload<int>(&QComboBox::currentIndexChanged), this, [this,trouDc](int){ trouDc(); });

    int pmPocheIdx = -1;
    for (int i = 0; i < ui->tabWidget->count(); ++i)
        if (ui->tabWidget->widget(i) == ui->tabPMpoche)
            pmPocheIdx = i;
    int insertAt = (pmPocheIdx >= 0) ? pmPocheIdx + 1 : ui->tabWidget->count();
    m_trouTabIndex = ui->tabWidget->insertTab(insertAt, scroll, "PM Trou");
    m_trouTabW = scroll;
    ui->tabWidget->setTabVisible(m_trouTabIndex, false);
}

// ─────────────────────────────────────────────────────────────
//  setupContourTab — VERSION MODIFIÉE avec Plongée + Entrée + Sortie
// ─────────────────────────────────────────────────────────────
void UnitEditor::setupContourTab()
{
    m_contourTab = new QWidget;
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_contourTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *form = new QFormLayout(m_contourTab);
    m_cntForm = form;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setSpacing(8);
    form->setContentsMargins(12, 12, 12, 12);

    auto secLabel = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#3a8f34;font-size:15px;font-weight:700;"
                         "letter-spacing:2px;padding-top:4px;");
        return l;
    };
    auto sepLine = []() {
        auto *f = new QFrame; f->setFrameShape(QFrame::HLine);
        f->setStyleSheet("QFrame{background:#c8ccd8;}"); return f;
    };
    auto makeEdit = [](const QString &def, const QString &ph="") {
        auto *e = new QLineEdit(def);
        if (!ph.isEmpty()) e->setPlaceholderText(ph);
        return e;
    };

    // ── Mode d'usinage (Ebauche / Finition / Eb+Fin) ─────────
    //  Widget partage entre les 3 boutons radio. Determine quelles
    //  phases sont generees dans le NC (voir generateContour).
    m_modeUsinContour = new ModeUsinWidget(m_contourTab);
    form->addRow(m_modeUsinContour);
    connect(m_modeUsinContour, &ModeUsinWidget::modeChanged, this, [this](int m) {
        m_modeUsinContourValue = m;
        if (!m_syncing) emit dataChanged();
    });
    connect(m_modeUsinContour, &ModeUsinWidget::helpHover, this, &UnitEditor::helpRequested);

    // ── Géométrie ────────────────────────────────────────────
    form->addRow(secLabel("GÉOMÉTRIE"));
    m_cntProfondeur = makeEdit("10", "mm"); form->addRow("Profondeur Z",    m_cntProfondeur);
    m_cntPasse      = makeEdit("3",  "mm"); form->addRow("Prof/passe",       m_cntPasse);
    m_cntSurepLat   = makeEdit("0.2","mm"); form->addRow("Surcote latérale", m_cntSurepLat);
    m_cntSurepProf  = makeEdit("0.1","mm"); form->addRow("Surcote fond",     m_cntSurepProf);
    m_cntPasseFin   = makeEdit("0",  "mm"); form->addRow("Passe finition",   m_cntPasseFin);
    auto dcCnt = [this](const QString&){ if(!m_syncing) emit dataChanged(); };
    for(auto *e : {m_cntProfondeur,m_cntPasse,m_cntSurepLat,m_cntSurepProf,m_cntPasseFin})
        connect(e, &QLineEdit::textChanged, this, dcCnt);

    // ── Step-over % (recouvrement de l'ebauche adaptative) ───
    //  60% = passes Clipper espacees de 0.6 * diametre outil.
    //  Utilise uniquement en mode Ebauche ou Eb+Fin.
    m_cntStepOverPct = new QSpinBox;
    m_cntStepOverPct->setRange(10, 95);
    m_cntStepOverPct->setSuffix(" %");
    m_cntStepOverPct->setValue(60);
    m_cntStepOverPct->setToolTip("Recouvrement entre passes d'ebauche (% du diametre outil)");
    form->addRow("Step-over ebauche", m_cntStepOverPct);
    connect(m_cntStepOverPct, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int){ if(!m_syncing) emit dataChanged(); });

    // ── Mode fraisage uniquement (G40/G41/G42 supprimes) ──────
    //  La compensation est calculee par EazyNC (offset Clipper), pas par
    //  le controleur. Le NC est toujours en G40, donc plus de choix de
    //  correction a faire par l'utilisateur.
    form->addRow(sepLine());
    form->addRow(secLabel("FRAISAGE"));

    auto *frBox = new QWidget;
    auto *frLay = new QHBoxLayout(frBox);
    frLay->setContentsMargins(0,0,0,0);
    auto *rbAval = new QRadioButton("Avalant");
    auto *rbOppo = new QRadioButton("Opposition");
    rbAval->setChecked(true);
    rbAval->setObjectName("cnt_rbAval");
    rbOppo->setObjectName("cnt_rbOppo");
    frLay->addWidget(rbAval); frLay->addWidget(rbOppo);
    form->addRow("Fraisage", frBox);
    connect(rbAval, &QRadioButton::toggled, this, [this](bool){ if(!m_syncing) emit dataChanged(); });

    // Position de l'outil (comme R0 / RL / RR sur la TNC)
    m_cntCbPosition = new QComboBox;
    m_cntCbPosition->addItem("Exterieur (profil + ebauche brut)");
    m_cntCbPosition->addItem("R0  sur la ligne (gorge, rainure)");
    m_cntCbPosition->addItem("RL  a gauche du sens de parcours");
    m_cntCbPosition->addItem("RR  a droite du sens de parcours");
    form->addRow("Position outil", m_cntCbPosition);
    connect(m_cntCbPosition, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ if(!m_syncing) emit dataChanged(); });

    // ── Stratégie de finition (Contour complet / Coins uniquement) ──
    //  Coins = rest machining : ne nettoie QUE les rayons concaves que
    //  l'outil precedent (defini dans Outil ebauche) n'a pas pu faire.
    form->addRow(sepLine());
    form->addRow(secLabel("FINITION"));

    auto *finBox = new QWidget;
    auto *finLay = new QHBoxLayout(finBox);
    finLay->setContentsMargins(0,0,0,0);
    m_cntRbFinitContour = new QRadioButton("Contour complet");
    m_cntRbFinitCoins   = new QRadioButton("Coins uniquement");
    m_cntRbFinitContour->setChecked(true);
    m_cntRbFinitContour->setObjectName("cnt_rbFinitContour");
    m_cntRbFinitCoins->setObjectName("cnt_rbFinitCoins");
    finLay->addWidget(m_cntRbFinitContour);
    finLay->addWidget(m_cntRbFinitCoins);
    form->addRow("Stratégie", finBox);

    m_cntLblToolEbauche = new QLabel("Outil ébauche");
    m_cntToolRefEbauche = makeEdit("", "T1, T2…");
    m_cntToolRefEbauche->setToolTip(
        "Reference de l'outil d'ebauche precedent (ex: T1).\n"
        "Utilise par 'Coins uniquement' pour calculer la zone residuelle.");
    form->addRow(m_cntLblToolEbauche, m_cntToolRefEbauche);
    // Active/desactive le champ outil selon strategie
    auto updFinitUi = [this]() {
        bool coins = m_cntRbFinitCoins && m_cntRbFinitCoins->isChecked();
        if (m_cntLblToolEbauche) m_cntLblToolEbauche->setEnabled(coins);
        if (m_cntToolRefEbauche) m_cntToolRefEbauche->setEnabled(coins);
    };
    updFinitUi();
    connect(m_cntRbFinitContour, &QRadioButton::toggled,
            this, [this,updFinitUi](bool){ updFinitUi(); if(!m_syncing) emit dataChanged(); });
    connect(m_cntRbFinitCoins, &QRadioButton::toggled,
            this, [this,updFinitUi](bool){ updFinitUi(); if(!m_syncing) emit dataChanged(); });
    connect(m_cntToolRefEbauche, &QLineEdit::textChanged,
            this, [this](const QString&){ if(!m_syncing) emit dataChanged(); });

    // ╔══════════════════════════════════════════════════════════╗
    // ║  NOUVEAU : PLONGÉE Z                                     ║
    // ╚══════════════════════════════════════════════════════════╝
    form->addRow(sepLine());
    form->addRow(secLabel("PLONGÉE Z"));
    {
        auto *box = new QWidget;
        auto *lay = new QHBoxLayout(box);
        lay->setContentsMargins(0,0,0,0); lay->setSpacing(8);
        m_cntRbPlongeeG0 = new QRadioButton("Rapide  G0");
        m_cntRbPlongeeF  = new QRadioButton("Avance  F descente");
        m_cntRbPlongeeG0->setChecked(true);
        m_cntRbPlongeeG0->setToolTip("Descente rapide G0 (point d'approche hors matiere)");
        m_cntRbPlongeeF->setToolTip("Descente G1 a l'avance F descente");
        lay->addWidget(m_cntRbPlongeeG0);
        lay->addWidget(m_cntRbPlongeeF);
        lay->addStretch();
        form->addRow("Plongée", box);
        connect(m_cntRbPlongeeG0, &QRadioButton::toggled, this, [this](bool){ if(!m_syncing) emit dataChanged(); });
        connect(m_cntRbPlongeeF,  &QRadioButton::toggled, this, [this](bool){ if(!m_syncing) emit dataChanged(); });
    }

    // ╔══════════════════════════════════════════════════════════╗
    // ║  NOUVEAU : ENTRÉE MATIÈRE                                ║
    // ╚══════════════════════════════════════════════════════════╝
    form->addRow(sepLine());
    form->addRow(secLabel("ENTRÉE MATIÈRE"));
    {
        auto *box = new QWidget;
        auto *lay = new QHBoxLayout(box);
        lay->setContentsMargins(0,0,0,0); lay->setSpacing(4);
        m_cntRbEntreePerp  = new QRadioButton("Perpendiculaire");
        m_cntRbEntreeArc   = new QRadioButton("Arc tangent");
        m_cntRbEntreeLigne = new QRadioButton("Ligne tangente");
        m_cntRbEntreePerp->setChecked(true);
        m_cntRbEntreePerp->setToolTip("Approche perpendiculaire au premier segment");
        m_cntRbEntreeArc->setToolTip("Arc tangent — raccord doux pour finition");
        m_cntRbEntreeLigne->setToolTip("Ligne dans l'axe du contour");
        lay->addWidget(m_cntRbEntreePerp);
        lay->addWidget(m_cntRbEntreeArc);
        lay->addWidget(m_cntRbEntreeLigne);
        form->addRow("Type", box);
    }
    m_cntLblParamEntree = new QLabel("Longueur");
    m_cntParamEntree    = makeEdit("5", "mm");
    form->addRow(m_cntLblParamEntree, m_cntParamEntree);

    // ╔══════════════════════════════════════════════════════════╗
    // ║  NOUVEAU : SORTIE MATIÈRE                                ║
    // ╚══════════════════════════════════════════════════════════╝
    form->addRow(sepLine());
    form->addRow(secLabel("SORTIE MATIÈRE"));
    {
        auto *box = new QWidget;
        auto *lay = new QHBoxLayout(box);
        lay->setContentsMargins(0,0,0,0); lay->setSpacing(4);
        m_cntRbSortiePerp  = new QRadioButton("Perpendiculaire");
        m_cntRbSortieArc   = new QRadioButton("Arc tangent");
        m_cntRbSortieLigne = new QRadioButton("Ligne tangente");
        m_cntRbSortiePerp->setChecked(true);
        lay->addWidget(m_cntRbSortiePerp);
        lay->addWidget(m_cntRbSortieArc);
        lay->addWidget(m_cntRbSortieLigne);
        form->addRow("Type", box);
    }
    m_cntLblParamSortie = new QLabel("Longueur");
    m_cntParamSortie    = makeEdit("5", "mm");
    form->addRow(m_cntLblParamSortie, m_cntParamSortie);

    // Mise à jour dynamique des labels (Longueur / Rayon)
    auto updateLabels = [this]{
        if (m_cntLblParamEntree)
            m_cntLblParamEntree->setText(
                (m_cntRbEntreeArc && m_cntRbEntreeArc->isChecked()) ? "Rayon" : "Longueur");
        if (m_cntLblParamSortie)
            m_cntLblParamSortie->setText(
                (m_cntRbSortieArc && m_cntRbSortieArc->isChecked()) ? "Rayon" : "Longueur");
    };
    auto rbEntreeChanged = [updateLabels,this](bool){ updateLabels(); if(!m_syncing) emit dataChanged(); };
    auto rbSortieChanged = [updateLabels,this](bool){ updateLabels(); if(!m_syncing) emit dataChanged(); };
    connect(m_cntRbEntreePerp,  &QRadioButton::toggled, this, rbEntreeChanged);
    connect(m_cntRbEntreeArc,   &QRadioButton::toggled, this, rbEntreeChanged);
    connect(m_cntRbEntreeLigne, &QRadioButton::toggled, this, rbEntreeChanged);
    connect(m_cntRbSortiePerp,  &QRadioButton::toggled, this, rbSortieChanged);
    connect(m_cntRbSortieArc,   &QRadioButton::toggled, this, rbSortieChanged);
    connect(m_cntRbSortieLigne, &QRadioButton::toggled, this, rbSortieChanged);
    connect(m_cntParamEntree, &QLineEdit::textChanged, this, [this](const QString&){ if(!m_syncing) emit dataChanged(); });
    connect(m_cntParamSortie, &QLineEdit::textChanged, this, [this](const QString&){ if(!m_syncing) emit dataChanged(); });

    // ── Sécurités ────────────────────────────────────────────
    form->addRow(sepLine());
    form->addRow(secLabel("SÉCURITÉS"));
    m_cntApproche   = makeEdit("2",  "mm"); form->addRow("Approche",      m_cntApproche);
    m_cntSautBride  = makeEdit("50", "mm"); form->addRow("Saut de bride", m_cntSautBride);

    m_cntZLiaison      = new QLineEdit("5");   m_cntZLiaison->setPlaceholderText("mm");
    m_cntAvanceLiaison = new QLineEdit("0");   m_cntAvanceLiaison->setPlaceholderText("mm/min (0=G0)");
    form->addRow("Z liaison",      m_cntZLiaison);
    form->addRow("Avance liaison", m_cntAvanceLiaison);

    m_cntChkOrigine = new QCheckBox("Définir");
    m_cntCbOrigine  = new QComboBox;
    m_cntCbOrigine->addItems({"G54","G55","G56","G57","G58","G59"});
    m_cntCbOrigine->setEnabled(false);
    connect(m_cntChkOrigine, &QCheckBox::toggled, m_cntCbOrigine, &QComboBox::setEnabled);
    auto *cntOriginWrap = new QWidget;
    auto *cntOriginLay = new QHBoxLayout(cntOriginWrap);
    cntOriginLay->setContentsMargins(0,0,0,0); cntOriginLay->setSpacing(8);
    cntOriginLay->addWidget(m_cntChkOrigine);
    cntOriginLay->addWidget(m_cntCbOrigine);
    cntOriginLay->addStretch();
    form->addRow("N° origine", cntOriginWrap);

    m_bgCntApproche = installBadgeG(m_cntApproche,
        QStringLiteral("Z approche — hérité du BLK FORM"),
        [this]{ if (m_cntApproche) m_cntApproche->setText(QString::number(m_heritedApproche)); });
    m_bgCntSautBride = installBadgeG(m_cntSautBride,
        QStringLiteral("Z saut de bride — hérité du BLK FORM"),
        [this]{ if (m_cntSautBride) m_cntSautBride->setText(QString::number(m_heritedSautBride)); });
    m_bgCntZLiaison = installBadgeG(m_cntZLiaison,
        QStringLiteral("Z liaison — hérité du BLK FORM"),
        [this]{ if (m_cntZLiaison) m_cntZLiaison->setText(QString::number(m_heritedZLiaison)); });
    m_bgCntAvanceLiaison = installBadgeG(m_cntAvanceLiaison,
        QStringLiteral("Avance liaison — héritée du BLK FORM"),
        [this]{ if (m_cntAvanceLiaison) m_cntAvanceLiaison->setText(QString::number(m_heritedAvanceLiaison)); });
    m_bgCntOrigine = installBadgeG(cntOriginWrap,
        QStringLiteral("Numéro d'origine — hérité du BLK FORM"),
        [this]{
            if (m_cntChkOrigine) m_cntChkOrigine->setChecked(m_heritedOrigineDef);
            if (m_cntCbOrigine)  m_cntCbOrigine->setCurrentIndex(qBound(0, m_heritedOrigine, 5));
        });

    auto cntRedBadge = [this](BadgeG *b){ if (b) b->setHeritance(false); };
    connect(m_cntApproche,       &QLineEdit::textEdited, this, [this,cntRedBadge](const QString&){ cntRedBadge(m_bgCntApproche); });
    connect(m_cntSautBride,      &QLineEdit::textEdited, this, [this,cntRedBadge](const QString&){ cntRedBadge(m_bgCntSautBride); });
    connect(m_cntZLiaison,       &QLineEdit::textEdited, this, [this,cntRedBadge](const QString&){ cntRedBadge(m_bgCntZLiaison); });
    connect(m_cntAvanceLiaison,  &QLineEdit::textEdited, this, [this,cntRedBadge](const QString&){ cntRedBadge(m_bgCntAvanceLiaison); });
    connect(m_cntChkOrigine, &QCheckBox::toggled, this, [this,cntRedBadge](bool){ cntRedBadge(m_bgCntOrigine); });
    connect(m_cntCbOrigine, qOverload<int>(&QComboBox::currentIndexChanged), this, [this,cntRedBadge](int){ cntRedBadge(m_bgCntOrigine); });

    auto cntDc = [this](){ if (!m_syncing) emit dataChanged(); };
    connect(m_cntZLiaison,      &QLineEdit::textChanged, this, cntDc);
    connect(m_cntAvanceLiaison, &QLineEdit::textChanged, this, cntDc);
    connect(m_cntChkOrigine,    &QCheckBox::toggled,      this, [this,cntDc](bool){ cntDc(); });
    connect(m_cntCbOrigine,     qOverload<int>(&QComboBox::currentIndexChanged), this, [this,cntDc](int){ cntDc(); });

    // ── Fichier contour ───────────────────────────────────────
    form->addRow(sepLine());
    form->addRow(secLabel("CONTOUR"));

    m_cntNomFichier = new QLabel("— aucun contour —");
    m_cntNomFichier->setStyleSheet(
        "color:#6688cc;font-style:italic;font-size:11px;padding:4px 0;");
    form->addRow("Fichier", m_cntNomFichier);

    auto *btnRow = new QWidget;
    auto *btnLay = new QHBoxLayout(btnRow);
    btnLay->setContentsMargins(0,0,0,0); btnLay->setSpacing(6);

    auto makeBtn = [](const QString &label, const QString &icon) {
        auto *b = new QPushButton(label);
        b->setStyleSheet(
            "QPushButton{background:#E0E8FF;color:#0000FF;"
            "border:1px solid #2a3a6a;border-radius:4px;padding:5px 12px;"
            "font-size:11px;}"
            "QPushButton:hover{background:#1e3255;}");
        Q_UNUSED(icon);
        return b;
    };

    auto *btnDessiner = makeBtn("✏  Dessiner", "");
    auto *btnImporter = makeBtn("📂  DXF / STEP / IGES", "");
    btnDessiner->setObjectName("cnt_btnDessiner");
    btnImporter->setObjectName("cnt_btnImporter");
    auto *btnOuvrir = makeBtn("📁  Ouvrir un .cnt", "");
    btnOuvrir->setObjectName("cnt_btnOuvrir");
    btnLay->addWidget(btnOuvrir);
    btnLay->addWidget(btnDessiner);
    btnLay->addWidget(btnImporter);
    connect(btnOuvrir, &QPushButton::clicked, this, &UnitEditor::openPointFile);
    form->addRow("", btnRow);

    connect(btnDessiner, &QPushButton::clicked, this, [this]{
        emit openContourEditor(m_cntFilePath, m_cntName);
    });
    connect(btnImporter, &QPushButton::clicked, this, &UnitEditor::openDxfImport);

    for (auto *e : {m_cntProfondeur, m_cntPasse, m_cntSurepLat,
                    m_cntSurepProf, m_cntPasseFin,
                    m_cntApproche, m_cntSautBride})
        connect(e, &QLineEdit::textChanged, this, [this](const QString&){ emit dataChanged(); });

    // ╔══════════════════════════════════════════════════════════╗
    // ║  POCHE DE CONTOUR : ilots + plongee                       ║
    // ╚══════════════════════════════════════════════════════════╝
    {
        auto *sep = sepLine(); form->addRow(sep);
        auto *sec = secLabel("CONTOURS DE LA POCHE"); form->addRow(sec);
        m_pocTable = new QTableWidget(0, 4);
        m_pocTable->setHorizontalHeaderLabels({"Contour", "Type", "Z départ", "Profondeur"});
        m_pocTable->verticalHeader()->setDefaultSectionSize(32);
        m_pocTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int col = 1; col < 4; ++col)
            m_pocTable->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
        m_pocTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_pocTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_pocTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_pocTable->setMinimumHeight(200);
        m_pocTable->setStyleSheet("QTableWidget{background:#fff;border:1px solid #c8ccd8;border-radius:6px;font-size:12px;}"
                                  "QTableWidget::item:selected{background:#dbe7fb;color:#000;}"
                                  "QHeaderView::section{background:#eceae6;border:none;padding:4px;font-size:11px;}");
        form->addRow(m_pocTable);
        auto *hint = new QLabel("Poche : Z départ 0 = profondeur totale depuis le dessus,  Z départ < 0 = profondeur depuis ce Z.\n"
                                "Îlot : Z départ = dessus de l'îlot, profondeur = hauteur.   L'ordre compte (calques).");
        hint->setStyleSheet("color:#6a7090;font-size:11px;");
        form->addRow(hint);
        auto *bw = new QWidget; auto *bl = new QHBoxLayout(bw);
        bl->setContentsMargins(0,0,0,0); bl->setSpacing(6);
        auto *bAdd  = new QPushButton("+  Ajouter (.cnt)");
        auto *bDel  = new QPushButton("−  Retirer");
        auto *bUp   = new QPushButton("▲");
        auto *bDown = new QPushButton("▼");
        for (auto *b : {bAdd, bDel, bUp, bDown})
            b->setStyleSheet("QPushButton{background:#E0E8FF;color:#0000FF;border:1px solid #2a3a6a;"
                             "border-radius:4px;padding:5px 12px;font-size:11px;}");
        bl->addWidget(bAdd); bl->addWidget(bDel); bl->addWidget(bUp); bl->addWidget(bDown); bl->addStretch();
        form->addRow("", bw);
        connect(bAdd, &QPushButton::clicked, this, [this]{
            QString dir = m_cntFilePath.isEmpty() ? QString() : QFileInfo(m_cntFilePath).absolutePath();
            const QString f1 = EazyFileDialog::ouvrir(this, "Contour .cnt fermé à ajouter", dir, {"cnt"}, "CNT");
            const QStringList fs = f1.isEmpty() ? QStringList() : QStringList{f1};
            for (const QString &f : fs) {
                PocEntry e; e.file = f;
                const auto cur = pocEntries();
                e.ilot = !cur.isEmpty();                       // 1er = poche, suivants = îlots par défaut
                e.profondeur = cur.isEmpty() ? (m_cntProfondeur ? m_cntProfondeur->text().toDouble() : 10.0)
                                             : cur.first().profondeur;
                pocAddRow(e);
            }
            if (!fs.isEmpty() && !m_syncing) emit dataChanged();
        });
        connect(bDel, &QPushButton::clicked, this, [this]{
            const int r = m_pocTable->currentRow();
            if (r < 0) return;
            auto l = pocEntries(); l.removeAt(r); pocSetList(l);
            if (!m_syncing) emit dataChanged();
        });
        auto move = [this](int dir){
            const int r = m_pocTable->currentRow(), t = r + dir;
            auto l = pocEntries();
            if (r < 0 || t < 0 || t >= l.size()) return;
            std::swap(l[r], l[t]); pocSetList(l); m_pocTable->selectRow(t);
            if (!m_syncing) emit dataChanged();
        };
        connect(bUp,   &QPushButton::clicked, this, [move]{ move(-1); });
        connect(bDown, &QPushButton::clicked, this, [move]{ move(+1); });

        auto *sep2 = sepLine(); form->addRow(sep2);
        auto *sec2 = secLabel("PLONGÉE DANS LA MATIÈRE"); form->addRow(sec2);
        auto *pw = new QWidget; auto *pl = new QHBoxLayout(pw);
        pl->setContentsMargins(0,0,0,0); pl->setSpacing(8);
        m_pocRbDirecte = new QRadioButton("Directe");
        m_pocRbHelice  = new QRadioButton("Hélice");
        m_pocRbRampe   = new QRadioButton("Rampe");
        m_pocRbHelice->setChecked(true);
        m_pocRbDirecte->setToolTip("Plongée droite en avance de descente (fraise à coupe au centre ou avant-trou)");
        m_pocRbHelice->setToolTip("Descente en hélice, angle = angle de rampe de l'outil");
        m_pocRbRampe->setToolTip("Descente en rampe le long de la première passe");
        pl->addWidget(m_pocRbDirecte); pl->addWidget(m_pocRbHelice); pl->addWidget(m_pocRbRampe); pl->addStretch();
        form->addRow("Plongée", pw);

        m_pocChkEntree = new QCheckBox("Imposer le point (plongée directe)");
        form->addRow("Position d'entrée", m_pocChkEntree);
        auto *xw = new QWidget; auto *xl = new QHBoxLayout(xw);
        xl->setContentsMargins(0,0,0,0); xl->setSpacing(6);
        m_pocEntreeX = makeEdit("0", "X"); m_pocEntreeY = makeEdit("0", "Y");
        xl->addWidget(new QLabel("X")); xl->addWidget(m_pocEntreeX);
        xl->addWidget(new QLabel("Y")); xl->addWidget(m_pocEntreeY);
        form->addRow("Point X / Y", xw);
        auto updEntree = [this, xw]{
            const bool dir = m_pocRbDirecte->isChecked();
            m_pocChkEntree->setEnabled(dir);
            xw->setEnabled(dir && m_pocChkEntree->isChecked());
        };
        updEntree();
        for (auto *rb : {m_pocRbDirecte, m_pocRbHelice, m_pocRbRampe})
            connect(rb, &QRadioButton::toggled, this, [this, updEntree](bool){ updEntree(); if(!m_syncing) emit dataChanged(); });
        connect(m_pocChkEntree, &QCheckBox::toggled, this, [this, updEntree](bool){ updEntree(); if(!m_syncing) emit dataChanged(); });
        for (auto *e : {m_pocEntreeX, m_pocEntreeY})
            connect(e, &QLineEdit::textChanged, this, [this](const QString&){ if(!m_syncing) emit dataChanged(); });

        m_pocOnlyRows = {sep, sec, m_pocTable, hint, bw, sep2, sec2, pw, m_pocChkEntree, xw};

        // Lignes propres au contournage (masquees en mode poche)
        auto addSection = [&](const QString &title) {
            for (auto *l : m_contourTab->findChildren<QLabel*>())
                if (l->text() == title) {
                    m_cntOnlyRows << l;
                    int row = -1; QFormLayout::ItemRole role;
                    form->getWidgetPosition(l, &row, &role);
                    if (row > 0)
                        if (auto *it = form->itemAt(row - 1, QFormLayout::SpanningRole))
                            if (auto *w = it->widget()) if (qobject_cast<QFrame*>(w) && !qobject_cast<QLabel*>(w)) m_cntOnlyRows << w;
                }
        };
        addSection("FINITION"); addSection("PLONGÉE Z");
        addSection("ENTRÉE MATIÈRE"); addSection("SORTIE MATIÈRE");
        m_cntOnlyRows << m_cntProfondeur << m_cntNomFichier
                      << m_cntCbPosition << m_cntRbFinitContour->parentWidget()
                      << m_cntToolRefEbauche << m_cntRbPlongeeG0->parentWidget()
                      << m_cntRbEntreePerp->parentWidget() << m_cntParamEntree
                      << m_cntRbSortiePerp->parentWidget() << m_cntParamSortie;
        setPocheContourMode(false);
    }

    // Molette : jamais de changement de valeur par accident (voir eventFilter)
    for (QWidget *root : {static_cast<QWidget*>(this), static_cast<QWidget*>(m_contourTab)}) {
        for (auto *w : root->findChildren<QAbstractSpinBox*>()) { w->setFocusPolicy(Qt::StrongFocus); w->installEventFilter(this); }
        for (auto *w : root->findChildren<QComboBox*>())        { w->setFocusPolicy(Qt::StrongFocus); w->installEventFilter(this); }
    }

    int insertAt = (m_trouTabIndex >= 0) ? m_trouTabIndex + 1
                                         : ui->tabWidget->count();
    m_contourTabIndex = ui->tabWidget->insertTab(insertAt, scroll, "PM Contour");
    m_contourTabW = scroll;
    ui->tabWidget->setTabVisible(m_contourTabIndex, false);
}



void UnitEditor::setupPlongeeIcons()
{
    static const QString paths[3] = {
        ":/options/plongee_helicoidale.png",
        ":/options/plongee_rampe.png",
        ":/options/plongee_directe.png"
    };
    QWidget *widgets[3] = { ui->iconHelicoidale, ui->iconRampe, ui->iconDirecte };
    for (int i = 0; i < 3; ++i) {
        QWidget *ph = widgets[i];
        if (!ph) continue;
        delete ph->layout();
        auto *lay = new QHBoxLayout(ph);
        lay->setContentsMargins(2, 2, 2, 2);
        auto *lbl = new QLabel(ph);
        lbl->setAlignment(Qt::AlignCenter);
        QPixmap px(paths[i]);
        if (!px.isNull()) {
            int w = qMax(ph->maximumWidth()  - 4, 32);
            int h = qMax(ph->maximumHeight() - 4, 32);
            lbl->setPixmap(px.scaled(w, h, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation));
        }
        lay->addWidget(lbl);
    }
}

static void applyIconToLabel(QLabel *lbl, const QString &path, int w, int h)
{
    if (!lbl) return;
    QPixmap px(path);
    if (px.isNull()) return;
    lbl->setPixmap(px.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    lbl->setAlignment(Qt::AlignCenter);
}

void UnitEditor::setupPositionnementWidget()
{
    ui->editGlobPositionnement->setEnabled(false);
    connect(ui->radioFRapide, &QRadioButton::toggled, this, [this](bool checked) {
        m_posG0 = checked;
        ui->editGlobPositionnement->setEnabled(!checked);
        if (checked) ui->editGlobPositionnement->clear();
        if (m_skContext == SK_POSITIONNEMENT) onGlobPositFocused();
    });
    connect(ui->radioFValeur, &QRadioButton::toggled, this, [this](bool checked) {
        m_posG0 = !checked;
        ui->editGlobPositionnement->setEnabled(checked);
        if (checked) ui->editGlobPositionnement->setFocus();
        if (m_skContext == SK_POSITIONNEMENT) onGlobPositFocused();
    });
    connect(ui->radioFRapide, &QRadioButton::clicked, this, [this]() { onGlobPositFocused(); });
    connect(ui->radioFValeur, &QRadioButton::clicked, this, [this]() { onGlobPositFocused(); });
}

void UnitEditor::setupConnections()
{
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &UnitEditor::onTabChanged);

    connect(ui->editSomT,          &QLineEdit::textChanged, this, &UnitEditor::onSomTChanged);
    connect(ui->editSomS,          &QLineEdit::textChanged, this, &UnitEditor::onSomSChanged);
    connect(ui->editSomDescente,   &QLineEdit::textChanged, this, &UnitEditor::onSomDescenteChanged);
    connect(ui->editSomAvance,     &QLineEdit::textChanged, this, &UnitEditor::onSomAvanceChanged);
    connect(ui->editSomLongueur,   &QLineEdit::textChanged, this, &UnitEditor::onSomLongueurChanged);
    connect(ui->editSomLargeur,    &QLineEdit::textChanged, this, &UnitEditor::onSomLargeurChanged);
    connect(ui->editSomRayon,      &QLineEdit::textChanged, this, &UnitEditor::onSomRayonChanged);
    connect(ui->editSomDiametre,   &QLineEdit::textChanged, this, &UnitEditor::onSomDiametreChanged);
    connect(ui->editSomProfondeur, &QLineEdit::textChanged, this, &UnitEditor::onSomProfondeurChanged);
    connect(ui->editSomPasse,      &QLineEdit::textChanged, this, &UnitEditor::onSomPasseChanged);
    connect(ui->editSomSurepLat,   &QLineEdit::textChanged, this, &UnitEditor::onSomSurepLatChanged);
    connect(ui->editSomSurepProf,  &QLineEdit::textChanged, this, &UnitEditor::onSomSurepProfChanged);

    connect(ui->editToolT,         &QLineEdit::textChanged, this, &UnitEditor::onToolTChanged);
    connect(ui->editToolS,         &QLineEdit::textChanged, this, &UnitEditor::onToolSChanged);
    connect(ui->editToolDescente,  &QLineEdit::textChanged, this, &UnitEditor::onToolDescenteChanged);
    connect(ui->editToolAvance,    &QLineEdit::textChanged, this, &UnitEditor::onToolAvanceChanged);

    connect(ui->editPMLongueur,   &QLineEdit::textChanged, this, [this](){ onPMChanged(); });
    connect(ui->editPMLargeur,    &QLineEdit::textChanged, this, [this](){ onPMChanged(); });
    connect(ui->editPMRayon,      &QLineEdit::textChanged, this, [this](){ onPMChanged(); });
    connect(ui->editPMDiametre,   &QLineEdit::textChanged, this, [this](){ onPMChanged(); });
    connect(ui->editPMProfondeur, &QLineEdit::textChanged, this, [this](){ onPMChanged(); });
    connect(ui->editPMPasse,      &QLineEdit::textChanged, this, [this](){ onPMChanged(); });
    connect(ui->editPMSurepLat,   &QLineEdit::textChanged, this, [this](){ onPMChanged(); });
    connect(ui->editPMSurepProf,  &QLineEdit::textChanged, this, [this](){ onPMChanged(); });

    connect(ui->btnTableauCreer,   &QPushButton::clicked, this, &UnitEditor::onCreerTableau);
    connect(ui->btnTableauInserer, &QPushButton::clicked, this, &UnitEditor::onInsererLignes);
    connect(ui->btnTableauAjouter, &QPushButton::clicked, this, &UnitEditor::onAjouterLignes);

    connect(ui->radioHelicoidale, &QRadioButton::clicked, this, [this](){ m_modePlongee=0; onPlongeeFocused(); if(!m_syncing) emit dataChanged(); });
    connect(ui->radioRampe,       &QRadioButton::clicked, this, [this](){ m_modePlongee=1; onPlongeeFocused(); if(!m_syncing) emit dataChanged(); });
    connect(ui->radioDirecte,     &QRadioButton::clicked, this, [this](){ m_modePlongee=2; onPlongeeFocused(); if(!m_syncing) emit dataChanged(); });
    connect(ui->radioAvalant,     &QRadioButton::clicked, this, [this](){ m_modeFraisage=0; onFraisageFocused(); if(!m_syncing) emit dataChanged(); });
    connect(ui->radioOpposition,  &QRadioButton::clicked, this, [this](){ m_modeFraisage=1; onFraisageFocused(); if(!m_syncing) emit dataChanged(); });

    auto dc = [this](){ if (!m_syncing) emit dataChanged(); };
    connect(ui->editSomT,           &QLineEdit::textChanged, this, dc);
    connect(ui->editSomS,           &QLineEdit::textChanged, this, dc);
    connect(ui->editSomDescente,    &QLineEdit::textChanged, this, dc);
    connect(ui->editSomAvance,      &QLineEdit::textChanged, this, dc);
    connect(ui->editSomLongueur,    &QLineEdit::textChanged, this, dc);
    connect(ui->editSomLargeur,     &QLineEdit::textChanged, this, dc);
    connect(ui->editSomProfondeur,  &QLineEdit::textChanged, this, dc);
    connect(ui->editSomPasse,       &QLineEdit::textChanged, this, dc);
    connect(ui->editSomSurepLat,    &QLineEdit::textChanged, this, dc);
    connect(ui->editSomSurepProf,   &QLineEdit::textChanged, this, dc);
    connect(ui->editToolFM1,        &QLineEdit::textChanged, this, dc);
    connect(ui->editToolFM2,        &QLineEdit::textChanged, this, dc);
    connect(ui->editToolDL,         &QLineEdit::textChanged, this, dc);
    connect(ui->editToolDR,         &QLineEdit::textChanged, this, dc);
    connect(ui->editToolDR2,        &QLineEdit::textChanged, this, dc);
    connect(ui->editPMPasseFin,     &QLineEdit::textChanged, this, dc);
    connect(ui->editPMSurepFinLat,  &QLineEdit::textChanged, this, dc);
    connect(ui->editPMSurepFinProf, &QLineEdit::textChanged, this, dc);
    connect(ui->editPMAvanceFin,    &QLineEdit::textChanged, this, dc);
    connect(ui->editPMAngulaire,    &QLineEdit::textChanged, this, dc);
    connect(ui->editGlobApproche,   &QLineEdit::textChanged, this, dc);
    connect(ui->editGlobSautBride,  &QLineEdit::textChanged, this, dc);
    connect(ui->editGlobPositionnement, &QLineEdit::textChanged, this, dc);
    connect(ui->editSomModelePts,   &QLineEdit::textChanged, this, dc);
    connect(ui->radioM03, &QRadioButton::toggled, this, dc);
    connect(ui->radioM04, &QRadioButton::toggled, this, dc);
    connect(ui->radioFRapide, &QRadioButton::toggled, this, dc);
    connect(ui->radioFValeur, &QRadioButton::toggled, this, dc);

    installLeEventFilters();

    setupBlkFormHeritedFields();
}

void UnitEditor::setupBlkFormHeritedFields()
{
    auto *form = ui->editGlobApproche->parentWidget()->findChild<QFormLayout*>("formGlobales");
    if (!form) {
        if (auto *pl = qobject_cast<QFormLayout*>(ui->editGlobApproche->parentWidget()->layout()))
            form = pl;
    }

    m_editGlobZLiaison = new QLineEdit("5.0");
    m_editGlobZLiaison->setObjectName("editGlobZLiaison");

    m_editGlobAvanceLiaison = new QLineEdit("0");
    m_editGlobAvanceLiaison->setObjectName("editGlobAvanceLiaison");
    m_editGlobAvanceLiaison->setPlaceholderText("mm/min (0=G0)");

    m_chkGlobOrigine = new QCheckBox("Définir");
    m_chkGlobOrigine->setObjectName("chkGlobOrigine");
    m_cbGlobOrigine = new QComboBox;
    m_cbGlobOrigine->setObjectName("cbGlobOrigine");
    m_cbGlobOrigine->addItems({"G54","G55","G56","G57","G58","G59"});
    m_cbGlobOrigine->setEnabled(false);
    connect(m_chkGlobOrigine, &QCheckBox::toggled,
            m_cbGlobOrigine, &QComboBox::setEnabled);

    auto *origineWrap = new QWidget;
    auto *origineLay = new QHBoxLayout(origineWrap);
    origineLay->setContentsMargins(0,0,0,0); origineLay->setSpacing(8);
    origineLay->addWidget(m_chkGlobOrigine);
    origineLay->addWidget(m_cbGlobOrigine);
    origineLay->addStretch();

    if (form) {
        form->addRow(new QLabel("Z liaison"),        m_editGlobZLiaison);
        form->addRow(new QLabel("Avance liaison"),   m_editGlobAvanceLiaison);
        form->addRow(new QLabel("Numéro d'origine"), origineWrap);
    }

    m_bgApproche = installBadgeG(ui->editGlobApproche,
        QStringLiteral("Z approche — hérité du BLK FORM"),
        [this]{ ui->editGlobApproche->setText(QString::number(m_heritedApproche)); });
    m_bgSautBride = installBadgeG(ui->editGlobSautBride,
        QStringLiteral("Z saut de bride — hérité du BLK FORM"),
        [this]{ ui->editGlobSautBride->setText(QString::number(m_heritedSautBride)); });
    m_bgZLiaison = installBadgeG(m_editGlobZLiaison,
        QStringLiteral("Z liaison — hérité du BLK FORM"),
        [this]{ m_editGlobZLiaison->setText(QString::number(m_heritedZLiaison)); });
    m_bgAvanceLiaison = installBadgeG(m_editGlobAvanceLiaison,
        QStringLiteral("Avance de liaison — héritée du BLK FORM"),
        [this]{ m_editGlobAvanceLiaison->setText(QString::number(m_heritedAvanceLiaison)); });
    m_bgOrigine = installBadgeG(origineWrap,
        QStringLiteral("Numéro d'origine — hérité du BLK FORM"),
        [this]{
            m_chkGlobOrigine->setChecked(m_heritedOrigineDef);
            m_cbGlobOrigine->setCurrentIndex(qBound(0, m_heritedOrigine, 5));
        });

    connect(ui->editGlobApproche, &QLineEdit::textEdited, this, [this](const QString&){
        if (m_bgApproche) m_bgApproche->setHeritance(false);
    });
    connect(ui->editGlobSautBride, &QLineEdit::textEdited, this, [this](const QString&){
        if (m_bgSautBride) m_bgSautBride->setHeritance(false);
    });
    connect(m_editGlobZLiaison, &QLineEdit::textEdited, this, [this](const QString&){
        if (m_bgZLiaison) m_bgZLiaison->setHeritance(false);
    });
    connect(m_editGlobAvanceLiaison, &QLineEdit::textEdited, this, [this](const QString&){
        if (m_bgAvanceLiaison) m_bgAvanceLiaison->setHeritance(false);
    });
    connect(m_chkGlobOrigine, &QCheckBox::toggled, this, [this](bool){
        if (m_bgOrigine) m_bgOrigine->setHeritance(false);
    });
    connect(m_cbGlobOrigine, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){
        if (m_bgOrigine) m_bgOrigine->setHeritance(false);
    });

    auto dc = [this](){ if (!m_syncing) emit dataChanged(); };
    connect(m_editGlobZLiaison,      &QLineEdit::textChanged, this, dc);
    connect(m_editGlobAvanceLiaison, &QLineEdit::textChanged, this, dc);
    connect(m_chkGlobOrigine,        &QCheckBox::toggled,      this, [this, dc](bool){ dc(); });
    connect(m_cbGlobOrigine,         qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, dc](int){ dc(); });
}

void UnitEditor::installLeEventFilters()
{
    ui->editSomT->installEventFilter(this);
    ui->editToolT->installEventFilter(this);
    ui->editSomS->installEventFilter(this);
    ui->editToolS->installEventFilter(this);
    ui->editSomDescente->installEventFilter(this);
    ui->editSomPendul->installEventFilter(this);
    ui->editToolPendul->installEventFilter(this);
    ui->editToolDescente->installEventFilter(this);
    ui->editSomAvance->installEventFilter(this);
    ui->editToolAvance->installEventFilter(this);
    ui->editPMAvanceFin->installEventFilter(this);
    ui->editPMRayonES->installEventFilter(this);
    // Champs du contournage : aide animée au focus
    for (QWidget *w : std::initializer_list<QWidget*>{m_cntProfondeur, m_cntPasse, m_cntSurepLat,
                                                       m_cntSurepProf, m_cntPasseFin, m_cntStepOverPct})
        if (w) w->installEventFilter(this);
    ui->editGlobPositionnement->installEventFilter(this);
    ui->editSomModelePts->installEventFilter(this);
    ui->editPositionFichier->installEventFilter(this);

    ui->radioHelicoidale->installEventFilter(this);
    ui->radioRampe->installEventFilter(this);
    ui->radioDirecte->installEventFilter(this);
    ui->radioAvalant->installEventFilter(this);
    ui->radioOpposition->installEventFilter(this);
    ui->radioFRapide->installEventFilter(this);
    ui->radioFValeur->installEventFilter(this);

    const QList<QLineEdit*> numFields = {
        ui->editSomLongueur, ui->editSomLargeur, ui->editSomRayon,
        ui->editSomDiametre, ui->editSomProfondeur, ui->editSomPasse,
        ui->editSomSurepLat, ui->editSomSurepProf,
        ui->editPMLongueur,  ui->editPMLargeur,   ui->editPMRayon,
        ui->editPMDiametre,  ui->editPMProfondeur,ui->editPMPasse,
        ui->editPMSurepLat,  ui->editPMSurepProf,
        ui->editPMPasseFin,  ui->editPMAngulaire,
        ui->editPMSurepFinLat, ui->editPMSurepFinProf,
        ui->editGlobApproche,ui->editGlobSautBride,
        ui->editToolDL,      ui->editToolDR,      ui->editToolDR2,
        // Ligne / Grille / Cercle — jamais enregistrés jusqu'ici,
        // d'où l'obligation d'effacer avant de retaper.
        ui->editLigneP1X, ui->editLigneP1Y, ui->editLigneP2X, ui->editLigneP2Y,
        ui->editGrilleOX, ui->editGrilleOY, ui->editGrilleOZ,
        ui->editGrillePasX, ui->editGrillePasY,
        ui->editCercleCX, ui->editCercleCY, ui->editCercleZ,
        ui->editCercleRayon, ui->editCercleAngDep, ui->editCercleAngInc,
    };
    for (QLineEdit *le : numFields)
        le->installEventFilter(this);

    if (ui->spinGlobRecouvrement)
        connect(ui->spinGlobRecouvrement, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int){ emit helpRequested("recouvrement"); });
}

void UnitEditor::prevTab()
{
    int cur = ui->tabWidget->currentIndex();
    for (int i = cur - 1; i >= 0; --i) {
        if (ui->tabWidget->isTabVisible(i)) {
            ui->tabWidget->setCurrentIndex(i);
            return;
        }
    }
}

void UnitEditor::nextTab()
{
    int cur = ui->tabWidget->currentIndex();
    for (int i = cur + 1; i < ui->tabWidget->count(); ++i) {
        if (ui->tabWidget->isTabVisible(i)) {
            ui->tabWidget->setCurrentIndex(i);
            return;
        }
    }
}

void UnitEditor::focusNextField() { focusNextChild(); }
void UnitEditor::focusPrevField() { focusPreviousChild(); }

void UnitEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::NoModifier) {
        if (event->key() == Qt::Key_Left)  { prevTab();       return; }
        if (event->key() == Qt::Key_Right) { nextTab();       return; }
        if (event->key() == Qt::Key_Up)    { focusPrevField(); return; }
        if (event->key() == Qt::Key_Down)  { focusNextField(); return; }
    }
    QWidget::keyPressEvent(event);
}

bool UnitEditor::eventFilter(QObject *obj, QEvent *event)
{
    // ── Molette sur un champ numérique / une liste : on fait défiler
    //    la page au lieu de modifier la valeur (évite les erreurs de saisie)
    if (event->type() == QEvent::Wheel
        && (qobject_cast<QAbstractSpinBox*>(obj) || qobject_cast<QComboBox*>(obj))) {
        for (QWidget *p = static_cast<QWidget*>(obj)->parentWidget(); p; p = p->parentWidget())
            if (auto *sa = qobject_cast<QScrollArea*>(p)) {
                QCoreApplication::sendEvent(sa->verticalScrollBar(), event);
                break;
            }
        return true;
    }
    if (event->type() == QEvent::FocusIn) {
        // ── Sélection auto du contenu au focus (comportement global) ──
        //  Sans ça, l'opérateur doit effacer manuellement avant de
        //  retaper — la saisie doit directement écraser la valeur
        //  existante, comme sur un pupitre TNC. QTimer::singleShot(0,…)
        //  car selectAll() appelé pendant le FocusIn lui-même est parfois
        //  écrasé par le traitement interne du focus de Qt.
        if (auto *le = qobject_cast<QLineEdit*>(obj))
            QTimer::singleShot(0, le, [le]{ le->selectAll(); });

        // Vider le cadre AIDE : le champ qui prend le focus remet sa propre
        // aide juste après (sinon l'animation du champ précédent reste)
        emit helpRequested(QString());

        if (obj == ui->editSomT || obj == ui->editToolT) {
            onSomTFocused(); return false;
        }
        if (obj == ui->editSomS || obj == ui->editToolS) {
            if (m_blockMode != FraisageTrou) onSomSFocused(); return false;
        }
        if (obj == ui->editSomDescente || obj == ui->editToolDescente) {
            if (m_blockMode != FraisageTrou) onSomDescFocused();
            emit helpRequested("avance_plongee");
            return false;
        }
        if (obj == ui->editSomPendul || obj == ui->editToolPendul) {
            onSomPendulFocused();
            emit helpRequested("avance_rampe");
            return false;
        }
        if (obj == ui->editSomAvance || obj == ui->editToolAvance) {
            if (m_blockMode != FraisageTrou) onSomAvanceFocused();
            emit helpRequested("avance");
            return false;
        }
        if (obj == ui->editPMAvanceFin) {
            onPMAvanceFinFocused();
            emit helpRequested("avance");
            return false;
        }
        // Position angulaire : la rotation se fait autour du point coché
        if (obj == ui->editPMAngulaire) {
            emit helpRequested(QString("posang_%1").arg(m_posGroup ? m_posGroup->checkedId() : 5));
            return false;
        }
        // Contournage
        {
            const QString k = obj == m_cntProfondeur ? "cnt_profondeur"
                            : obj == m_cntPasse      ? "cnt_passe"
                            : obj == m_cntSurepLat   ? "cnt_surep_lat"
                            : obj == m_cntSurepProf  ? "surep_fond"
                            : obj == m_cntPasseFin   ? "cnt_passe_fin"
                            : obj == m_cntStepOverPct ? "recouvrement" : QString();
            if (!k.isEmpty()) { emit helpRequested(k); return false; }
        }
        if (obj == ui->editGlobPositionnement) {
            onGlobPositFocused(); return false;
        }
        if (obj == ui->editSomModelePts || obj == ui->editPositionFichier) {
            onModelePtsFocused(); return false;
        }
        if (obj == ui->radioHelicoidale ||
            obj == ui->radioRampe       ||
            obj == ui->radioDirecte) {
            onPlongeeFocused();   // émet aussi l'aide animée du mode choisi
            return false;
        }
        if (obj == ui->radioAvalant || obj == ui->radioOpposition) {
            onFraisageFocused(); return false;
        }
        if (obj == ui->radioFRapide || obj == ui->radioFValeur) {
            onGlobPositFocused(); return false;
        }
        static const QHash<QObject*, QString> helpMap = {
                                                          {ui->editSomLongueur,   "longueur_largeur"},
                                                          {ui->editSomLargeur,    "longueur_largeur"},
                                                          {ui->editPMLongueur,    "longueur_largeur"},
                                                          {ui->editPMLargeur,     "longueur_largeur"},
                                                          {ui->editSomDiametre,   "diametre"},
                                                          {ui->editPMDiametre,    "diametre"},
                                                          {ui->editSomProfondeur, "profondeur"},
                                                          {ui->editPMProfondeur,  "profondeur"},
                                                          {ui->editSomPasse,      "prof_passe"},
                                                          {ui->editPMPasse,       "prof_passe"},
                                                          {ui->editSomSurepLat,   "surep_lat"},
                                                          {ui->editPMSurepLat,    "surep_lat"},
                                                          {ui->editSomSurepProf,  "surep_fond"},
                                                          {ui->editPMSurepProf,   "surep_fond"},
                                                          {ui->editSomRayon,      "rayon_angle"},
                                                          {ui->editPMRayon,       "rayon_angle"},
                                                          {ui->editPMPasseFin,   "passe_fin"},
                                                          {ui->editPMSurepFinLat, "surep_fin"},
                                                          {ui->editPMSurepFinProf,"surep_fin"},
                                                          {ui->editGlobApproche,  "approche"},
                                                          {ui->editGlobSautBride, "approche"},
                                                          {ui->editPMRayonES,     "rayon_es"},
                                                          };
        if (helpMap.contains(obj)) {
            emit helpRequested(helpMap.value(obj));
        }
        emitDefaultContext();
        return false;
    }

    if (event->type() == QEvent::FocusOut) {
        QTimer::singleShot(100, this, [this]() {
            QWidget *fw = QApplication::focusWidget();
            if (!fw || !isAncestorOf(fw))
                emitDefaultContext();
        });
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(event);
        auto *le = qobject_cast<QLineEdit*>(obj);

        if (le && ke->modifiers() == Qt::NoModifier) {
            if (ke->key() == Qt::Key_Left &&
                le->cursorPosition() == 0 &&
                le->selectedText().isEmpty()) {
                prevTab(); return true;
            }
            if (ke->key() == Qt::Key_Right &&
                le->cursorPosition() == le->text().length() &&
                le->selectedText().isEmpty()) {
                nextTab(); return true;
            }
            if (ke->key() == Qt::Key_Up)   { focusPrevField(); return true; }
            if (ke->key() == Qt::Key_Down) { focusNextField(); return true; }

            if ((ke->key() == Qt::Key_Period || ke->key() == Qt::Key_Comma)) {
                QString sel = le->selectedText(), txt = le->text();
                if (txt.isEmpty() || sel == txt) {
                    le->setText("0."); le->setCursorPosition(2); return true;
                }
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

void UnitEditor::onSomTFocused()
{
    // Simplifié : plus de choix "Par nom / Par numéro" — le champ T
    // reste toujours numérique (indispensable pour le G-code). Le
    // nom s'affiche automatiquement à côté une fois l'outil sélectionné.
    setSkContext(SK_TOOL, {"Sélectionner\ndans tableau"}, -1, "");
}
void UnitEditor::onSomSFocused()
{
    static const QStringList labs = {"S  tr/min","Vc m/min"};
    setSkContext(SK_SPINDLE, labs, m_modeS,
                 ui->editSomS->text() + (m_modeS==0?" tr/min":" m/min"));
}
void UnitEditor::onSomDescFocused()
{
    static const QStringList labs  = {"F","Fu","Fz"};
    static const QStringList units = {"mm/min","mm/tr","mm/dent"};
    setSkContext(SK_DESCENTE, labs, m_modeDescente,
                 ui->editSomDescente->text()+" "+units[m_modeDescente]);
}
void UnitEditor::onSomAvanceFocused()
{
    static const QStringList labs  = {"F","Fu","Fz"};
    static const QStringList units = {"mm/min","mm/tr","mm/dent"};
    setSkContext(SK_AVANCE, labs, m_modeAvance,
                 ui->editSomAvance->text()+" "+units[m_modeAvance]);
}
void UnitEditor::onPMAvanceFinFocused()
{
    static const QStringList labs  = {"F","Fu","Fz"};
    static const QStringList units = {"mm/min","mm/tr","mm/dent"};
    setSkContext(SK_AVANCEFIN, labs, m_modeAvanceFin,
                 ui->editPMAvanceFin->text()+" "+units[m_modeAvanceFin]);
}
void UnitEditor::onGlobPositFocused()
{
    setSkContext(SK_POSITIONNEMENT, {"F rapide","F"}, m_posG0 ? 0 : 1,
                 m_posG0 ? "G0" : ui->editGlobPositionnement->text()+" mm/min");
}
void UnitEditor::onContourFileFocused()
{
    setSkContext(SK_CONTOUR_FILE,
                 {"Nouveau .cnt","Fichier .cnt","Éditer .cnt","DXF/STEP/IGES"},
                 -1, "");
}

void UnitEditor::onModelePtsFocused()
{
    if (m_blockMode == Contour) {
        onContourFileFocused();
        return;
    }
    int stackIdx = ui->stackPosition ? posWidgetToMode(ui->stackPosition->currentWidget()) : -1;
    QStringList ptLabels = {"Nouveau", "Fichier", "Éditer", "DXF",
                            "Point", "Ligne", "Grille", "Cercle", "Tableau"};
    if (m_blockMode == FraisageTrou) ptLabels.removeLast();  // fusionné avec "Point"
    setSkContext(SK_POINTS, ptLabels,
                 stackIdx + 4,
                 stackIdx >= 0 ? PointFile::modeName((PointFile::Mode)stackIdx) : "");
}

void UnitEditor::onPlongeeFocused()
{
    static const QStringList labs = {"Hélicoïdale","Rampe","Directe"};
    static const QStringList desc = {"spirale","zigzag","direct"};
    setSkContext(SK_PLONGEE, labs, m_modePlongee, desc[m_modePlongee]);
    emit helpRequested(QString("plongee_%1").arg(m_modePlongee));
}
void UnitEditor::onFraisageFocused()
{
    static const QStringList labs = {"En avalant","En opposition"};
    static const QStringList desc = {"avalant","opposition"};
    setSkContext(SK_FRAISAGE, labs, m_modeFraisage, desc[m_modeFraisage]);
    emit helpRequested(QString("fraisage_%1").arg(m_modeFraisage));
}
void UnitEditor::emitDefaultContext()  { m_skContext=SK_NONE; emit softkeyContextChanged(SK_NONE,{},-1,""); }

void UnitEditor::setupToolSelectButton() { }


void UnitEditor::onFocusLost() { emitDefaultContext(); }
void UnitEditor::onTabChanged(int index)
{
    if (!m_readOnly)
        emit saveRequested();

    if (ui->tabWidget->widget(index) == ui->tabPosition) {
        // Si un fichier de points est associé : mêmes softkeys que dans
        // le Sommaire (Nouveau/Fichier/Éditer/DXF + modes), pas juste
        // Point/Ligne/Grille/Cercle/Tableau — sinon ils étaient écrasés
        // dès l'arrivée sur cet onglet, avant même un clic dans le champ.
        const bool hasFile = ui->editPositionFichier &&
                             !ui->editPositionFichier->text().trimmed().isEmpty();
        if (hasFile) { onModelePtsFocused(); return; }
        int stackIdx = posWidgetToMode(ui->stackPosition->currentWidget());
        QStringList posLabels = {"Point", "Ligne", "Grille", "Cercle", "Tableau"};
        if (m_blockMode == FraisageTrou) posLabels.removeLast();
        setSkContext(SK_POSITION_MODE, posLabels,
                     (stackIdx >= 0 && stackIdx <= 4) ? stackIdx : -1,
                     "");
        return;
    }
    emitDefaultContext();
}

void UnitEditor::setSkContext(SoftkeyContext ctx, QStringList labels,
                              int activeIndex, const QString &activeValue)
{
    m_skContext = ctx;
    emit softkeyContextChanged(ctx, labels, activeIndex, activeValue);
}

// ─────────────────────────────────────────────────────────────────
//  posModeToWidget / posWidgetToMode — conversion ROBUSTE mode↔page.
//  On ne se fie JAMAIS à l'ordre physique d'insertion dans le
//  QStackedWidget (setCurrentIndex/currentIndex()) : si ce dernier
//  diffère un jour de l'ordre supposé (Point/Ligne/Grille/Cercle/
//  Tableau), TOUT le mapping softkey↔page se décale silencieusement
//  (symptôme observé : chaque bouton affichait la page suivante).
//  On mappe explicitement par POINTEUR de widget, jamais par index.
// ─────────────────────────────────────────────────────────────────
QWidget* UnitEditor::posModeToWidget(int mode) const
{
    // FraisageTrou : "Point" (et "Tableau" pour rétrocompat des .enc
    // existants) pointent vers la page dédiée multi-points — le mode
    // "Tableau" générique est fusionné avec "Point" pour ce cycle.
    if (m_blockMode == FraisageTrou && (mode == 0 || mode == 4) && m_ftPositionPage)
        return m_ftPositionPage;
    switch (mode) {
    case 0: return ui->pagePositionPoint;
    case 1: return ui->pagePositionLigne;
    case 2: return ui->pagePositionGrille;
    case 3: return ui->pagePositionCercle;
    case 4: return ui->pagePositionTableau;
    default: return ui->pagePositionPoint;
    }
}

int UnitEditor::posWidgetToMode(QWidget *w) const
{
    if (m_blockMode == FraisageTrou && w == m_ftPositionPage) return 0;
    if (w == ui->pagePositionPoint)   return 0;
    if (w == ui->pagePositionLigne)   return 1;
    if (w == ui->pagePositionGrille)  return 2;
    if (w == ui->pagePositionCercle)  return 3;
    if (w == ui->pagePositionTableau) return 4;
    return 0;
}

void UnitEditor::goToPositionMode(int stackIndex)
{
    // Choisir explicitement un mode manuel (Point/Ligne/Grille/Cercle/
    // Tableau) abandonne tout fichier de points associé — sinon le nom
    // du fichier restait affiché EN PLUS de la page manuelle qu'on
    // vient de choisir (les deux ne doivent jamais coexister).
    if (ui->editSomModelePts)    ui->editSomModelePts->clear();
    if (ui->editPositionFichier) { ui->editPositionFichier->clear(); ui->editPositionFichier->setVisible(false); }
    if (m_ftPositionPage)        m_ftPositionPage->setVisible(true);

    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        if (ui->tabWidget->widget(i) == ui->tabPosition) {
            ui->tabWidget->setCurrentIndex(i);
            break;
        }
    }
    ui->stackPosition->setCurrentWidget(posModeToWidget(stackIndex));
    updateSommairePosition();

    int activeIdx = stackIndex;
    QStringList posLabels2 = {"Point", "Ligne", "Grille", "Cercle", "Tableau"};
    if (m_blockMode == FraisageTrou) posLabels2.removeLast();
    setSkContext(SK_POSITION_MODE, posLabels2,
                 (activeIdx >= 0 && activeIdx <= 3) ? activeIdx : -1,
                 "");
}

void UnitEditor::updateFtPointsPreview()
{
    if (!m_ftPointsPreview || m_blockMode != FraisageTrou) return;
    // Réutilise EXACTEMENT le calcul du générateur — aucune divergence
    // possible entre l'aperçu affiché et le G-code réellement produit.
    FraisageTrouData d = saveFraisageTrou();
    m_ftPointsPreview->setPoints(FraisageTrouGenerator::computePositions(d));
}

void UnitEditor::updateSommairePosition()
{
    if (!ui->lblSomModelePts) return;
    if (m_blockMode == Contour) {          // pas de positions pour un contour
        ui->lblSomModelePts->setText("Contour (cliquer pour choisir)");
        return;
    }
    int stackIdx = ui->stackPosition ? posWidgetToMode(ui->stackPosition->currentWidget()) : 0;
    const bool hasPtFile = ui->editSomModelePts && !ui->editSomModelePts->text().trimmed().isEmpty();

    if (ui->lblPositionModeActif) {
        using PF = PointFile;
        static const PF::Mode modes[] = {
            PF::ModePoint, PF::ModeLigne, PF::ModeGrille, PF::ModeCercle, PF::ModeTableau
        };
        if (stackIdx >= 0 && stackIdx < 5) {
            PF::Mode m = modes[stackIdx];
            // Icône dédiée "fichier de points" (même logo que l'explorateur
            // de fichiers) quand un .pnt est associé — sinon l'icône du mode
            // brut (Point/Ligne/...) laisse croire à tort que le motif vient
            // des champs directs plutôt que du fichier.
            QPixmap px(hasPtFile ? ":/unit/logo fichier de point.bmp" : PF::modeIcon(m));
            if (!px.isNull()) {
                QPixmap scaled = px.scaled(96, 59, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                ui->lblPositionModeActif->setPixmap(scaled);
                ui->lblPositionModeActif->setAlignment(Qt::AlignCenter);
                // Rafraîchissement immédiat — sans ça l'icône n'apparaît
                // qu'au prochain repaint naturel (ex: après un clic
                // ailleurs), pas tout de suite au changement de mode.
                ui->lblPositionModeActif->repaint();
                // Icône dupliquée dans le Sommaire (FraisageTrou)
                if (m_somFtModeIcon) {
                    m_somFtModeIcon->setPixmap(scaled);
                    m_somFtModeIcon->setAlignment(Qt::AlignCenter);
                    m_somFtModeIcon->repaint();
                }
            } else {
                QString txt = hasPtFile ? "Fichier" : PF::modeName(m);
                ui->lblPositionModeActif->setText(txt);
                ui->lblPositionModeActif->repaint();
                if (m_somFtModeIcon) {
                    m_somFtModeIcon->setText(txt);
                    m_somFtModeIcon->repaint();
                }
            }
        }
    }

    // Point 1..4 (Sommaire) : visibles UNIQUEMENT en mode "Point" ET
    // SANS fichier associé (stackIdx==0). Pour Ligne/Grille/Cercle/
    // Tableau, ils n'ont pas de sens (paramètres dans l'onglet Position)
    // — seule l'icône du mode reste affichée dans le Sommaire. Idem
    // quand un fichier .pnt est associé : la page affichée dans
    // stackPosition est alors le message "Positions gérées par le
    // fichier...", non reconnue par posWidgetToMode() qui retombe par
    // défaut sur 0 (Point) — d'où le hasPtFile explicite ci-dessous,
    // sinon le tableau restait affiché à tort en plus du fichier.
    if (m_blockMode == FraisageTrou) {
        bool isPointMode = (stackIdx == 0) && !hasPtFile;
        for (int i = 0; i < kFtSomPts; ++i) {
            if (m_somFtPX[i]) {
                m_somFtPX[i]->parentWidget()->setVisible(isPointMode);
                if (m_somFtLbl[i]) m_somFtLbl[i]->setVisible(isPointMode);
            }
        }
    }

    QString fichier = ui->editSomModelePts ? ui->editSomModelePts->text().trimmed() : "";
    if (!fichier.isEmpty()) {
        ui->lblSomModelePts->setText("Fichier de points : " + QFileInfo(fichier).fileName());
        return;
    }
    QString modeStr;
    switch (stackIdx) {
    case 0: modeStr = QString("Point  X%1 Y%2")
        .arg(ui->editPosX ? ui->editPosX->text() : "0")
            .arg(ui->editPosY ? ui->editPosY->text() : "0"); break;
    case 1: modeStr = QString("Ligne  %1 pts")
                      .arg(ui->spinLigneNbPts ? ui->spinLigneNbPts->value() : 1); break;
    case 2: modeStr = QString("Grille  %1x%2")
                      .arg(ui->spinGrilleNbX ? ui->spinGrilleNbX->value() : 1)
                      .arg(ui->spinGrilleNbY ? ui->spinGrilleNbY->value() : 1); break;
    case 3: modeStr = QString("Cercle  %1 pts")
                      .arg(ui->spinCercleNbPts ? ui->spinCercleNbPts->value() : 4); break;
    case 4: { int n = ui->tablePoints ? ui->tablePoints->rowCount() : 10;
        modeStr = QString("Tableau  %1 pts").arg(n); break; }
    default: modeStr = "—"; break;
    }
    ui->lblSomModelePts->setText("Modèle de points : " + modeStr);
}

void UnitEditor::onSoftkeyPressed(int index)
{
    switch (m_skContext) {
    case SK_TOOL:
        emit openToolTable(false);  // toujours par numéro maintenant
        break;
    case SK_SPINDLE:   m_modeS=index; updateBadgeS(); onSomSFocused(); break;
    case SK_DESCENTE:  m_modeDescente=index; updateBadgeDescente(); onSomDescFocused(); break;
    case SK_PENDUL:    m_modePendul=index;   updateBadgePendul();   onSomPendulFocused(); break;
    case SK_AVANCE:    m_modeAvance=index; updateBadgeAvance(); onSomAvanceFocused(); break;
    case SK_AVANCEFIN: m_modeAvanceFin=index; onPMAvanceFinFocused(); break;
    case SK_PLONGEE:
        m_modePlongee=index;
        if(index==0) ui->radioHelicoidale->setChecked(true);
        else if(index==1) ui->radioRampe->setChecked(true);
        else ui->radioDirecte->setChecked(true);
        onPlongeeFocused(); break;
    case SK_FRAISAGE:
        m_modeFraisage=index;
        if(index==0) ui->radioAvalant->setChecked(true);
        else ui->radioOpposition->setChecked(true);
        onFraisageFocused(); break;
    case SK_POSITIONNEMENT:
        m_posG0=(index==0);
        if(index==0){ ui->radioFRapide->setChecked(true); ui->editGlobPositionnement->setEnabled(false); ui->editGlobPositionnement->clear(); }
        else { ui->radioFValeur->setChecked(true); ui->editGlobPositionnement->setEnabled(true); ui->editGlobPositionnement->setFocus(); }
        onGlobPositFocused(); break;
    case SK_POINTS:
        switch(index){
        case 0: emit createNewPointFile(); break;
        case 1: emit openPointFile(); break;
        case 2: emit editPointFile(ui->editSomModelePts->text()); break;
        case 3: emit openDxfImport(); break;
        case 4: goToPositionMode(0); break;
        case 5: goToPositionMode(1); break;
        case 6: goToPositionMode(2); break;
        case 7: goToPositionMode(3); break;
        case 8: goToPositionMode(4); break;
        } break;
    case SK_POSITION_MODE:
        // BUG CORRIGÉ : "index + 1" décalait tout d'un cran vers la
        // droite (Grille cliqué → Cercle activé). Les labels de ce
        // contexte sont {Point,Ligne,Grille,Cercle,Tableau} = index
        // 0..4 directement, sans offset.
        goToPositionMode(index);
        break;
    case SK_CONTOUR_FILE:
        switch(index){
        case 0:
            m_cntFilePath.clear();
            m_cntName.clear();
            if (m_cntNomFichier) m_cntNomFichier->setText("— nouveau contour —");
            emit openContourEditor("", "");
            break;
        case 1:
            emit openPointFile();
            break;
        case 2:
            emit openContourEditor(m_cntFilePath, m_cntName);
            break;
        case 3:
            emit openDxfImport();
            break;
        }
        break;
    default: break;
    }
}

void UnitEditor::updateBadgeS()
{
    static const QStringList labs  = {"S","Vc"};
    static const QStringList units = {"tr/min","m/min"};
    ui->lblSomSMode->setText(labs[m_modeS]);
    ui->lblSomSMode->setProperty("badge", m_modeS==1);
    ui->lblSomSUnit->setText(units[m_modeS]);
    ui->lblToolS->setText(m_modeS==0 ? "Vitesse broche S (tr/min)" : "Vitesse de coupe Vc (m/min)");
    style()->unpolish(ui->lblSomSMode); style()->polish(ui->lblSomSMode);
}
void UnitEditor::updateBadgeDescente()
{
    static const QStringList labs  = {"F","Fu","Fz"};
    static const QStringList units = {"mm/min","mm/tr","mm/dent"};
    ui->lblSomDescenteMode->setText(labs[m_modeDescente]);
    ui->lblSomDescenteUnit->setText(units[m_modeDescente]);
    ui->lblToolDescente->setText(QString("Avance plongée (%1)").arg(units[m_modeDescente]));
    style()->unpolish(ui->lblSomDescenteMode); style()->polish(ui->lblSomDescenteMode);
}
// ─────────────────────────────────────────────────────────────────────
//  setPendulVisible — affiche/masque l'avance pendulaire dans les deux
//  onglets concernés. Appelée par setBlockMode : masquée en mode Trou
//  (la plongée pendulaire n'a pas de sens en perçage), visible sinon.
// ─────────────────────────────────────────────────────────────────────
void UnitEditor::setPendulVisible(bool visible)
{
    // Onglet Sommaire : le pendulaire est dans le QFrame "framePendul"
    // (icône + champ + unité + label F) → on masque le frame entier.
    if (ui->framePendul)
        ui->framePendul->setVisible(visible);

    // Onglet Outil : QFormLayout "formTool". setRowVisible masque label
    // ET champ de la ligne d'un seul coup (Qt 6.4+).
    if (ui->formTool && ui->editToolPendul)
        ui->formTool->setRowVisible(ui->editToolPendul, visible);
}

void UnitEditor::updateBadgePendul()
{
    static const QStringList labs  = {"F","Fu","Fz"};
    static const QStringList units = {"mm/min","mm/tr","mm/dent"};
    ui->lblSomPendulMode->setText(labs[m_modePendul]);
    ui->lblSomPendulUnit->setText(units[m_modePendul]);
    ui->lblToolPendul->setText(QString("Avance rampe (%1)").arg(units[m_modePendul]));
    style()->unpolish(ui->lblSomPendulMode); style()->polish(ui->lblSomPendulMode);
}
void UnitEditor::updateBadgeAvance()
{
    static const QStringList labs  = {"F","Fu","Fz"};
    static const QStringList units = {"mm/min","mm/tr","mm/dent"};
    ui->lblSomAvanceMode->setText(labs[m_modeAvance]);
    ui->lblSomAvanceUnit->setText(units[m_modeAvance]);
    ui->lblToolAvance->setText(QString("Avance travail (%1)").arg(units[m_modeAvance]));
    style()->unpolish(ui->lblSomAvanceMode); style()->polish(ui->lblSomAvanceMode);
}
void UnitEditor::updateBadgeAvanceFin() {}

void UnitEditor::setupPositionPocheGroup()
{
    m_posGroup = new QButtonGroup(this);
    m_posGroup->addButton(ui->btnPos1,1); m_posGroup->addButton(ui->btnPos2,2);
    m_posGroup->addButton(ui->btnPos3,3); m_posGroup->addButton(ui->btnPos4,4);
    m_posGroup->addButton(ui->btnPos5,5); m_posGroup->addButton(ui->btnPos6,6);
    m_posGroup->addButton(ui->btnPos7,7); m_posGroup->addButton(ui->btnPos8,8);
    m_posGroup->addButton(ui->btnPos9,9);
    m_posGroup->setExclusive(true);
    connect(m_posGroup, &QButtonGroup::idClicked, this, &UnitEditor::onPositionPocheClicked);

    // Choix réduits : les 4 coins et le centre (7 9 / 5 / 1 3).
    // Les milieux de côtés (2 4 6 8) restent lus dans les anciens
    // fichiers mais ne sont plus proposés.
    for (QAbstractButton *b : {ui->btnPos2, ui->btnPos4, ui->btnPos6, ui->btnPos8})
        if (b) b->setVisible(false);

    // La position angulaire vient APRÈS le point de la poche : on la
    // sort du formulaire commun et on la place sous le pavé.
    if (ui->formPMCommun && ui->editPMAngulaire && ui->layoutPMpoche && ui->widgetPositionPoche) {
        int r = -1; QFormLayout::ItemRole ro;
        ui->formPMCommun->getWidgetPosition(ui->editPMAngulaire, &r, &ro);
        if (r >= 0) {
            QFormLayout::TakeRowResult t = ui->formPMCommun->takeRow(r);
            delete t.labelItem; delete t.fieldItem;
            auto *w = new QWidget;
            auto *fl = new QFormLayout(w);
            fl->setContentsMargins(0, 4, 0, 0);
            ui->lblPMAngulaire->setText("Position angulaire (°)");
            ui->editPMAngulaire->setPlaceholderText("0 = pas de rotation");
            fl->addRow(ui->lblPMAngulaire, ui->editPMAngulaire);
            const int idx = ui->layoutPMpoche->indexOf(ui->widgetPositionPoche);
            ui->layoutPMpoche->insertWidget(idx + 1, w);
        }
    }
}
void UnitEditor::onTypeRectToggled(bool /*checked*/) { }
void UnitEditor::updatePocheTypeVisibility(bool isCirc)
{
    ui->widgetDimRect->setVisible(!isCirc); ui->widgetDimCirc->setVisible(isCirc);
    ui->widgetPMRect->setVisible(!isCirc);  ui->widgetPMCirc->setVisible(isCirc);
}
void UnitEditor::onPositionPocheClicked(int id)
{
    // Aide : montre le point de référence coché sur la poche
    emit helpRequested(QString("posref_%1").arg(id));
}

void UnitEditor::onSomTChanged(const QString &v)        { if(m_syncing)return; m_syncing=true; ui->editToolT->setText(v); m_syncing=false; }
void UnitEditor::onSomSChanged(const QString &v)        { if(m_syncing)return; m_syncing=true; ui->editToolS->setText(v); m_syncing=false; }
void UnitEditor::onSomDescenteChanged(const QString &v) { if(m_syncing)return; m_syncing=true; ui->editToolDescente->setText(v); m_syncing=false; }
void UnitEditor::onSomPendulChanged(const QString &v)    { if(m_syncing)return; m_syncing=true; ui->editToolPendul->setText(v);    m_syncing=false; }
void UnitEditor::onSomPendulFocused()
{
    static const QStringList labs  = {"F","Fu","Fz"};
    static const QStringList units = {"mm/min","mm/tr","mm/dent"};
    setSkContext(SK_PENDUL, labs, m_modePendul,
                 ui->editSomPendul->text()+" "+units[m_modePendul]);
}
void UnitEditor::onSomAvanceChanged(const QString &v)   { if(m_syncing)return; m_syncing=true; ui->editToolAvance->setText(v); m_syncing=false; }
void UnitEditor::onSomLongueurChanged(const QString &v) { if(m_syncing)return; m_syncing=true; ui->editPMLongueur->setText(v); m_syncing=false; }
void UnitEditor::onSomLargeurChanged(const QString &v)  { if(m_syncing)return; m_syncing=true; ui->editPMLargeur->setText(v); m_syncing=false; }
void UnitEditor::onSomRayonChanged(const QString &v)    { if(m_syncing)return; m_syncing=true; ui->editPMRayon->setText(v); m_syncing=false; }
void UnitEditor::onSomDiametreChanged(const QString &v) { if(m_syncing)return; m_syncing=true; ui->editPMDiametre->setText(v); m_syncing=false; }
void UnitEditor::onSomProfondeurChanged(const QString &v){if(m_syncing)return; m_syncing=true; ui->editPMProfondeur->setText(v); m_syncing=false; }
void UnitEditor::onSomPasseChanged(const QString &v)    { if(m_syncing)return; m_syncing=true; ui->editPMPasse->setText(v); m_syncing=false; }
void UnitEditor::onSomSurepLatChanged(const QString &v) { if(m_syncing)return; m_syncing=true; ui->editPMSurepLat->setText(v); m_syncing=false; }
void UnitEditor::onSomSurepProfChanged(const QString &v){ if(m_syncing)return; m_syncing=true; ui->editPMSurepProf->setText(v); m_syncing=false; }
void UnitEditor::onToolTChanged(const QString &v)        { if(m_syncing)return; m_syncing=true; ui->editSomT->setText(v); m_syncing=false; }
void UnitEditor::onToolSChanged(const QString &v)        { if(m_syncing)return; m_syncing=true; ui->editSomS->setText(v); m_syncing=false; }
void UnitEditor::onToolDescenteChanged(const QString &v) { if(m_syncing)return; m_syncing=true; ui->editSomDescente->setText(v); m_syncing=false; }
void UnitEditor::onToolAvanceChanged(const QString &v)   { if(m_syncing)return; m_syncing=true; ui->editSomAvance->setText(v); m_syncing=false; }
void UnitEditor::onPMChanged()
{
    if (m_syncing) return; m_syncing = true;
    ui->editSomLongueur->setText(ui->editPMLongueur->text());
    ui->editSomLargeur->setText(ui->editPMLargeur->text());
    ui->editSomRayon->setText(ui->editPMRayon->text());
    ui->editSomDiametre->setText(ui->editPMDiametre->text());
    ui->editSomProfondeur->setText(ui->editPMProfondeur->text());
    ui->editSomPasse->setText(ui->editPMPasse->text());
    ui->editSomSurepLat->setText(ui->editPMSurepLat->text());
    ui->editSomSurepProf->setText(ui->editPMSurepProf->text());
    m_syncing = false;
}

void UnitEditor::onCreerTableau()
{
    int nb = ui->spinTableauNbPts->value();
    ui->tablePoints->blockSignals(true);
    ui->tablePoints->setRowCount(0);
    for (int i = 0; i < nb; ++i) {
        int r = ui->tablePoints->rowCount();
        ui->tablePoints->insertRow(r);
        ui->tablePoints->setItem(r,0,new QTableWidgetItem(QString::number(i+1)));
        ui->tablePoints->setItem(r,1,new QTableWidgetItem("0"));
        ui->tablePoints->setItem(r,2,new QTableWidgetItem("0"));
        ui->tablePoints->setItem(r,3,new QTableWidgetItem("0"));
    }
    ui->tablePoints->blockSignals(false);
    ui->stackPosition->setCurrentWidget(posModeToWidget(4));
}
void UnitEditor::onInsererLignes()
{
    int r = qMax(0, ui->tablePoints->currentRow());
    int nb = ui->spinTableauNbPts->value();
    ui->tablePoints->blockSignals(true);
    for(int i=0;i<nb;++i){
        ui->tablePoints->insertRow(r);
        ui->tablePoints->setItem(r,0,new QTableWidgetItem("?"));
        ui->tablePoints->setItem(r,1,new QTableWidgetItem("0"));
        ui->tablePoints->setItem(r,2,new QTableWidgetItem("0"));
        ui->tablePoints->setItem(r,3,new QTableWidgetItem("0"));
    }
    for(int row=0;row<ui->tablePoints->rowCount();++row) ui->tablePoints->item(row,0)->setText(QString::number(row+1));
    ui->tablePoints->blockSignals(false);
}
void UnitEditor::onAjouterLignes()
{
    int nb = ui->spinTableauNbPts->value();
    ui->tablePoints->blockSignals(true);
    for(int i=0;i<nb;++i){
        int r=ui->tablePoints->rowCount();
        ui->tablePoints->insertRow(r);
        ui->tablePoints->setItem(r,0,new QTableWidgetItem(QString::number(r+1)));
        ui->tablePoints->setItem(r,1,new QTableWidgetItem("0"));
        ui->tablePoints->setItem(r,2,new QTableWidgetItem("0"));
        ui->tablePoints->setItem(r,3,new QTableWidgetItem("0"));
    }
    ui->tablePoints->blockSignals(false);
}

void UnitEditor::load(const PocheData &d)
{
    if (m_blockMode != PocheRect && m_blockMode != PocheCirc)
        setBlockMode(PocheRect);
    m_syncing = true;

    m_modeUsinValue = d.modeUsin;
    if (m_modeUsin)
        m_modeUsin->setMode(d.modeUsin);

    ui->editSomT->setText(d.toolRef); ui->editToolT->setText(d.toolRef);
    m_toolByName = d.toolByName;
    ui->editSomS->setText(QString::number(d.spindleVal)); ui->editToolS->setText(QString::number(d.spindleVal));
    m_modeS = d.spindleByVc ? 1 : 0;
    ui->editSomDescente->setText(QString::number(d.descenteVal)); ui->editToolDescente->setText(QString::number(d.descenteVal));
    m_modeDescente = d.descenteMode;
    ui->editSomPendul->setText(QString::number(d.pendulVal)); ui->editToolPendul->setText(QString::number(d.pendulVal));
    m_modePendul = d.pendulMode;
    ui->editSomAvance->setText(QString::number(d.avanceVal)); ui->editToolAvance->setText(QString::number(d.avanceVal));
    m_modeAvance = d.avanceMode;
    ui->editToolFM1->setText(d.fonctionM1); ui->editToolFM2->setText(d.fonctionM2);
    ui->editToolDL->setText(QString::number(d.DL)); ui->editToolDR->setText(QString::number(d.DR)); ui->editToolDR2->setText(QString::number(d.Routil2));
    (d.brocheCW ? ui->radioM03 : ui->radioM04)->setChecked(true);
    ui->editSomLongueur->setText(QString::number(d.longueur)); ui->editSomLargeur->setText(QString::number(d.largeur));
    ui->editSomRayon->setText(QString::number(d.rayonAngle)); ui->editSomDiametre->setText(QString::number(d.diametre));
    ui->editSomProfondeur->setText(QString::number(d.profondeur)); ui->editSomPasse->setText(QString::number(d.profPasse));
    ui->editSomSurepLat->setText(QString::number(d.surepLat)); ui->editSomSurepProf->setText(QString::number(d.surepProf));
    ui->editPMLongueur->setText(QString::number(d.longueur)); ui->editPMLargeur->setText(QString::number(d.largeur));
    ui->editPMRayon->setText(QString::number(d.rayonAngle)); ui->editPMDiametre->setText(QString::number(d.diametre));
    ui->editPMProfondeur->setText(QString::number(d.profondeur)); ui->editPMPasse->setText(QString::number(d.profPasse));
    ui->editPMSurepLat->setText(QString::number(d.surepLat)); ui->editPMSurepProf->setText(QString::number(d.surepProf));
    ui->editPMPasseFin->setText(QString::number(d.passeFinition)); ui->editPMAvanceFin->setText(QString::number(d.avanceFinition));
    if (qAbs(d.surepFinLat)  > 1e-9) ui->editPMSurepFinLat->setText(QString::number(d.surepFinLat));  else ui->editPMSurepFinLat->clear();
    if (qAbs(d.surepFinProf) > 1e-9) ui->editPMSurepFinProf->setText(QString::number(d.surepFinProf)); else ui->editPMSurepFinProf->clear();
    if (d.rayonEntreeSortie > 0.0001)
        ui->editPMRayonES->setText(QString::number(d.rayonEntreeSortie));
    else
        ui->editPMRayonES->clear();   // vide → rayon coin par défaut
    m_modeAvanceFin = d.avanceFinMode;
    ui->editPMAngulaire->setText(QString::number(d.posAngulaire));
    if (m_posGroup->button(d.positionPoche)) m_posGroup->button(d.positionPoche)->setChecked(true);
    ui->editSomModelePts->setText(d.fichierPoints); ui->editPositionFichier->setText(d.fichierPoints);

    ui->editPosX->setText(QString::number(d.posX));
    ui->editPosY->setText(QString::number(d.posY));

    if (ui->editLigneP1X) ui->editLigneP1X->setText(QString::number(d.ligneP1X));
    if (ui->editLigneP1Y) ui->editLigneP1Y->setText(QString::number(d.ligneP1Y));
    if (ui->editLigneP2X) ui->editLigneP2X->setText(QString::number(d.ligneP2X));
    if (ui->editLigneP2Y) ui->editLigneP2Y->setText(QString::number(d.ligneP2Y));
    if (ui->spinLigneNbPts) ui->spinLigneNbPts->setValue(d.ligneNbPts);

    if (ui->editGrilleOX)   ui->editGrilleOX->setText(QString::number(d.grilleOX));
    if (ui->editGrilleOY)   ui->editGrilleOY->setText(QString::number(d.grilleOY));
    if (ui->editGrilleOZ)   ui->editGrilleOZ->setText(QString::number(d.grilleOZ));
    if (ui->editGrillePasX) ui->editGrillePasX->setText(QString::number(d.grillePasX));
    if (ui->editGrillePasY) ui->editGrillePasY->setText(QString::number(d.grillePasY));
    if (ui->spinGrilleNbX)  ui->spinGrilleNbX->setValue(d.grilleNbX);
    if (ui->spinGrilleNbY)  ui->spinGrilleNbY->setValue(d.grilleNbY);

    if (ui->editCercleCX)    ui->editCercleCX->setText(QString::number(d.cercleCX));
    if (ui->editCercleCY)    ui->editCercleCY->setText(QString::number(d.cercleCY));
    if (ui->editCercleRayon) ui->editCercleRayon->setText(QString::number(d.cercleRayon));
    if (ui->editCercleZ)     ui->editCercleZ->setText(QString::number(d.cercleZ));
    if (ui->spinCercleNbPts) ui->spinCercleNbPts->setValue(d.cercleNbPts);
    if (ui->editCercleAngDep) ui->editCercleAngDep->setText(QString::number(d.cercleAngDep));
    if (ui->editCercleAngInc) ui->editCercleAngInc->setText(QString::number(d.cercleAngInc));

    if (ui->tablePoints) {
        ui->tablePoints->blockSignals(true);
        const auto &pts = d.tableauPts.isEmpty()
                              ? QVector<PocheData::TableauPoint>(10)
                              : d.tableauPts;
        ui->tablePoints->setRowCount(0);
        for (int i = 0; i < pts.size(); ++i) {
            int r = ui->tablePoints->rowCount();
            ui->tablePoints->insertRow(r);
            ui->tablePoints->setItem(r, 0, new QTableWidgetItem(QString::number(i+1)));
            ui->tablePoints->setItem(r, 1, new QTableWidgetItem(QString::number(pts[i].x)));
            ui->tablePoints->setItem(r, 2, new QTableWidgetItem(QString::number(pts[i].y)));
            ui->tablePoints->setItem(r, 3, new QTableWidgetItem(QString::number(pts[i].z)));
        }
        ui->tablePoints->blockSignals(false);
    }

    if (ui->stackPosition) ui->stackPosition->setCurrentWidget(posModeToWidget(d.positionMode));
    updateSommairePosition();   // sinon « Modèle de points » gardait le texte du bloc précédent
    ui->editGlobApproche->setText(QString::number(d.approcheLocal  ? d.approche  : m_heritedApproche));
    ui->editGlobSautBride->setText(QString::number(d.sautBrideLocal ? d.sautBride : m_heritedSautBride));
    if (m_editGlobZLiaison)
        m_editGlobZLiaison->setText(QString::number(d.zLiaisonLocal ? d.zLiaison : m_heritedZLiaison));
    if (m_editGlobAvanceLiaison)
        m_editGlobAvanceLiaison->setText(QString::number(d.avanceLiaisonLocal ? d.avanceLiaison : m_heritedAvanceLiaison));
    if (m_chkGlobOrigine && m_cbGlobOrigine) {
        bool chk = d.origineLocal ? d.origineDefinie : m_heritedOrigineDef;
        int  org = d.origineLocal ? d.origine        : m_heritedOrigine;
        m_chkGlobOrigine->setChecked(chk);
        m_cbGlobOrigine->setCurrentIndex(qBound(0, org, 5));
    }
    if (m_bgApproche)      m_bgApproche->setHeritance(!d.approcheLocal);
    if (m_bgSautBride)     m_bgSautBride->setHeritance(!d.sautBrideLocal);
    if (m_bgZLiaison)      m_bgZLiaison->setHeritance(!d.zLiaisonLocal);
    if (m_bgAvanceLiaison) m_bgAvanceLiaison->setHeritance(!d.avanceLiaisonLocal);
    if (m_bgOrigine)       m_bgOrigine->setHeritance(!d.origineLocal);
    ui->spinGlobRecouvrement->setValue(d.recouvrement);

    m_posG0 = d.positionnementG0;
    if (d.positionnementG0) {
        ui->radioFRapide->setChecked(true);
        ui->editGlobPositionnement->setEnabled(false);
        ui->editGlobPositionnement->clear();
    } else {
        ui->radioFValeur->setChecked(true);
        ui->editGlobPositionnement->setEnabled(true);
        ui->editGlobPositionnement->setText(QString::number(d.positionnementF));
    }

    m_modeFraisage = d.modeFraisage;
    (d.modeFraisage==0 ? ui->radioAvalant : ui->radioOpposition)->setChecked(true);

    m_modePlongee = d.modePlongee;
    switch(d.modePlongee){
    case 0: ui->radioHelicoidale->setChecked(true); break;
    case 1: ui->radioRampe->setChecked(true); break;
    default: ui->radioDirecte->setChecked(true); break;
    }

    updateBadgeS();
    updateBadgeDescente();
    updateBadgePendul();
    updateBadgeAvance();

    m_syncing = false;
    ui->tabWidget->setCurrentIndex(0);
}

PocheData UnitEditor::save() const
{
    PocheData d;

    d.toolRef        = ui->editSomT->text();       d.toolByName     = m_toolByName;
    d.spindleVal     = ui->editSomS->text().toDouble(); d.spindleByVc= (m_modeS==1);
    d.descenteVal    = ui->editSomDescente->text().toDouble(); d.descenteMode = m_modeDescente;
    d.pendulVal      = ui->editSomPendul->text().toDouble();  d.pendulMode   = m_modePendul;
    d.avanceVal      = ui->editSomAvance->text().toDouble(); d.avanceMode   = m_modeAvance;
    d.fonctionM1     = ui->editToolFM1->text();    d.fonctionM2     = ui->editToolFM2->text();
    d.DL             = ui->editToolDL->text().toDouble(); d.DR = ui->editToolDR->text().toDouble(); d.Routil2 = ui->editToolDR2->text().toDouble();
    d.brocheCW       = ui->radioM03->isChecked();
    d.isCirculaire   = (m_blockMode == PocheCirc);
    d.longueur       = ui->editSomLongueur->text().toDouble(); d.largeur   = ui->editSomLargeur->text().toDouble();
    d.rayonAngle     = ui->editSomRayon->text().toDouble();    d.diametre  = ui->editSomDiametre->text().toDouble();
    d.profondeur     = ui->editSomProfondeur->text().toDouble(); d.profPasse = ui->editSomPasse->text().toDouble();
    d.surepLat       = ui->editSomSurepLat->text().toDouble(); d.surepProf = ui->editSomSurepProf->text().toDouble();
    d.passeFinition  = ui->editPMPasseFin->text().toDouble();  d.avanceFinition = ui->editPMAvanceFin->text().toDouble();
    d.surepFinLat    = ui->editPMSurepFinLat->text().replace(',', '.').toDouble();
    d.surepFinProf   = ui->editPMSurepFinProf->text().replace(',', '.').toDouble();
    d.rayonEntreeSortie = ui->editPMRayonES->text().toDouble();   // 0 = rayon coin
    d.avanceFinMode  = m_modeAvanceFin;            d.posAngulaire   = ui->editPMAngulaire->text().toDouble();
    d.positionPoche  = m_posGroup->checkedId();    d.fichierPoints  = ui->editSomModelePts->text();

    d.modeUsin = m_modeUsin ? m_modeUsin->mode() : 0;

    d.posX           = ui->editPosX ? ui->editPosX->text().toDouble() : 0.0;
    d.posY           = ui->editPosY ? ui->editPosY->text().toDouble() : 0.0;

    d.ligneP1X       = ui->editLigneP1X  ? ui->editLigneP1X->text().toDouble()  : 0.0;
    d.ligneP1Y       = ui->editLigneP1Y  ? ui->editLigneP1Y->text().toDouble()  : 0.0;
    d.ligneP2X       = ui->editLigneP2X  ? ui->editLigneP2X->text().toDouble()  : 0.0;
    d.ligneP2Y       = ui->editLigneP2Y  ? ui->editLigneP2Y->text().toDouble()  : 0.0;
    d.ligneNbPts     = ui->spinLigneNbPts ? ui->spinLigneNbPts->value()         : 1;

    d.grilleOX       = ui->editGrilleOX   ? ui->editGrilleOX->text().toDouble() : 0.0;
    d.grilleOY       = ui->editGrilleOY   ? ui->editGrilleOY->text().toDouble() : 0.0;
    d.grilleOZ       = ui->editGrilleOZ   ? ui->editGrilleOZ->text().toDouble() : 0.0;
    d.grillePasX     = ui->editGrillePasX ? ui->editGrillePasX->text().toDouble(): 0.0;
    d.grillePasY     = ui->editGrillePasY ? ui->editGrillePasY->text().toDouble(): 0.0;
    d.grilleNbX      = ui->spinGrilleNbX  ? ui->spinGrilleNbX->value()          : 1;
    d.grilleNbY      = ui->spinGrilleNbY  ? ui->spinGrilleNbY->value()          : 1;

    d.cercleCX       = ui->editCercleCX    ? ui->editCercleCX->text().toDouble()   : 0.0;
    d.cercleCY       = ui->editCercleCY    ? ui->editCercleCY->text().toDouble()   : 0.0;
    d.cercleRayon    = ui->editCercleRayon ? ui->editCercleRayon->text().toDouble(): 0.0;
    d.cercleZ        = ui->editCercleZ     ? ui->editCercleZ->text().toDouble()    : 0.0;
    d.cercleNbPts    = ui->spinCercleNbPts ? ui->spinCercleNbPts->value()          : 4;
    d.cercleAngDep   = ui->editCercleAngDep ? ui->editCercleAngDep->text().toDouble(): 0.0;
    d.cercleAngInc   = ui->editCercleAngInc ? ui->editCercleAngInc->text().toDouble(): 90.0;

    d.tableauPts.clear();
    if (ui->tablePoints) {
        for (int r = 0; r < ui->tablePoints->rowCount(); ++r) {
            PocheData::TableauPoint tp;
            tp.x = ui->tablePoints->item(r,1) ? ui->tablePoints->item(r,1)->text().toDouble() : 0.0;
            tp.y = ui->tablePoints->item(r,2) ? ui->tablePoints->item(r,2)->text().toDouble() : 0.0;
            tp.z = ui->tablePoints->item(r,3) ? ui->tablePoints->item(r,3)->text().toDouble() : 0.0;
            d.tableauPts << tp;
        }
    }
    if (d.tableauPts.isEmpty())
        d.tableauPts = QVector<PocheData::TableauPoint>(10);

    d.positionMode   = ui->stackPosition ? posWidgetToMode(ui->stackPosition->currentWidget()) : 4;
    d.approcheLocal       = m_bgApproche      ? !m_bgApproche->heritance()      : false;
    d.sautBrideLocal      = m_bgSautBride     ? !m_bgSautBride->heritance()     : false;
    d.zLiaisonLocal       = m_bgZLiaison      ? !m_bgZLiaison->heritance()      : false;
    d.avanceLiaisonLocal  = m_bgAvanceLiaison ? !m_bgAvanceLiaison->heritance() : false;
    d.origineLocal        = m_bgOrigine       ? !m_bgOrigine->heritance()       : false;
    d.approche      = ui->editGlobApproche->text().toDouble();
    d.sautBride     = ui->editGlobSautBride->text().toDouble();
    if (m_editGlobZLiaison)      d.zLiaison      = m_editGlobZLiaison->text().toDouble();
    if (m_editGlobAvanceLiaison) d.avanceLiaison = m_editGlobAvanceLiaison->text().toDouble();
    if (m_chkGlobOrigine) d.origineDefinie = m_chkGlobOrigine->isChecked();
    if (m_cbGlobOrigine)  d.origine        = m_cbGlobOrigine->currentIndex();
    d.recouvrement   = ui->spinGlobRecouvrement->value();
    d.positionnementG0 = m_posG0;
    d.positionnementF  = ui->editGlobPositionnement->text().toDouble();
    d.modeFraisage   = ui->radioAvalant->isChecked() ? 0 : 1;
    d.modePlongee    = ui->radioHelicoidale->isChecked() ? 0 : ui->radioRampe->isChecked() ? 1 : 2;

    return d;
}

void UnitEditor::load(const TrouData &d)
{
    setBlockMode(Trou);
    m_syncing = true;
    ui->editSomT->setText(d.toolRef); ui->editToolT->setText(d.toolRef);
    m_toolByName = d.toolByName;
    ui->editSomS->setText(QString::number(d.spindleVal)); ui->editToolS->setText(QString::number(d.spindleVal));
    m_modeS = d.spindleByVc ? 1 : 0;
    ui->editSomDescente->setText(QString::number(d.descenteVal)); ui->editToolDescente->setText(QString::number(d.descenteVal));
    m_modeDescente = d.descenteMode;
    ui->editToolFM1->setText(d.fonctionM1); ui->editToolFM2->setText(d.fonctionM2);
    ui->editToolDL->setText(QString::number(d.DL)); ui->editToolDR->setText(QString::number(d.DR));
    (d.brocheCW ? ui->radioM03 : ui->radioM04)->setChecked(true);
    // Fichier de points (.pnt) partagé : affiché dans le Sommaire
    if (ui->editSomModelePts)    ui->editSomModelePts->setText(d.fichierPoints);
    if (ui->editPositionFichier) ui->editPositionFichier->setText(d.fichierPoints);
    if (m_trouProf)       m_trouProf->setText(QString::number(d.profondeur));
    // Le Sommaire aussi (le lien automatique est coupé pendant le chargement :
    // sinon il gardait la profondeur du bloc affiché avant)
    if (ui->editSomProfondeur) ui->editSomProfondeur->setText(QString::number(d.profondeur));
    if (m_trouTraversant) m_trouTraversant->setChecked(d.traversant);
    // Perçage profond — cycle unique, comportement pilote par les valeurs
    if (m_trouProfPasse)     m_trouProfPasse->setText(d.profPasse > 0 ? QString::number(d.profPasse) : QString());
    if (m_trouReduction)     m_trouReduction->setText(QString::number(d.reductionPasse));
    if (m_trouPasseMin)      m_trouPasseMin->setText(QString::number(d.passeMin));
    if (m_trouProfBrise)     m_trouProfBrise->setText(QString::number(d.profBriseCopeaux));
    if (m_trouRetraitBrise)  m_trouRetraitBrise->setText(QString::number(d.retraitBriseCopeaux));
    if (m_trouTempoFond)     m_trouTempoFond->setText(QString::number(d.tempoFond));
    if (m_trouApproche)   m_trouApproche->setText(QString::number(d.approcheLocal  ? d.approche  : m_heritedApproche));
    if (m_trouSautBride)  m_trouSautBride->setText(QString::number(d.sautBrideLocal ? d.sautBride : m_heritedSautBride));
    if (m_trouZLiaison)
        m_trouZLiaison->setText(QString::number(d.zLiaisonLocal ? d.zLiaison : m_heritedZLiaison));
    if (m_trouAvanceLiaison)
        m_trouAvanceLiaison->setText(QString::number(d.avanceLiaisonLocal ? d.avanceLiaison : m_heritedAvanceLiaison));
    if (m_trouChkOrigine && m_trouCbOrigine) {
        bool chk = d.origineLocal ? d.origineDefinie : m_heritedOrigineDef;
        int  org = d.origineLocal ? d.origine        : m_heritedOrigine;
        m_trouChkOrigine->setChecked(chk);
        m_trouCbOrigine->setCurrentIndex(qBound(0, org, 5));
    }
    if (m_bgTrouApproche)      m_bgTrouApproche->setHeritance(!d.approcheLocal);
    if (m_bgTrouSautBride)     m_bgTrouSautBride->setHeritance(!d.sautBrideLocal);
    if (m_bgTrouZLiaison)      m_bgTrouZLiaison->setHeritance(!d.zLiaisonLocal);
    if (m_bgTrouAvanceLiaison) m_bgTrouAvanceLiaison->setHeritance(!d.avanceLiaisonLocal);
    if (m_bgTrouOrigine)       m_bgTrouOrigine->setHeritance(!d.origineLocal);

    // ── Rechargement complet de l'onglet Position ───────────────────
    //  Sans ça, un cycle save→load (déclenché par exemple par un
    //  changement d'outil) vide tous les champs point/ligne/grille/
    //  cercle. On recharge donc toutes les coordonnées, comme le fait
    //  load(PocheData).
    if (ui->editLigneP1X)    ui->editLigneP1X->setText(QString::number(d.ligneP1X));
    if (ui->editLigneP1Y)    ui->editLigneP1Y->setText(QString::number(d.ligneP1Y));
    if (ui->editLigneP2X)    ui->editLigneP2X->setText(QString::number(d.ligneP2X));
    if (ui->editLigneP2Y)    ui->editLigneP2Y->setText(QString::number(d.ligneP2Y));
    if (ui->spinLigneNbPts)  ui->spinLigneNbPts->setValue(d.ligneNbPts);

    if (ui->editGrilleOX)    ui->editGrilleOX->setText(QString::number(d.grilleOX));
    if (ui->editGrilleOY)    ui->editGrilleOY->setText(QString::number(d.grilleOY));
    if (ui->editGrillePasX)  ui->editGrillePasX->setText(QString::number(d.grillePasX));
    if (ui->editGrillePasY)  ui->editGrillePasY->setText(QString::number(d.grillePasY));
    if (ui->spinGrilleNbX)   ui->spinGrilleNbX->setValue(d.grilleNbX);
    if (ui->spinGrilleNbY)   ui->spinGrilleNbY->setValue(d.grilleNbY);

    if (ui->editCercleCX)    ui->editCercleCX->setText(QString::number(d.cercleCX));
    if (ui->editCercleCY)    ui->editCercleCY->setText(QString::number(d.cercleCY));
    if (ui->editCercleRayon) ui->editCercleRayon->setText(QString::number(d.cercleRayon));
    if (ui->spinCercleNbPts) ui->spinCercleNbPts->setValue(d.cercleNbPts);
    if (ui->editCercleAngDep)ui->editCercleAngDep->setText(QString::number(d.cercleAngDep));
    if (ui->editCercleAngInc)ui->editCercleAngInc->setText(QString::number(d.cercleAngFin));

    if (ui->stackPosition) ui->stackPosition->setCurrentWidget(posModeToWidget(d.positionMode));
    updateSommairePosition();
    m_posG0 = d.positionnementG0;
    if (d.positionnementG0) { ui->radioFRapide->setChecked(true); ui->editGlobPositionnement->setEnabled(false); ui->editGlobPositionnement->clear(); }
    else { ui->radioFValeur->setChecked(true); ui->editGlobPositionnement->setEnabled(true); ui->editGlobPositionnement->setText(QString::number(d.positionnementF)); }
    updateBadgeS(); updateBadgeDescente(); updateBadgeAvance();
    m_syncing = false;
    ui->tabWidget->setCurrentIndex(0);
}

TrouData UnitEditor::saveTrou() const
{
    TrouData d;
    d.toolRef      = ui->editSomT->text();           d.toolByName   = m_toolByName;
    d.spindleVal   = ui->editSomS->text().toDouble(); d.spindleByVc = (m_modeS==1);
    d.descenteVal  = ui->editSomDescente->text().toDouble(); d.descenteMode = m_modeDescente;
    d.fonctionM1   = ui->editToolFM1->text();        d.fonctionM2   = ui->editToolFM2->text();
    d.DL           = ui->editToolDL->text().toDouble(); d.DR = ui->editToolDR->text().toDouble();
    d.brocheCW     = ui->radioM03->isChecked();
    if (m_trouProf)      d.profondeur = m_trouProf->text().toDouble();
    if (m_trouTraversant) d.traversant = m_trouTraversant->isChecked();
    // Perçage profond — cycle unique
    if (m_trouProfPasse)     d.profPasse           = m_trouProfPasse->text().toDouble();
    if (m_trouReduction)     d.reductionPasse      = m_trouReduction->text().toDouble();
    if (m_trouPasseMin)      d.passeMin            = m_trouPasseMin->text().toDouble();
    if (m_trouProfBrise)     d.profBriseCopeaux    = m_trouProfBrise->text().toDouble();
    if (m_trouRetraitBrise)  d.retraitBriseCopeaux = m_trouRetraitBrise->text().toDouble();
    if (m_trouTempoFond)     d.tempoFond           = m_trouTempoFond->text().toDouble();
    if (m_trouApproche)  d.approche  = m_trouApproche->text().toDouble();
    if (m_trouSautBride) d.sautBride = m_trouSautBride->text().toDouble();
    if (m_trouZLiaison)      d.zLiaison      = m_trouZLiaison->text().toDouble();
    if (m_trouAvanceLiaison) d.avanceLiaison = m_trouAvanceLiaison->text().toDouble();
    if (m_trouChkOrigine) d.origineDefinie = m_trouChkOrigine->isChecked();
    if (m_trouCbOrigine)  d.origine        = m_trouCbOrigine->currentIndex();
    d.approcheLocal      = m_bgTrouApproche      ? !m_bgTrouApproche->heritance()      : false;
    d.sautBrideLocal     = m_bgTrouSautBride     ? !m_bgTrouSautBride->heritance()     : false;
    d.zLiaisonLocal      = m_bgTrouZLiaison      ? !m_bgTrouZLiaison->heritance()      : false;
    d.avanceLiaisonLocal = m_bgTrouAvanceLiaison ? !m_bgTrouAvanceLiaison->heritance() : false;
    d.origineLocal       = m_bgTrouOrigine       ? !m_bgTrouOrigine->heritance()       : false;
    d.positionnementG0 = m_posG0;
    d.positionnementF  = ui->editGlobPositionnement ? ui->editGlobPositionnement->text().toDouble() : 0.0;
    d.positionMode = ui->stackPosition ? posWidgetToMode(ui->stackPosition->currentWidget()) : 0;
    d.fichierPoints = ui->editSomModelePts ? ui->editSomModelePts->text().trimmed() : QString();
    d.ligneP1X  = ui->editLigneP1X  ? ui->editLigneP1X->text().toDouble()  : 0.0;
    d.ligneP1Y  = ui->editLigneP1Y  ? ui->editLigneP1Y->text().toDouble()  : 0.0;
    d.ligneP2X  = ui->editLigneP2X  ? ui->editLigneP2X->text().toDouble()  : 0.0;
    d.ligneP2Y  = ui->editLigneP2Y  ? ui->editLigneP2Y->text().toDouble()  : 0.0;
    d.ligneNbPts= ui->spinLigneNbPts? ui->spinLigneNbPts->value()          : 2;
    d.grilleOX  = ui->editGrilleOX  ? ui->editGrilleOX->text().toDouble()  : 0.0;
    d.grilleOY  = ui->editGrilleOY  ? ui->editGrilleOY->text().toDouble()  : 0.0;
    d.grillePasX= ui->editGrillePasX? ui->editGrillePasX->text().toDouble(): 0.0;
    d.grillePasY= ui->editGrillePasY? ui->editGrillePasY->text().toDouble(): 0.0;
    d.grilleNbX = ui->spinGrilleNbX ? ui->spinGrilleNbX->value()           : 1;
    d.grilleNbY = ui->spinGrilleNbY ? ui->spinGrilleNbY->value()           : 1;
    d.cercleCX  = ui->editCercleCX  ? ui->editCercleCX->text().toDouble()  : 0.0;
    d.cercleCY  = ui->editCercleCY  ? ui->editCercleCY->text().toDouble()  : 0.0;
    d.cercleRayon=ui->editCercleRayon?ui->editCercleRayon->text().toDouble(): 0.0;
    d.cercleNbPts=ui->spinCercleNbPts?ui->spinCercleNbPts->value()         : 4;
    d.cercleAngDep=ui->editCercleAngDep?ui->editCercleAngDep->text().toDouble():0.0;
    d.cercleAngFin=ui->editCercleAngInc?ui->editCercleAngInc->text().toDouble():360.0;
    return d;
}

void UnitEditor::setToolFromTable(const QString &ref, bool byName)
{
    m_toolByName = byName;
    ui->editSomT->setText(ref);
    ui->editToolT->setText(ref);  // BUG CORRIGÉ : l'onglet Tool restait
                                  // désynchronisé (jamais mis à jour ici)
}

void UnitEditor::applyToolData(const ToolData &tool)
{
    // Le champ T reste TOUJOURS numérique — c'est ce que le générateur
    // G-code lit (toolByT(T.toInt())) pour retrouver le rayon outil etc.
    // Le nom (description) n'est qu'un affichage complémentaire pour la
    // lisibilité opérateur — via tooltip — jamais utilisé comme référence.
    const QString numStr = QString::number(tool.T);
    ui->editSomT->setText(numStr);
    ui->editToolT->setText(numStr);
    const QString nameTxt = tool.description.isEmpty() ? "" : tool.description;
    if (m_lblToolName)    m_lblToolName->setText(nameTxt);
    if (m_lblSomToolName) m_lblSomToolName->setText(nameTxt);

    double S = (tool.spindleSpeed > 0) ? (double)tool.spindleSpeed : 0.0;
    if (S > 0) { ui->editSomS->setText(QString::number(qRound(S))); m_modeS=0; updateBadgeS(); }
    ui->editToolS->setText(QString::number(qRound(S)));
    ui->editToolDL->setText("0.000");
    ui->editToolDR->setText("0.000");
    ui->editToolDR2->setText(tool.Routil2>0 ? QString::number(tool.Routil2,'f',3) : "0.000");
    if (tool.spindleDir != 0)
        ui->radioM04->setChecked(true);
    m_toolByName = false;
}

void UnitEditor::setPointFile(const QString &path)
{
    ui->editSomModelePts->setText(path);
    ui->editPositionFichier->setText(path);
    // Rafraîchir tout de suite le label "Fichier de points : ..." et
    // l'aperçu — sans ça il faut fermer/rouvrir le bloc pour que le
    // Sommaire cesse d'afficher le mode générique ("Point X0 Y0").
    updateSommairePosition();
    if (m_blockMode == FraisageTrou) updateFtPointsPreview();
}

// ─────────────────────────────────────────────────────────────
//  load / save ContourData — VERSION MODIFIÉE
// ─────────────────────────────────────────────────────────────
void UnitEditor::load(const ContourData &d)
{
    setBlockMode(Contour);
    m_syncing = true;

    ui->editSomT->setText(d.toolRef);    ui->editToolT->setText(d.toolRef);
    m_toolByName = d.toolByName;
    ui->editSomS->setText(QString::number(d.spindleVal));
    ui->editToolS->setText(QString::number(d.spindleVal));
    m_modeS = d.spindleByVc ? 1 : 0;
    ui->editSomDescente->setText(QString::number(d.descenteVal));
    ui->editToolDescente->setText(QString::number(d.descenteVal));
    m_modeDescente = d.descenteMode;
    ui->editSomAvance->setText(QString::number(d.avanceVal));
    ui->editToolAvance->setText(QString::number(d.avanceVal));
    m_modeAvance = d.avanceMode;
    // Avance pendulaire : à remplir aussi, sinon le champ gardait la
    // valeur du bloc affiché juste avant (ex 250 d'une poche)
    ui->editSomPendul->setText(d.pendulVal > 0 ? QString::number(d.pendulVal) : QString());
    m_modePendul = d.pendulMode;
    ui->editToolFM1->setText(d.fonctionM1);
    ui->editToolFM2->setText(d.fonctionM2);
    ui->editToolDL->setText(QString::number(d.DL));
    ui->editToolDR->setText(QString::number(d.DR));
    ui->editToolDR2->setText(QString::number(d.Routil2));
    (d.brocheCW ? ui->radioM03 : ui->radioM04)->setChecked(true);

    if (m_cntProfondeur) m_cntProfondeur->setText(QString::number(d.profondeur));
    if (m_cntPasse)      m_cntPasse->setText(QString::number(d.profPasse));
    if (m_cntSurepLat)   m_cntSurepLat->setText(QString::number(d.surepLat));
    if (m_cntSurepProf)  m_cntSurepProf->setText(QString::number(d.surepProf));
    if (m_cntPasseFin)   m_cntPasseFin->setText(QString::number(d.passeFinition));

    // ── Mode d'usinage + step-over ──────────────────────────
    m_modeUsinContourValue = d.modeUsin;
    if (m_modeUsinContour) m_modeUsinContour->setMode(d.modeUsin);
    if (m_cntStepOverPct)  m_cntStepOverPct->setValue(qBound(10, d.stepOverPct, 95));
    if (m_cntApproche)   m_cntApproche->setText(QString::number(d.approcheLocal  ? d.approche  : m_heritedApproche));
    if (m_cntSautBride)  m_cntSautBride->setText(QString::number(d.sautBrideLocal ? d.sautBride : m_heritedSautBride));
    if (m_cntZLiaison)
        m_cntZLiaison->setText(QString::number(d.zLiaisonLocal ? d.zLiaison : m_heritedZLiaison));
    if (m_cntAvanceLiaison)
        m_cntAvanceLiaison->setText(QString::number(d.avanceLiaisonLocal ? d.avanceLiaison : m_heritedAvanceLiaison));
    if (m_cntChkOrigine && m_cntCbOrigine) {
        bool chk = d.origineLocal ? d.origineDefinie : m_heritedOrigineDef;
        int  org = d.origineLocal ? d.origine        : m_heritedOrigine;
        m_cntChkOrigine->setChecked(chk);
        m_cntCbOrigine->setCurrentIndex(qBound(0, org, 5));
    }
    if (m_bgCntApproche)      m_bgCntApproche->setHeritance(!d.approcheLocal);
    if (m_bgCntSautBride)     m_bgCntSautBride->setHeritance(!d.sautBrideLocal);
    if (m_bgCntZLiaison)      m_bgCntZLiaison->setHeritance(!d.zLiaisonLocal);
    if (m_bgCntAvanceLiaison) m_bgCntAvanceLiaison->setHeritance(!d.avanceLiaisonLocal);
    if (m_bgCntOrigine)       m_bgCntOrigine->setHeritance(!d.origineLocal);

    if (m_cntCbPosition) m_cntCbPosition->setCurrentIndex(qBound(0, d.positionOutil, 3));
    if (auto *rb = m_contourTab ? m_contourTab->findChild<QRadioButton*>("cnt_rbAval") : nullptr)
        rb->setChecked(d.modeFraisage == 0);
    if (auto *rb = m_contourTab ? m_contourTab->findChild<QRadioButton*>("cnt_rbOppo") : nullptr)
        rb->setChecked(d.modeFraisage == 1);

    // ── Stratégie finition + outil ébauche ────────────────
    if (m_cntRbFinitContour) m_cntRbFinitContour->setChecked(d.strategieFinition == 0);
    if (m_cntRbFinitCoins)   m_cntRbFinitCoins  ->setChecked(d.strategieFinition == 1);
    if (m_cntToolRefEbauche) m_cntToolRefEbauche->setText(d.toolRefEbauche);
    if (m_cntLblToolEbauche) m_cntLblToolEbauche->setEnabled(d.strategieFinition == 1);
    if (m_cntToolRefEbauche) m_cntToolRefEbauche->setEnabled(d.strategieFinition == 1);

    // (G40/G41/G42 supprimes de l'UI - voir saveContour qui force 40)

    // ── NOUVEAU : Plongée Z ─────────────────────────────────
    if (m_cntRbPlongeeG0) m_cntRbPlongeeG0->setChecked( d.plongeeRapide);
    if (m_cntRbPlongeeF)  m_cntRbPlongeeF ->setChecked(!d.plongeeRapide);

    // ── NOUVEAU : Entrée matière ───────────────────────────
    {
        QRadioButton *rbs[3] = {m_cntRbEntreePerp, m_cntRbEntreeArc, m_cntRbEntreeLigne};
        int t = qBound(0, d.typeEntree, 2);
        for (int i = 0; i < 3; ++i) if (rbs[i]) rbs[i]->setChecked(i == t);
    }
    if (m_cntParamEntree) m_cntParamEntree->setText(QString::number(d.paramEntree));

    // ── NOUVEAU : Sortie matière ───────────────────────────
    {
        QRadioButton *rbs[3] = {m_cntRbSortiePerp, m_cntRbSortieArc, m_cntRbSortieLigne};
        int t = qBound(0, d.typeSortie, 2);
        for (int i = 0; i < 3; ++i) if (rbs[i]) rbs[i]->setChecked(i == t);
    }
    if (m_cntParamSortie) m_cntParamSortie->setText(QString::number(d.paramSortie));

    // Mise à jour des labels Longueur/Rayon selon le type
    if (m_cntLblParamEntree)
        m_cntLblParamEntree->setText(d.typeEntree == 1 ? "Rayon" : "Longueur");
    if (m_cntLblParamSortie)
        m_cntLblParamSortie->setText(d.typeSortie == 1 ? "Rayon" : "Longueur");

    // ── Poche de contour ──────────────────────────────────
    pocSetList(d.pocList);
    if (m_pocRbDirecte) m_pocRbDirecte->setChecked(d.pocPlongee == 0);
    if (m_pocRbHelice)  m_pocRbHelice ->setChecked(d.pocPlongee == 1);
    if (m_pocRbRampe)   m_pocRbRampe  ->setChecked(d.pocPlongee == 2);
    if (m_pocChkEntree) m_pocChkEntree->setChecked(d.entreeDefinie);
    if (m_pocEntreeX)   m_pocEntreeX->setText(QString::number(d.entreeX));
    if (m_pocEntreeY)   m_pocEntreeY->setText(QString::number(d.entreeY));
    setPocheContourMode(d.typeCycle == 1);

    m_cntFilePath = d.contourFile;
    m_cntName     = d.contourName;
    if (m_cntNomFichier) {
        QString display;
        if (!d.contourFile.isEmpty())
            display = QFileInfo(d.contourFile).baseName();
        else if (!d.contourName.isEmpty())
            display = d.contourName;
        m_cntNomFichier->setText(display.isEmpty() ? "— aucun contour —" : display);
    }
    if (ui->editSomModelePts)
        ui->editSomModelePts->setText(d.contourFile.isEmpty() ? d.contourName
                                                              : QFileInfo(d.contourFile).fileName());

    m_syncing = false;
    ui->tabWidget->setCurrentIndex(0);
}

ContourData UnitEditor::saveContour() const
{
    ContourData d;
    // G40/G41/G42 supprimes de l'UI : compensation calculee par EazyNC (Clipper),
    // donc on force toujours G40 dans la struct (NC en G40, plus aucun G41/G42).
    d.correctionFraise = 40;
    d.positionOutil = m_cntCbPosition ? m_cntCbPosition->currentIndex() : 0;

    d.toolRef      = ui->editSomT->text();
    d.toolByName   = m_toolByName;
    d.spindleVal   = ui->editSomS->text().toDouble();
    d.spindleByVc  = (m_modeS == 1);
    d.descenteVal  = ui->editSomDescente->text().toDouble();
    d.descenteMode = m_modeDescente;
    d.avanceVal    = ui->editSomAvance->text().toDouble();
    d.avanceMode   = m_modeAvance;
    // Avance pendulaire (rampe / helice) : n'etait pas enregistree -> perdue a l'Enreg.
    d.pendulVal    = ui->editSomPendul->text().toDouble();
    d.pendulMode   = m_modePendul;
    d.fonctionM1   = ui->editToolFM1->text();
    d.fonctionM2   = ui->editToolFM2->text();
    d.DL           = ui->editToolDL->text().toDouble();
    d.DR           = ui->editToolDR->text().toDouble();
    d.Routil2      = ui->editToolDR2->text().toDouble();
    d.brocheCW     = ui->radioM03->isChecked();

    if (m_cntProfondeur) d.profondeur   = m_cntProfondeur->text().toDouble();
    if (m_cntPasse)      d.profPasse    = m_cntPasse->text().toDouble();
    if (m_cntSurepLat)   d.surepLat     = m_cntSurepLat->text().toDouble();
    if (m_cntSurepProf)  d.surepProf    = m_cntSurepProf->text().toDouble();
    if (m_cntPasseFin)   d.passeFinition= m_cntPasseFin->text().toDouble();

    // ── Mode d'usinage + step-over ────────────────────────────
    d.modeUsin    = m_modeUsinContour ? m_modeUsinContour->mode() : 0;
    d.stepOverPct = m_cntStepOverPct  ? m_cntStepOverPct->value() : 60;
    if (m_cntApproche)  d.approche  = m_cntApproche->text().toDouble();
    if (m_cntSautBride) d.sautBride = m_cntSautBride->text().toDouble();
    if (m_cntZLiaison)      d.zLiaison      = m_cntZLiaison->text().toDouble();
    if (m_cntAvanceLiaison) d.avanceLiaison = m_cntAvanceLiaison->text().toDouble();
    if (m_cntChkOrigine) d.origineDefinie = m_cntChkOrigine->isChecked();
    if (m_cntCbOrigine)  d.origine        = m_cntCbOrigine->currentIndex();
    d.approcheLocal      = m_bgCntApproche      ? !m_bgCntApproche->heritance()      : false;
    d.sautBrideLocal     = m_bgCntSautBride     ? !m_bgCntSautBride->heritance()     : false;
    d.zLiaisonLocal      = m_bgCntZLiaison      ? !m_bgCntZLiaison->heritance()      : false;
    d.avanceLiaisonLocal = m_bgCntAvanceLiaison ? !m_bgCntAvanceLiaison->heritance() : false;
    d.origineLocal       = m_bgCntOrigine       ? !m_bgCntOrigine->heritance()       : false;

    if (auto *rb = m_contourTab ? m_contourTab->findChild<QRadioButton*>("cnt_rbOppo") : nullptr)
        d.modeFraisage = rb->isChecked() ? 1 : 0;

    // ── NOUVEAU : Plongée Z ─────────────────────────────────
    d.plongeeRapide = m_cntRbPlongeeG0 ? m_cntRbPlongeeG0->isChecked() : true;

    // ── NOUVEAU : Entrée matière ───────────────────────────
    if      (m_cntRbEntreeArc   && m_cntRbEntreeArc->isChecked())   d.typeEntree = 1;
    else if (m_cntRbEntreeLigne && m_cntRbEntreeLigne->isChecked()) d.typeEntree = 2;
    else                                                            d.typeEntree = 0;
    if (m_cntParamEntree) d.paramEntree = m_cntParamEntree->text().toDouble();

    // ── NOUVEAU : Sortie matière ───────────────────────────
    if      (m_cntRbSortieArc   && m_cntRbSortieArc->isChecked())   d.typeSortie = 1;
    else if (m_cntRbSortieLigne && m_cntRbSortieLigne->isChecked()) d.typeSortie = 2;
    else                                                            d.typeSortie = 0;
    if (m_cntParamSortie) d.paramSortie = m_cntParamSortie->text().toDouble();

    // ── Strategie finition + outil ebauche ─────────────────
    if (m_cntRbFinitCoins && m_cntRbFinitCoins->isChecked()) d.strategieFinition = 1;
    else                                                     d.strategieFinition = 0;
    if (m_cntToolRefEbauche) d.toolRefEbauche = m_cntToolRefEbauche->text().trimmed();

    d.contourFile = m_cntFilePath;
    d.contourName = m_cntName;

    // ── Poche de contour ──────────────────────────────────
    d.typeCycle = m_pocMode ? 1 : 0;
    if (m_pocMode) {
        d.pocList = pocEntries();
        // contour « principal » = 1re poche (nom du bloc, sim), profondeur = la plus profonde
        double pMax = 0; QString first;
        for (const PocEntry &e : d.pocList)
            if (!e.ilot) { pMax = qMax(pMax, e.profondeur); if (first.isEmpty()) first = e.file; }
        if (first.isEmpty() && !d.pocList.isEmpty()) first = d.pocList.first().file;
        d.contourFile = first;
        d.contourName = first.isEmpty() ? QString() : QFileInfo(first).baseName();
        if (pMax > 0) d.profondeur = pMax;
    }
    d.pocPlongee = (m_pocRbDirecte && m_pocRbDirecte->isChecked()) ? 0
                 : (m_pocRbRampe   && m_pocRbRampe->isChecked())   ? 2 : 1;
    d.entreeDefinie = m_pocChkEntree && m_pocChkEntree->isChecked();
    if (m_pocEntreeX) d.entreeX = m_pocEntreeX->text().toDouble();
    if (m_pocEntreeY) d.entreeY = m_pocEntreeY->text().toDouble();
    return d;
}

// ── Tableau poches / îlots ───────────────────────────────────
static double pocNum(const QLineEdit *e)
{ return e ? e->text().trimmed().replace(',', '.').toDouble() : 0.0; }

void UnitEditor::pocAddRow(const PocEntry &e)
{
    if (!m_pocTable) return;
    const int r = m_pocTable->rowCount();
    m_pocTable->insertRow(r);
    auto *it = new QTableWidgetItem(QFileInfo(e.file).baseName());
    it->setData(Qt::UserRole, e.file); it->setToolTip(e.file);
    m_pocTable->setItem(r, 0, it);
    // Type : 2 boutons radio (pas de liste deroulante -> la molette ne change rien)
    auto *tw = new QWidget; auto *tl = new QHBoxLayout(tw);
    tw->setMinimumWidth(150);
    tl->setContentsMargins(4,0,4,0); tl->setSpacing(6);
    auto *rbP = new QRadioButton("Poche"); auto *rbI = new QRadioButton("Îlot");
    rbP->setObjectName("poc_rbPoche"); rbI->setObjectName("poc_rbIlot");
    rbP->setFocusPolicy(Qt::NoFocus);  rbI->setFocusPolicy(Qt::NoFocus);
    auto *grp = new QButtonGroup(tw);  grp->addButton(rbP); grp->addButton(rbI);
    (e.ilot ? rbI : rbP)->setChecked(true);
    tl->addWidget(rbP); tl->addWidget(rbI);
    auto *ez = new QLineEdit(QString::number(e.zDepart));   ez->setMaximumWidth(70);
    auto *ep = new QLineEdit(QString::number(e.profondeur)); ep->setMaximumWidth(70);
    m_pocTable->setCellWidget(r, 1, tw);
    m_pocTable->setCellWidget(r, 2, ez);
    m_pocTable->setCellWidget(r, 3, ep);
    connect(rbI, &QRadioButton::toggled, this, [this](bool){ if (!m_syncing) emit dataChanged(); });
    for (auto *le : {ez, ep})
        connect(le, &QLineEdit::textChanged, this, [this](const QString&){ if (!m_syncing) emit dataChanged(); });
}

void UnitEditor::pocSetList(const QVector<PocEntry> &l)
{
    if (!m_pocTable) return;
    const bool was = m_syncing; m_syncing = true;
    m_pocTable->setRowCount(0);
    for (const PocEntry &e : l) pocAddRow(e);
    m_syncing = was;
}

QVector<PocEntry> UnitEditor::pocEntries() const
{
    QVector<PocEntry> l;
    if (!m_pocTable) return l;
    for (int r = 0; r < m_pocTable->rowCount(); ++r) {
        PocEntry e;
        if (auto *it = m_pocTable->item(r, 0)) e.file = it->data(Qt::UserRole).toString();
        if (auto *tw = m_pocTable->cellWidget(r, 1))
            if (auto *rbI = tw->findChild<QRadioButton*>("poc_rbIlot")) e.ilot = rbI->isChecked();
        e.zDepart    = pocNum(qobject_cast<QLineEdit*>(m_pocTable->cellWidget(r, 2)));
        e.profondeur = pocNum(qobject_cast<QLineEdit*>(m_pocTable->cellWidget(r, 3)));
        l << e;
    }
    return l;
}

void UnitEditor::setPocheContourMode(bool on)
{
    m_pocMode = on;
    auto show = [this](QWidget *w, bool v) {
        if (!w) return;
        w->setVisible(v);
        if (m_cntForm) if (auto *l = m_cntForm->labelForField(w)) l->setVisible(v);
    };
    for (auto *w : m_cntOnlyRows) show(w, !on);
    for (auto *w : m_pocOnlyRows) show(w, on);
    if (m_contourTabIndex >= 0)
        ui->tabWidget->setTabText(m_contourTabIndex, on ? "PM Poche contour" : "PM Contour");
}

void UnitEditor::setContourFile(const QString &path, const QString &name)
{
    if (m_pocMode && m_pocTable && !path.isEmpty()) {
        const auto cur = pocEntries();
        bool there = false;
        for (const PocEntry &e : cur) if (e.file == path) there = true;
        if (!there) {
            PocEntry e; e.file = path; e.ilot = !cur.isEmpty();
            e.profondeur = cur.isEmpty() ? 10.0 : cur.first().profondeur;
            pocAddRow(e);
        }
    }
    m_cntFilePath = path;
    m_cntName     = name;
    if (m_cntNomFichier) {
        QString display = path.isEmpty() ? name : QFileInfo(path).baseName();
        m_cntNomFichier->setText(display.isEmpty() ? "— aucun contour —" : display);
    }
    if (ui->editSomModelePts && m_blockMode == Contour) {
        const bool was = ui->editSomModelePts->blockSignals(true);
        ui->editSomModelePts->setText(path.isEmpty() ? name : QFileInfo(path).fileName());
        ui->editSomModelePts->blockSignals(was);
    }
    if (!m_syncing) emit dataChanged();
}


// ═══════════════════════════════════════════════════════════════
//  setupFraisageTrouTab — onglet PM Fraisage de trou
// ═══════════════════════════════════════════════════════════════
void UnitEditor::setupFraisageTrouTab()
{
    // ── Style radio vert TNC ──────────────────────────────────
    const QString rbStyle = R"(
        QRadioButton {
            color: #888; background: #222; border: 1px solid #444;
            padding: 2px 8px; border-radius: 3px; font-size: 11px;
        }
        QRadioButton::indicator { width: 0; height: 0; }
        QRadioButton:checked {
            color: #000; background: #A6CE39; border: 1px solid #A6CE39;
            font-weight: bold;
        }
    )";

    auto makeRb = [&](const QString &label) {
        auto *rb = new QRadioButton(label);
        rb->setStyleSheet(rbStyle);
        return rb;
    };

    auto rbRow = [&](std::initializer_list<QRadioButton*> rbs) {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0); l->setSpacing(3);
        for (auto *rb : rbs) l->addWidget(rb);
        l->addStretch();
        return w;
    };

    auto fieldRow = [&](QLineEdit *le, QWidget *rbWidget) {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0); l->setSpacing(6);
        l->addWidget(le, 1);
        l->addWidget(rbWidget);
        return w;
    };

    // Helper : connecter un champ au cadre AIDE (survol → helpRequested)
    auto installFieldHelp = [this](QWidget *w, const QString &key) {
        struct FieldHover : QObject {
            UnitEditor *ed; QString k;
            FieldHover(UnitEditor *e, const QString &key, QObject *p) : QObject(p), ed(e), k(key) {}
            bool eventFilter(QObject*, QEvent *ev) override {
                if (ev->type() == QEvent::Enter) emit ed->helpRequested(k);
                if (ev->type() == QEvent::Leave) emit ed->helpRequested(QString());
                return false;
            }
        };
        w->installEventFilter(new FieldHover(this, key, w));
    };

    m_ftTab = new QWidget;
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_ftTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *form = new QFormLayout(m_ftTab);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setSpacing(8);
    form->setContentsMargins(12, 12, 12, 12);

    auto secLabel = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#3a8f34;font-size:15px;font-weight:700;"
                         "letter-spacing:2px;padding-top:4px;");
        return l;
    };
    auto sepLine = []() {
        auto *f = new QFrame; f->setFrameShape(QFrame::HLine);
        f->setStyleSheet("QFrame{background:#c8ccd8;}"); return f;
    };

    // ── BROCHE (widgets gardés pour synchro Sommaire, non affichés) ──
    m_ftS     = new QLineEdit; m_ftS->setPlaceholderText("valeur");
    m_ftRbRpm = makeRb("tr/min");
    m_ftRbVc  = makeRb("Vc");
    m_ftRbRpm->setChecked(true);
    auto *bgS = new QButtonGroup(this);
    bgS->addButton(m_ftRbRpm, 0); bgS->addButton(m_ftRbVc, 1);

    m_ftRbM3 = makeRb("M3 \u21B7"); m_ftRbM4 = makeRb("M4 \u21B6");
    m_ftRbM3->setChecked(true);
    auto *bgM = new QButtonGroup(this);
    bgM->addButton(m_ftRbM3, 0); bgM->addButton(m_ftRbM4, 1);
    // Section BROCHE retirée du PM (redondante avec le Sommaire) —
    // les widgets restent en mémoire pour les connects de synchro

    // ── GÉOMÉTRIE ─────────────────────────────────────────────
    form->addRow(secLabel("GÉOMÉTRIE"));
    m_ftDiamFini   = new QLineEdit("45");  m_ftDiamFini->setPlaceholderText("mm");
    m_ftDiamAvant  = new QLineEdit("10");  m_ftDiamAvant->setPlaceholderText("mm");
    m_ftProfondeur = new QLineEdit("15");  m_ftProfondeur->setPlaceholderText("mm");
    m_ftPasHelic   = new QLineEdit("1.5"); m_ftPasHelic->setPlaceholderText("mm/tour");
    installFieldHelp(m_ftDiamFini,   "ft_diam_fini");
    installFieldHelp(m_ftDiamAvant,  "ft_diam_avant");
    installFieldHelp(m_ftProfondeur, "ft_profondeur");
    installFieldHelp(m_ftPasHelic,   "ft_pas_helico");
    form->addRow("Ø fini",         m_ftDiamFini);
    form->addRow("Ø avant-trou",   m_ftDiamAvant);
    form->addRow("Profondeur",     m_ftProfondeur);
    form->addRow("Pas hélicoïdal", m_ftPasHelic);

    form->addRow(sepLine());
    // ── FINITION ──────────────────────────────────────────────
    form->addRow(secLabel("FINITION"));

    // Mode : Fin seule / Ébauche seule / Ébauche + Finition
    // Créer les boutons Mode avec icône + texte sous l'image
    // On utilise un widget conteneur : icône (QLabel) + texte + QRadioButton invisible
    auto makeModeBtn = [&](const QString &iconPath, const QString &label) -> QRadioButton* {
        auto *rb = new QRadioButton;
        rb->setStyleSheet(
            "QRadioButton { border:0; background:transparent; }"
            "QRadioButton::indicator { width:0; height:0; }"
        );
        // Wrapper visuel cliquable
        auto *wrap = new QWidget;
        wrap->setCursor(Qt::PointingHandCursor);
        auto *lay = new QVBoxLayout(wrap);
        lay->setContentsMargins(6,6,6,6); lay->setSpacing(4);
        auto *ico = new QLabel;
        QPixmap px(iconPath);
        if (!px.isNull())
            ico->setPixmap(px.scaled(ModeUsinWidget::kIconW, ModeUsinWidget::kIconH,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation));
        ico->setAlignment(Qt::AlignCenter);
        auto *txt = new QLabel(label);
        txt->setAlignment(Qt::AlignCenter);
        txt->setStyleSheet("font-size:11pt;color:#404040;");
        lay->addWidget(ico);
        lay->addWidget(txt);
        // Style du wrapper selon état du radio
        wrap->setProperty("modeBtn", true);
        wrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        wrap->setMinimumWidth(ModeUsinWidget::kTuileMinW);
        wrap->setMaximumWidth(ModeUsinWidget::kTuileMaxW);
        wrap->setStyleSheet(
            "QWidget[modeBtn=true] { border:1px solid #808080; border-radius:4px;"
            "background:transparent;  }"
        );
        // Clic sur le wrapper → cocher le radio
        QObject::connect(rb, &QRadioButton::toggled, wrap, [wrap, txt](bool checked){
            wrap->setStyleSheet(checked
                ? "QWidget[modeBtn=true]{border:2px solid #00AA00;border-radius:4px;"
                  "background:#E0E8FF;}"
                : "QWidget[modeBtn=true]{border:1px solid #808080;border-radius:4px;"
                  "background:transparent;}");
            txt->setStyleSheet(checked
                ? "font-size:11pt;font-weight:bold;color:#004400;"
                : "font-size:11pt;color:#404040;");
        });
        // Installer event filter pour capturer le clic sur le wrap
        struct ClickFilter : QObject {
            QRadioButton *rb;
            ClickFilter(QRadioButton *r, QObject *p) : QObject(p), rb(r) {}
            bool eventFilter(QObject*, QEvent *e) override {
                if (e->type() == QEvent::MouseButtonRelease) { rb->setChecked(true); return true; }
                return false;
            }
        };
        wrap->installEventFilter(new ClickFilter(rb, rb));
        ico->installEventFilter(new ClickFilter(rb, rb));
        txt->installEventFilter(new ClickFilter(rb, rb));

        // Hover → aide contextuelle via event filter Enter
        struct HoverFilter : QObject {
            UnitEditor *editor; QString key;
            HoverFilter(UnitEditor *e, const QString &k, QObject *p)
                : QObject(p), editor(e), key(k) {}
            bool eventFilter(QObject*, QEvent *ev) override {
                if (ev->type() == QEvent::Enter)
                    emit editor->helpRequested(key);
                if (ev->type() == QEvent::Leave)
                    emit editor->helpRequested(QString());
                return false;
            }
        };
        QString helpKey = "mode_" + QString(label).toLower().replace("+","_").replace(" ","_");
        wrap->installEventFilter(new HoverFilter(this, helpKey, rb));
        rb->setParent(wrap);  // ownership
        return rb;
    };

    m_ftRbModeFin  = makeModeBtn(":/unit/finition.png",         "Finition");
    m_ftRbModeEb   = makeModeBtn(":/unit/ebauche.png",          "Ebauche");
    m_ftRbModeEbFin= makeModeBtn(":/unit/ebauche finition.png", "Eb+Fin");
    // Style avec icône visible — plus grand
    // Style identique à ModeUsinWidget (cycles poche)
    m_ftRbModeFin->setChecked(true);
    auto *bgMode = new QButtonGroup(this);
    bgMode->addButton(m_ftRbModeFin,   0);
    bgMode->addButton(m_ftRbModeEb,    1);
    bgMode->addButton(m_ftRbModeEbFin, 2);
    // Row Mode : afficher les wrappers visuels (pas les radios eux-mêmes)
    {
        auto *modeWrap = new QWidget;
        auto *modeLay  = new QHBoxLayout(modeWrap);
        modeLay->setContentsMargins(0,0,0,0); modeLay->setSpacing(8);
        // Les wrappers sont les parents des radios (créés dans makeModeBtn)
        modeLay->addWidget(m_ftRbModeFin->parentWidget(), 1);
        modeLay->addWidget(m_ftRbModeEb->parentWidget(), 1);
        modeLay->addWidget(m_ftRbModeEbFin->parentWidget(), 1);
        form->addRow("Mode", modeWrap);
    }

    m_ftSurepLat = new QLineEdit("0"); m_ftSurepLat->setPlaceholderText("mm");
    m_ftPasseFin = new QLineEdit("0"); m_ftPasseFin->setPlaceholderText("mm");
    installFieldHelp(m_ftSurepLat, "ft_surep_lat");
    installFieldHelp(m_ftPasseFin, "ft_passe_fin");
    form->addRow("Surépaisseur lat.", m_ftSurepLat);
    form->addRow("Passe finition",    m_ftPasseFin);

    // Mode descente de la passe finition (Eb+Fin uniquement)
    m_ftRbDescenteHelico = makeRb("Hélicoïdale");
    m_ftRbDescenteDirecte = makeRb("Directe");
    m_ftRbDescenteHelico->setChecked(true);
    m_ftRbDescenteHelico->setToolTip("Recommandé si la fraise a une faible longueur de coupe (LU)");
    m_ftRbDescenteDirecte->setToolTip("Possible seulement si la LU de l'outil couvre toute la profondeur");
    auto *bgDescFin = new QButtonGroup(this);
    bgDescFin->addButton(m_ftRbDescenteHelico, 0);
    bgDescFin->addButton(m_ftRbDescenteDirecte, 1);
    form->addRow("Descente finition", rbRow({m_ftRbDescenteHelico, m_ftRbDescenteDirecte}));

    // Activer surep/passe selon le mode
    auto updateFinMode = [this]{
        bool hasEb  = m_ftRbModeEb  && (m_ftRbModeEb->isChecked()  || (m_ftRbModeEbFin && m_ftRbModeEbFin->isChecked()));
        bool hasFin = m_ftRbModeEbFin && m_ftRbModeEbFin->isChecked();
        bool directe = m_ftRbDescenteDirecte && m_ftRbDescenteDirecte->isChecked();
        if (m_ftSurepLat) m_ftSurepLat->setEnabled(hasEb);
        // Passe finition inutilisée en descente directe (on utilise Z approche)
        if (m_ftPasseFin) m_ftPasseFin->setEnabled(hasFin && !directe);
        if (m_ftRbDescenteHelico)  m_ftRbDescenteHelico->setEnabled(hasFin);
        if (m_ftRbDescenteDirecte) m_ftRbDescenteDirecte->setEnabled(hasFin);
    };
    connect(m_ftRbModeFin,   &QRadioButton::toggled, this, updateFinMode);
    connect(m_ftRbModeEb,    &QRadioButton::toggled, this, updateFinMode);
    connect(m_ftRbModeEbFin, &QRadioButton::toggled, this, updateFinMode);
    connect(m_ftRbDescenteHelico,  &QRadioButton::toggled, this, updateFinMode);
    connect(m_ftRbDescenteDirecte, &QRadioButton::toggled, this, updateFinMode);
    updateFinMode();

    form->addRow(sepLine());
    // ── AVANCE ────────────────────────────────────────────────
    form->addRow(secLabel("AVANCE"));
    m_ftAvance  = new QLineEdit("0.08");
    m_ftRbFmin  = makeRb("F");
    m_ftRbFz    = makeRb("Fz");
    m_ftRbFz->setChecked(true);
    auto *bgF = new QButtonGroup(this);
    bgF->addButton(m_ftRbFmin, 0);
    bgF->addButton(m_ftRbFz,   2);
    form->addRow("Avance contour", fieldRow(m_ftAvance, rbRow({m_ftRbFmin, m_ftRbFz})));
    installFieldHelp(m_ftAvance, "avance");

    // Avance plongée — distincte de l'avance de contournage (avant :
    // dupliquée depuis m_ftAvance, impossible de mettre une plongée
    // plus prudente qu'un tour de finition).
    m_ftDescente     = new QLineEdit("0.05");
    m_ftRbFminDesc   = makeRb("F");
    m_ftRbFzDesc     = makeRb("Fz");
    m_ftRbFzDesc->setChecked(true);
    auto *bgFDesc = new QButtonGroup(this);
    bgFDesc->addButton(m_ftRbFminDesc, 0);
    bgFDesc->addButton(m_ftRbFzDesc,   2);
    form->addRow("Avance plongée", fieldRow(m_ftDescente, rbRow({m_ftRbFminDesc, m_ftRbFzDesc})));
    installFieldHelp(m_ftDescente, "avance_plongee");

    form->addRow(sepLine());
    // ── SENS ──────────────────────────────────────────────────
    form->addRow(secLabel("SENS"));
    m_ftRbAvalant    = makeRb("Avalant");
    m_ftRbOpposition = makeRb("Opposition");
    m_ftRbAvalant->setChecked(true);
    auto *bgSens = new QButtonGroup(this);
    bgSens->addButton(m_ftRbAvalant,    0);
    bgSens->addButton(m_ftRbOpposition, 1);
    form->addRow("Sens fraisage", rbRow({m_ftRbAvalant, m_ftRbOpposition}));

    form->addRow(sepLine());
    // ── SÉCURITÉS ─────────────────────────────────────────────
    form->addRow(secLabel("SÉCURITÉS"));
    m_ftApproche     = new QLineEdit("2");  m_ftApproche->setPlaceholderText("mm");
    m_ftSautBride    = new QLineEdit("50"); m_ftSautBride->setPlaceholderText("mm");
    m_ftZLiaison     = new QLineEdit("5");  m_ftZLiaison->setPlaceholderText("mm");
    m_ftAvanceLiaison= new QLineEdit();  m_ftAvanceLiaison->setPlaceholderText("mm/min");
    m_ftRbLiaisonG0  = makeRb("G0");
    m_ftRbLiaisonF   = makeRb("F");
    m_ftRbLiaisonG0->setChecked(true);
    auto *bgLiaison = new QButtonGroup(this);
    bgLiaison->addButton(m_ftRbLiaisonG0, 0);
    bgLiaison->addButton(m_ftRbLiaisonF,  1);
    // État initial appliqué explicitement (G0 par défaut) — sans ça
    // le champ restait "0" (texte non vide → placeholder invisible)
    // tant qu'on n'avait pas cliqué F puis G0 pour forcer un rafraîchissement.
    m_ftAvanceLiaison->clear();
    m_ftAvanceLiaison->setEnabled(false);
    m_ftAvanceLiaison->setPlaceholderText("G0 F-Rapide");
    m_ftAvanceLiaison->setStyleSheet("font-weight:bold;");
    connect(m_ftRbLiaisonF, &QRadioButton::toggled, this, [this](bool f){
        if (m_ftAvanceLiaison) {
            m_ftAvanceLiaison->setEnabled(f);
            m_ftAvanceLiaison->setPlaceholderText(f ? "mm/min" : "G0 F-Rapide");
            m_ftAvanceLiaison->setStyleSheet(f ? "" : "font-weight:bold;");
            if (!f) m_ftAvanceLiaison->clear();
        }
    });

    form->addRow("Approche",       m_ftApproche);
    form->addRow("Saut de bride",  m_ftSautBride);
    // Aide animée : descente de finition, sens, sécurités
    installFieldHelp(m_ftRbDescenteHelico,  "ft_desc_helico");
    installFieldHelp(m_ftRbDescenteDirecte, "ft_desc_directe");
    installFieldHelp(m_ftRbAvalant,         "fraisage_0");
    installFieldHelp(m_ftRbOpposition,      "fraisage_1");
    installFieldHelp(m_ftApproche,          "approche");
    installFieldHelp(m_ftSautBride,         "approche");
    form->addRow("Z liaison",      m_ftZLiaison);
    form->addRow("Positionnement", fieldRow(m_ftAvanceLiaison,
                                   rbRow({m_ftRbLiaisonG0, m_ftRbLiaisonF})));

    // ── Badges G ─────────────────────────────────────────────
    m_bgFtApproche = installBadgeG(m_ftApproche,
        "Z approche hérité BLK FORM",
        [this]{ if (m_ftApproche) m_ftApproche->setText(QString::number(m_heritedApproche)); });
    m_bgFtSautBride = installBadgeG(m_ftSautBride,
        "Z saut de bride hérité BLK FORM",
        [this]{ if (m_ftSautBride) m_ftSautBride->setText(QString::number(m_heritedSautBride)); });
    m_bgFtZLiaison = installBadgeG(m_ftZLiaison,
        "Z liaison hérité BLK FORM",
        [this]{ if (m_ftZLiaison) m_ftZLiaison->setText(QString::number(m_heritedZLiaison)); });
    m_bgFtAvanceLiaison = installBadgeG(m_ftAvanceLiaison,
        "Avance liaison héritée BLK FORM",
        [this]{ if (m_ftAvanceLiaison) m_ftAvanceLiaison->setText(QString::number(m_heritedAvanceLiaison)); });
    m_bgFtOrigine = nullptr; // Origine non affichée dans PM FraisageTrou

    auto redBadge = [this](BadgeG *b){ if (b) b->setHeritance(false); };
    connect(m_ftApproche,      &QLineEdit::textEdited, this, [=](const QString&){ redBadge(m_bgFtApproche); });
    connect(m_ftSautBride,     &QLineEdit::textEdited, this, [=](const QString&){ redBadge(m_bgFtSautBride); });
    connect(m_ftZLiaison,      &QLineEdit::textEdited, this, [=](const QString&){ redBadge(m_bgFtZLiaison); });
    connect(m_ftAvanceLiaison, &QLineEdit::textEdited, this, [=](const QString&){ redBadge(m_bgFtAvanceLiaison); });

    // ── SYNCHROS Sommaire ↔ PM ────────────────────────────────
    // S
    connect(ui->editSomS, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (m_ftS) m_ftS->setText(v); m_syncing = false;
    });
    connect(m_ftS, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (ui->editSomS) ui->editSomS->setText(v); m_syncing = false;
    });
    // S mode
    connect(m_ftRbVc, &QRadioButton::toggled, this, [this](bool vc){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_modeS = vc ? 1 : 0; updateBadgeS();
    });
    // M3/M4
    connect(m_ftRbM3, &QRadioButton::toggled, this, [this](bool m3){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true;
        (m3 ? ui->radioM03 : ui->radioM04)->setChecked(true);
        m_syncing = false;
    });
    connect(ui->radioM03, &QRadioButton::toggled, this, [this](bool m3){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true;
        if (m_ftRbM3) m_ftRbM3->setChecked(m3);
        if (m_ftRbM4) m_ftRbM4->setChecked(!m3);
        m_syncing = false;
    });
    // Ø fini
    connect(ui->editSomDiametre, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (m_ftDiamFini) m_ftDiamFini->setText(v); m_syncing = false;
    });
    connect(m_ftDiamFini, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (ui->editSomDiametre) ui->editSomDiametre->setText(v); m_syncing = false;
    });
    // Profondeur
    connect(ui->editSomProfondeur, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (m_ftProfondeur) m_ftProfondeur->setText(v); m_syncing = false;
    });
    connect(m_ftProfondeur, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (ui->editSomProfondeur) ui->editSomProfondeur->setText(v); m_syncing = false;
    });
    // Pas hélico
    connect(ui->editSomPasse, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (m_ftPasHelic) m_ftPasHelic->setText(v); m_syncing = false;
    });
    connect(m_ftPasHelic, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (ui->editSomPasse) ui->editSomPasse->setText(v); m_syncing = false;
    });
    // Avance
    connect(ui->editSomAvance, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (m_ftAvance) m_ftAvance->setText(v); m_syncing = false;
    });
    connect(m_ftAvance, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (ui->editSomAvance) ui->editSomAvance->setText(v); m_syncing = false;
    });
    // Mode avance radios → m_modeAvance
    connect(m_ftRbFmin, &QRadioButton::toggled, this, [this](bool v){ if (v && m_blockMode==FraisageTrou) m_modeAvance=0; });
    connect(m_ftRbFz,   &QRadioButton::toggled, this, [this](bool v){ if (v && m_blockMode==FraisageTrou) m_modeAvance=2; });

    // Avance positionnement : editSomDescente ↔ m_ftAvanceLiaison
    connect(ui->editSomDescente, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (m_ftAvanceLiaison) m_ftAvanceLiaison->setText(v); m_syncing = false;
    });
    connect(m_ftAvanceLiaison, &QLineEdit::textChanged, this, [this](const QString &v){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true; if (ui->editSomDescente) ui->editSomDescente->setText(v); m_syncing = false;
    });
    // Mode G0/F : Sommaire ↔ PM (connecté après création des widgets dans setBlockMode)
    // On connecte via m_ftRbLiaisonG0/F → m_somFtRbG0/Fpos
    connect(m_ftRbLiaisonG0, &QRadioButton::toggled, this, [this](bool g0){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true;
        if (m_somFtRbG0)   m_somFtRbG0->setChecked(g0);
        if (m_somFtRbFpos) m_somFtRbFpos->setChecked(!g0);
        m_syncing = false;
    });
    connect(m_ftRbLiaisonF, &QRadioButton::toggled, this, [this](bool f){
        if (m_blockMode != FraisageTrou || m_syncing) return;
        m_syncing = true;
        if (m_somFtRbFpos) m_somFtRbFpos->setChecked(f);
        if (m_somFtRbG0)   m_somFtRbG0->setChecked(!f);
        m_syncing = false;
    });

    // ── dataChanged ───────────────────────────────────────────
    auto ftDc = [this](){ if (!m_syncing) emit dataChanged(); };
    for (QLineEdit *le : {m_ftS, m_ftDiamFini, m_ftDiamAvant, m_ftProfondeur,
                          m_ftPasHelic, m_ftSurepLat, m_ftPasseFin, m_ftAvance,
                          m_ftDescente,
                          m_ftApproche, m_ftSautBride, m_ftZLiaison, m_ftAvanceLiaison})
        connect(le, &QLineEdit::textChanged, this, ftDc);
    for (QRadioButton *rb : {m_ftRbRpm, m_ftRbVc, m_ftRbM3, m_ftRbM4,
                              m_ftRbModeFin, m_ftRbModeEb, m_ftRbModeEbFin,
                              m_ftRbFmin, m_ftRbFz,
                              m_ftRbFminDesc, m_ftRbFzDesc,
                              m_ftRbAvalant, m_ftRbOpposition,
                              m_ftRbLiaisonG0, m_ftRbLiaisonF,
                              m_ftRbDescenteHelico, m_ftRbDescenteDirecte})
        connect(rb, &QRadioButton::toggled, this, ftDc);
    if (m_ftChkOrigine) connect(m_ftChkOrigine, &QCheckBox::toggled, this, ftDc);
    if (m_ftCbOrigine)  connect(m_ftCbOrigine, qOverload<int>(&QComboBox::currentIndexChanged), this, ftDc);

    // ── Insérer l'onglet ──────────────────────────────────────
    int insertAt = ui->tabWidget->count();
    if (m_trouTabIndex >= 0) insertAt = m_trouTabIndex + 1;
    m_fraisageTrouTabIndex = ui->tabWidget->insertTab(insertAt, scroll, "PM Fraisage trou");
    m_fraisageTrouTabW = scroll;
    refreshPmTabIndexes();
    ui->tabWidget->setTabVisible(m_fraisageTrouTabIndex, false);
}

// ═══════════════════════════════════════════════════════════════
//  load / save FraisageTrouData — VERSION COMPLÈTE
// ═══════════════════════════════════════════════════════════════
void UnitEditor::loadFraisageTrou(const FraisageTrouData &d)
{
    m_fraisageTrouData = d;
    m_syncing = true;  // bloquer dataChanged pendant le chargement

    // ── Sommaire commun ───────────────────────────────────────
    ui->editSomT->setText(d.toolRef);
    m_toolByName = d.toolByName;
    ui->editSomS->setText(QString::number(d.spindleVal, 'f', 0));
    m_modeS = d.spindleByVc ? 1 : 0;
    updateBadgeS();
    (d.brocheCW ? ui->radioM03 : ui->radioM04)->setChecked(true);
    // Positionnement (G0/F) — PAS d.descenteVal (ancien concept
    // "avance descente hélice", plus utilisé dans ce cycle). Le champ
    // editSomDescente est réutilisé ici pour le mode G0/F liaison,
    // vidé + placeholder si G0, valeur affichée si F.
    {
        bool posG0 = (d.avanceLiaison <= 0.0);
        if (m_somFtRbG0)   m_somFtRbG0->setChecked(posG0);
        if (m_somFtRbFpos) m_somFtRbFpos->setChecked(!posG0);
        ui->editSomDescente->setEnabled(!posG0);
        ui->editSomDescente->setPlaceholderText(posG0 ? "G0 F-Rapide" : "mm/min");
        ui->editSomDescente->setStyleSheet(posG0 ? "font-weight:bold;" : "");
        if (posG0) ui->editSomDescente->clear();
        else ui->editSomDescente->setText(QString::number(d.avanceLiaison, 'f', 3));
    }
    m_modeDescente = d.descenteMode;
    ui->editToolFM1->setText(d.fonctionM1);
    ui->editToolFM2->setText(d.fonctionM2);

    // Tableau de points — charge les 10 positions dans les champs
    // dédiés (page Position) et les 4 premières dans le Sommaire.
    // Vides si 0/0/0 (convention "point inactif").
    {
        auto fmtCell = [](double v) -> QString {
            return (qAbs(v) > 1e-9) ? QString::number(v, 'f', 3) : QString();
        };
        m_syncing = true;
        for (int r = 0; r < kFtPosPts; ++r) {
            double x=0, y=0, z=0;
            if (r < d.tableauPts.size()) {
                x = d.tableauPts[r].x; y = d.tableauPts[r].y; z = d.tableauPts[r].z;
            }
            if (m_ftPosX[r]) m_ftPosX[r]->setText(fmtCell(x));
            if (m_ftPosY[r]) m_ftPosY[r]->setText(fmtCell(y));
            if (m_ftPosZ[r]) m_ftPosZ[r]->setText(fmtCell(z));
            if (r < kFtSomPts) {
                if (m_somFtPX[r]) m_somFtPX[r]->setText(fmtCell(x));
                if (m_somFtPY[r]) m_somFtPY[r]->setText(fmtCell(y));
                if (m_somFtPZ[r]) m_somFtPZ[r]->setText(fmtCell(z));
            }
        }
        m_syncing = false;
    }

    // Fichier de points associé (.pnt réutilisable, comme les .cnt) —
    // DOIT être peuplé AVANT updateSommairePosition() ci-dessous, sinon
    // le label "Modèle de points" ne voit pas encore le fichier et
    // affiche le mode générique ("Point X0 Y0") au lieu du nom réel.
    if (ui->editSomModelePts) ui->editSomModelePts->setText(d.fichierPoints);
    if (ui->editPositionFichier) ui->editPositionFichier->setText(d.fichierPoints);

    // Forcer l'onglet Position sur la page dédiée FraisageTrou
    // (mode 0=Point → m_ftPositionPage via posModeToWidget, avec
    // redirection spécifique m_blockMode==FraisageTrou déjà en place)
    if (ui->stackPosition && m_ftPositionPage)
        ui->stackPosition->setCurrentWidget(posModeToWidget(d.positionMode));
    updateSommairePosition();  // icône visible dès l'ouverture du bloc, sans clic
    updateFtPointsPreview();   // aperçu graphique à jour dès l'ouverture
    // Rafraîchissement DIFFÉRÉ d'une frame — au moment de l'appel ci-
    // dessus, l'onglet Sommaire vient tout juste d'être (re)construit et
    // n'est pas encore réellement affiché à l'écran, donc le repaint()
    // immédiat ne se voit pas ; il fallait changer d'onglet et revenir
    // pour que ça se rafraîchisse. Même correctif que le selectAll()
    // différé utilisé ailleurs dans ce fichier.
    QTimer::singleShot(0, this, [this]{ updateSommairePosition(); updateFtPointsPreview(); });

    // ── Un fichier ET des champs directs ne doivent JAMAIS coexister
    //  visuellement — sinon on voit une "Grille" fantôme dans l'onglet
    //  Position alors que seul le fichier est réellement utilisé par
    //  le générateur. Si un fichier est associé : on masque tout le
    //  contenu direct (Point/Ligne/Grille/Cercle) et l'onglet Position
    //  affiche juste un message.
    const bool hasFile = !d.fichierPoints.trimmed().isEmpty();
    if (m_ftPositionPage) m_ftPositionPage->setVisible(!hasFile);
    // "Fichier de points" (au-dessus de stackPosition) : caché par
    // défaut pour FraisageTrou (Point/Tableau ont leurs propres champs,
    // Ligne/Grille/Cercle les leurs) — mais réaffiché dès qu'un fichier
    // .pnt est réellement associé, pour voir le nom ET pouvoir cliquer
    // dedans (softkeys Nouveau/Fichier/Éditer/DXF), sans repasser par
    // le Sommaire.
    if (ui->editPositionFichier) ui->editPositionFichier->setVisible(hasFile);
    if (!m_lblPositionFichierActif) {
        m_lblPositionFichierActif = new QLabel(
            "Positions gérées par le fichier de points ci-dessus.\n"
            "Clique dedans pour changer/éditer le fichier.");
        m_lblPositionFichierActif->setAlignment(Qt::AlignCenter);
        m_lblPositionFichierActif->setStyleSheet("color:#606060;font-size:11pt;padding:40px;");
        m_lblPositionFichierActif->setWordWrap(true);
        if (ui->stackPosition) ui->stackPosition->addWidget(m_lblPositionFichierActif);
    }
    if (hasFile && ui->stackPosition) {
        ui->stackPosition->setCurrentWidget(m_lblPositionFichierActif);
    }
    // Sommaire : Point 1..4 masqués aussi quand un fichier est actif
    for (int i = 0; i < kFtSomPts; ++i) {
        if (m_somFtPX[i]) {
            m_somFtPX[i]->parentWidget()->setVisible(!hasFile && m_somFtPX[i]->parentWidget()->isVisible());
        }
    }


    // ── Ligne / Grille / Cercle — champs partagés avec les autres
    //  cycles (ui->editLigneP1X, editGrilleOX, editCercleCX, etc.),
    //  jusqu'ici jamais chargés/sauvés pour FraisageTrou : seuls
    //  Point/Tableau fonctionnaient.
    if (ui->editLigneP1X) ui->editLigneP1X->setText(QString::number(d.ligneP1X,'f',3));
    if (ui->editLigneP1Y) ui->editLigneP1Y->setText(QString::number(d.ligneP1Y,'f',3));
    if (ui->editLigneP2X) ui->editLigneP2X->setText(QString::number(d.ligneP2X,'f',3));
    if (ui->editLigneP2Y) ui->editLigneP2Y->setText(QString::number(d.ligneP2Y,'f',3));
    if (ui->spinLigneNbPts) ui->spinLigneNbPts->setValue(d.ligneNbPts);

    if (ui->editGrilleOX)   ui->editGrilleOX->setText(QString::number(d.grilleOX,'f',3));
    if (ui->editGrilleOY)   ui->editGrilleOY->setText(QString::number(d.grilleOY,'f',3));
    if (ui->editGrilleOZ)   ui->editGrilleOZ->setText(QString::number(d.grilleOZ,'f',3));
    if (ui->editGrillePasX) ui->editGrillePasX->setText(QString::number(d.grillePasX,'f',3));
    if (ui->editGrillePasY) ui->editGrillePasY->setText(QString::number(d.grillePasY,'f',3));
    if (ui->spinGrilleNbX)  ui->spinGrilleNbX->setValue(d.grilleNbX);
    if (ui->spinGrilleNbY)  ui->spinGrilleNbY->setValue(d.grilleNbY);

    if (ui->editCercleCX)    ui->editCercleCX->setText(QString::number(d.cercleCX,'f',3));
    if (ui->editCercleCY)    ui->editCercleCY->setText(QString::number(d.cercleCY,'f',3));
    if (ui->editCercleZ)     ui->editCercleZ->setText(QString::number(d.cercleZ,'f',3));
    // Rayon stocké en interne, affiché en DIAMÈTRE pour FraisageTrou
    // (un plan de perçage cote quasi toujours en Ø, pas en rayon).
    if (ui->editCercleRayon) ui->editCercleRayon->setText(QString::number(d.cercleRayon * 2.0,'f',3));
    if (ui->lblCRayon)       ui->lblCRayon->setText("Diamètre");
    if (ui->spinCercleNbPts) ui->spinCercleNbPts->setValue(d.cercleNbPts);
    if (ui->editCercleAngDep)ui->editCercleAngDep->setText(QString::number(d.cercleAngDep,'f',3));
    if (ui->editCercleAngInc)ui->editCercleAngInc->setText(QString::number(d.cercleAngInc,'f',3));

    // Sommaire : Ø fini → champ Diamètre, Profondeur → champ Profondeur
    if (ui->editSomDiametre)  ui->editSomDiametre->setText(QString::number(d.diametreFini, 'f', 3));
    if (ui->editSomProfondeur)ui->editSomProfondeur->setText(QString::number(d.profondeur,  'f', 3));
    if (ui->editSomPasse)     ui->editSomPasse->setText(QString::number(d.pasHelicoidal,   'f', 3));
    if (ui->editSomAvance)    ui->editSomAvance->setText(QString::number(d.avanceVal,       'f', 3));

    // ── Onglet PM Fraisage trou ───────────────────────────────
    if (!m_ftDiamFini) return;
    m_syncing = true;
    m_ftDiamFini->setText(QString::number(d.diametreFini,  'f', 3));
    m_ftDiamAvant->setText(QString::number(d.diametreAvant,'f', 3));
    m_ftProfondeur->setText(QString::number(d.profondeur,  'f', 3));
    m_ftPasHelic->setText(QString::number(d.pasHelicoidal, 'f', 3));
    m_ftSurepLat->setText(QString::number(d.surepLat,      'f', 3));
    m_ftPasseFin->setText(QString::number(d.passeFinition, 'f', 3));
    if (m_ftDescente) m_ftDescente->setText(QString::number(d.descenteVal, 'f', 3));
    if (d.descenteMode == 0) { if (m_ftRbFminDesc) m_ftRbFminDesc->setChecked(true); }
    else                     { if (m_ftRbFzDesc)   m_ftRbFzDesc->setChecked(true);   }
    (d.spindleByVc ? m_ftRbVc : m_ftRbRpm)->setChecked(true);
    (d.brocheCW    ? m_ftRbM3 : m_ftRbM4)->setChecked(true);
    // Mode finition
    switch (d.modeFin) {
        case 1:  if (m_ftRbModeEb)    m_ftRbModeEb->setChecked(true);    break;
        case 2:  if (m_ftRbModeEbFin) m_ftRbModeEbFin->setChecked(true); break;
        default: if (m_ftRbModeFin)   m_ftRbModeFin->setChecked(true);   break;
    }
    // Mode descente finition
    (d.descenteFinDirecte ? m_ftRbDescenteDirecte : m_ftRbDescenteHelico)->setChecked(true);
    // Sens
    (d.modeFraisage == 0 ? m_ftRbAvalant : m_ftRbOpposition)->setChecked(true);
    // Avance mode
    switch (d.avanceMode) {
        case 0: if (m_ftRbFmin) m_ftRbFmin->setChecked(true); break;
        case 1: if (m_ftRbFz)   m_ftRbFz->setChecked(true);   break;  // mm/tr → Fz par défaut
        default:if (m_ftRbFz)   m_ftRbFz->setChecked(true);   break;
    }
    // Liaison mode
    (d.avanceLiaison > 0 ? m_ftRbLiaisonF : m_ftRbLiaisonG0)->setChecked(true);
    (d.modeFraisage == 0 ? m_ftRbAvalant : m_ftRbOpposition)->setChecked(true);
    m_ftApproche->setText(QString::number(d.approche,      'f', 3));
    m_ftSautBride->setText(QString::number(d.sautBride,    'f', 3));
    m_ftZLiaison->setText(QString::number(d.zLiaison,      'f', 3));
    m_ftAvanceLiaison->setText(QString::number(d.avanceLiaison,'f', 3));
    if (m_ftChkOrigine) m_ftChkOrigine->setChecked(d.origineDefinie);
    if (m_ftCbOrigine)  m_ftCbOrigine->setCurrentIndex(qBound(0, d.origine, 5));
    // Badges G
    if (m_bgFtApproche)      m_bgFtApproche->setHeritance(!d.approcheLocal);
    if (m_bgFtSautBride)     m_bgFtSautBride->setHeritance(!d.sautBrideLocal);
    if (m_bgFtZLiaison)      m_bgFtZLiaison->setHeritance(!d.zLiaisonLocal);
    if (m_bgFtAvanceLiaison) m_bgFtAvanceLiaison->setHeritance(!d.avanceLiaisonLocal);
    if (m_bgFtOrigine)       m_bgFtOrigine->setHeritance(!d.origineLocal);
    m_syncing = false;
}

FraisageTrouData UnitEditor::saveFraisageTrou() const
{
    FraisageTrouData d = m_fraisageTrouData;

    // Bloquer les signaux pendant la lecture pour éviter les boucles
    const_cast<UnitEditor*>(this)->m_syncing = true;
    struct SyncGuard { bool &b; ~SyncGuard(){ b=false; } }
        sg{const_cast<UnitEditor*>(this)->m_syncing};

    // ── Sommaire commun ───────────────────────────────────────
    d.toolRef     = ui->editSomT->text();
    d.toolByName  = m_toolByName;
    d.spindleVal  = ui->editSomS->text().toDouble();
    d.spindleByVc = (m_modeS == 1);
    d.brocheCW    = ui->radioM03->isChecked();
    d.fonctionM1  = ui->editToolFM1->text();
    d.fonctionM2  = ui->editToolFM2->text();

    // Tableau de points (10 positions) → d.tableauPts, depuis les
    // champs dédiés de la page Position.
    {
        d.tableauPts.resize(kFtPosPts);
        for (int r = 0; r < kFtPosPts; ++r) {
            auto val = [](QLineEdit *le) {
                return (le && !le->text().trimmed().isEmpty()) ? le->text().toDouble() : 0.0;
            };
            d.tableauPts[r].x = val(m_ftPosX[r]);
            d.tableauPts[r].y = val(m_ftPosY[r]);
            d.tableauPts[r].z = val(m_ftPosZ[r]);
        }
        d.posX = d.tableauPts[0].x;
        d.posY = d.tableauPts[0].y;
        d.posZ = d.tableauPts[0].z;
    }

    // Mode de position actif (Point/Ligne/Grille/Cercle/Tableau) —
    // jamais sauvegardé jusqu'ici, donc toujours relu comme 0=Point.
    if (ui->stackPosition)
        d.positionMode = posWidgetToMode(ui->stackPosition->currentWidget());
    if (ui->editSomModelePts) d.fichierPoints = ui->editSomModelePts->text();

    // Ligne / Grille / Cercle — mêmes champs partagés que les autres
    // cycles, jamais lus jusqu'ici pour FraisageTrou.
    if (ui->editLigneP1X) d.ligneP1X = ui->editLigneP1X->text().toDouble();
    if (ui->editLigneP1Y) d.ligneP1Y = ui->editLigneP1Y->text().toDouble();
    if (ui->editLigneP2X) d.ligneP2X = ui->editLigneP2X->text().toDouble();
    if (ui->editLigneP2Y) d.ligneP2Y = ui->editLigneP2Y->text().toDouble();
    if (ui->spinLigneNbPts) d.ligneNbPts = ui->spinLigneNbPts->value();

    if (ui->editGrilleOX)   d.grilleOX   = ui->editGrilleOX->text().toDouble();
    if (ui->editGrilleOY)   d.grilleOY   = ui->editGrilleOY->text().toDouble();
    if (ui->editGrilleOZ)   d.grilleOZ   = ui->editGrilleOZ->text().toDouble();
    if (ui->editGrillePasX) d.grillePasX = ui->editGrillePasX->text().toDouble();
    if (ui->editGrillePasY) d.grillePasY = ui->editGrillePasY->text().toDouble();
    if (ui->spinGrilleNbX)  d.grilleNbX  = ui->spinGrilleNbX->value();
    if (ui->spinGrilleNbY)  d.grilleNbY  = ui->spinGrilleNbY->value();

    if (ui->editCercleCX) d.cercleCX = ui->editCercleCX->text().toDouble();
    if (ui->editCercleCY) d.cercleCY = ui->editCercleCY->text().toDouble();
    if (ui->editCercleZ)  d.cercleZ  = ui->editCercleZ->text().toDouble();
    // Champ affiché en DIAMÈTRE pour FraisageTrou → converti en rayon
    // pour le stockage interne (même convention que les autres cycles).
    if (ui->editCercleRayon) d.cercleRayon = ui->editCercleRayon->text().toDouble() / 2.0;
    if (ui->spinCercleNbPts) d.cercleNbPts = ui->spinCercleNbPts->value();
    if (ui->editCercleAngDep) d.cercleAngDep = ui->editCercleAngDep->text().toDouble();
    if (ui->editCercleAngInc) d.cercleAngInc = ui->editCercleAngInc->text().toDouble();

    // ── Onglet PM Fraisage trou ───────────────────────────────
    if (!m_ftDiamFini) return d;
    d.diametreFini  = m_ftDiamFini->text().toDouble();
    d.diametreAvant = m_ftDiamAvant->text().toDouble();
    d.profondeur    = m_ftProfondeur->text().toDouble();
    d.pasHelicoidal = m_ftPasHelic->text().toDouble();
    d.surepLat      = m_ftSurepLat->text().toDouble();
    d.passeFinition = m_ftPasseFin->text().toDouble();
    d.avanceVal     = m_ftAvance ? m_ftAvance->text().toDouble() : 0;
    d.avanceMode    = m_ftRbFmin && m_ftRbFmin->isChecked() ? 0 : 2;
    // Avance de plongée hélicoïdale — désormais son propre champ,
    // indépendant de l'avance de contournage (Fix validation 07/2026).
    d.descenteVal   = m_ftDescente ? m_ftDescente->text().toDouble() : d.avanceVal;
    d.descenteMode  = (m_ftRbFminDesc && m_ftRbFminDesc->isChecked()) ? 0 : 2;
    d.modeFraisage  = m_ftRbOpposition && m_ftRbOpposition->isChecked() ? 1 : 0;
    d.modeFin       = m_ftRbModeEb     && m_ftRbModeEb->isChecked()     ? 1 :
                      m_ftRbModeEbFin  && m_ftRbModeEbFin->isChecked()  ? 2 : 0;
    d.descenteFinDirecte = m_ftRbDescenteDirecte && m_ftRbDescenteDirecte->isChecked();
    d.spindleByVc   = m_ftRbVc  && m_ftRbVc->isChecked();
    d.brocheCW      = !(m_ftRbM4 && m_ftRbM4->isChecked());
    d.avanceLiaison = m_ftAvanceLiaison ? m_ftAvanceLiaison->text().toDouble() : 0;
    d.approche      = m_ftApproche->text().toDouble();
    d.sautBride     = m_ftSautBride->text().toDouble();
    d.zLiaison      = m_ftZLiaison->text().toDouble();
    d.avanceLiaison = m_ftAvanceLiaison->text().toDouble();
    d.origineDefinie= m_ftChkOrigine ? m_ftChkOrigine->isChecked() : false;
    d.origine       = m_ftCbOrigine  ? m_ftCbOrigine->currentIndex() : 0;
    d.approcheLocal      = m_bgFtApproche      ? !m_bgFtApproche->heritance()      : false;
    d.sautBrideLocal     = m_bgFtSautBride     ? !m_bgFtSautBride->heritance()     : false;
    d.zLiaisonLocal      = m_bgFtZLiaison      ? !m_bgFtZLiaison->heritance()      : false;
    d.avanceLiaisonLocal = m_bgFtAvanceLiaison ? !m_bgFtAvanceLiaison->heritance() : false;
    d.origineLocal       = m_bgFtOrigine       ? !m_bgFtOrigine->heritance()       : false;
    return d;
}
