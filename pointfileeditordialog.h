#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pointfileeditordialog.h
//
//  Éditeur de fichier de points (.pnt) — regroupe TOUS les modèles
//  (Point/Ligne/Grille/Cercle/Tableau). On empile autant de motifs
//  que voulu dans le même fichier (ex : une grille + un cercle).
//
//  Disposition :
//   - Bas       : icônes de mode (Point/Ligne/Grille/Cercle/Tableau)
//                 — cliquer AJOUTE un motif de ce type
//   - Gauche    : liste des motifs créés (icône + résumé)
//   - Droite    : formulaire du motif sélectionné + tableau des
//                 points calculés pour ce motif
//   - Bas large : aperçu graphique combiné (tous motifs), pleine
//                 largeur, sous les colonnes gauche/droite
//
//  Mode Point = UN SEUL point X/Y/Z (pas une liste).
// ═══════════════════════════════════════════════════════════════

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <QTimer>
#include <QEvent>
#include <QFileInfo>
#include "pointfile.h"
#include "pointspreviewwidget.h"
#include "pntfilebrowserdialog.h"

class PointFileEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PointFileEditorDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Éditeur de fichier de points");
        setWindowFlags(windowFlags() | Qt::Window);
        buildUi();
    }

protected:
    void showEvent(QShowEvent *event) override {
        QDialog::showEvent(event);
        if (!m_maximizedOnce) {
            m_maximizedOnce = true;
            QTimer::singleShot(0, this, [this]{ setWindowState(windowState() | Qt::WindowMaximized); });
        }
    }

public:

    void setPointFile(const PointFile &pf)
    {
        m_file = pf;
        refreshList();
    }

    PointFile pointFile() const { return m_file; }

    bool runAndSave(const QString &suggestedDir)
    {
        m_suggestedDir = suggestedDir;
        return exec() == QDialog::Accepted;
    }

private:
    PointFile        m_file;
    QString          m_suggestedDir;
    bool             m_maximizedOnce = false;

    QListWidget         *m_list       = nullptr;
    QStackedWidget      *m_formStack  = nullptr;
    QTableWidget        *m_ptsTable   = nullptr;
    PointsPreviewWidget *m_preview    = nullptr;
    QLabel              *m_lblCount   = nullptr;

    QLineEdit *m_ptX=nullptr,*m_ptY=nullptr,*m_ptZ=nullptr;
    QLineEdit *m_lP1X=nullptr,*m_lP1Y=nullptr,*m_lPas=nullptr;
    QSpinBox  *m_lNb=nullptr;
    QLineEdit *m_gOX=nullptr,*m_gOY=nullptr,*m_gOZ=nullptr,*m_gPasX=nullptr,*m_gPasY=nullptr,*m_gAngle=nullptr;
    QSpinBox  *m_gNbX=nullptr,*m_gNbY=nullptr;
    QLineEdit *m_cCX=nullptr,*m_cCY=nullptr,*m_cZ=nullptr,*m_cDiam=nullptr,*m_cAngDep=nullptr,*m_cAngInc=nullptr;
    QSpinBox  *m_cNb=nullptr;
    QLineEdit *m_frOX=nullptr,*m_frOY=nullptr,*m_frOZ=nullptr,*m_frPasX=nullptr,*m_frPasY=nullptr,*m_frAngle=nullptr;
    QSpinBox  *m_frNbX=nullptr,*m_frNbY=nullptr;

    int m_currentEntry = -1;
    bool m_loading = false;   // vrai pendant loadEntryIntoForm() — évite la
                              // réentrance (setText() déclenche textChanged,
                              // qui rappelait onFieldEdited() AVANT que tous
                              // les champs soient chargés, mélangeant l'ancien
                              // et le nouveau motif).

    static QVector<QPointF> toQPointF(const QVector<PointEntry> &pts) {
        QVector<QPointF> out;
        for (const auto &p : pts) out << QPointF(p.x, p.y);
        return out;
    }

    QWidget* makeRow(const QString &label, QWidget *field) {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0);
        auto *lbl = new QLabel(label);
        lbl->setMinimumWidth(90);
        l->addWidget(lbl); l->addWidget(field);
        return w;
    }

    QToolButton* makeModeButton(const QString &iconPath, const QString &label, PatternEntry::Mode mode) {
        auto *btn = new QToolButton;
        btn->setText(label);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        QPixmap px(iconPath);
        if (!px.isNull()) btn->setIcon(QIcon(px));
        btn->setIconSize(QSize(48, 36));
        btn->setFixedSize(80, 64);
        btn->setStyleSheet(
            "QToolButton{border:1px solid #909090;border-radius:4px;background:#F0F0F0;}"
            "QToolButton:hover{border:2px solid #0078D4;}"
            "QToolButton:pressed{background:#D0E8FF;}");
        connect(btn, &QToolButton::clicked, this, [this, mode]{ addEntry(mode); });
        return btn;
    }

    void buildUi()
    {
        auto *mainLay = new QVBoxLayout(this);

        // ── Zone haute : liste (gauche) | formulaire+points (droite) ──
        auto *topLay = new QHBoxLayout;

        auto *leftCol = new QVBoxLayout;
        leftCol->addWidget(new QLabel("Motifs créés :"));
        m_list = new QListWidget;
        m_list->setMinimumWidth(220);
        m_list->setIconSize(QSize(28, 21));
        leftCol->addWidget(m_list);
        auto *btnDel = new QPushButton("Supprimer le motif sélectionné");
        btnDel->setAutoDefault(false); btnDel->setDefault(false);
        leftCol->addWidget(btnDel);
        topLay->addLayout(leftCol, 1);

        auto *rightCol = new QVBoxLayout;
        rightCol->addWidget(new QLabel("Paramètres du motif :"));
        m_formStack = new QStackedWidget;
        m_formStack->addWidget(buildPointForm());
        m_formStack->addWidget(buildLigneForm());
        m_formStack->addWidget(buildGrilleForm());
        m_formStack->addWidget(buildCercleForm());
        m_formStack->addWidget(new QLabel("Tableau : saisie manuelle à venir."));
        m_formStack->addWidget(buildCadreForm());
        m_formStack->setMaximumHeight(220);
        rightCol->addWidget(m_formStack);
        // Désactivé tant qu'aucun motif n'est sélectionné — sinon on
        // peut taper dans un formulaire "fantôme" qui ne correspond à
        // aucune entrée réelle (ex : après suppression du motif), et
        // tout ce qu'on tape est silencieusement perdu (onFieldEdited
        // sort immédiatement faute d'entrée à modifier).
        m_formStack->setEnabled(false);

        rightCol->addWidget(new QLabel("Points calculés pour ce motif :"));
        m_ptsTable = new QTableWidget(0, 3);
        m_ptsTable->setHorizontalHeaderLabels({"X","Y","Z"});
        m_ptsTable->horizontalHeader()->setStretchLastSection(true);
        m_ptsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        rightCol->addWidget(m_ptsTable, 1);
        topLay->addLayout(rightCol, 1);

        mainLay->addLayout(topLay, 2);

        // ── Icônes de mode (bas) — cliquer AJOUTE un motif ────────
        auto *modeRow = new QHBoxLayout;
        modeRow->addWidget(new QLabel("Ajouter :"));
        modeRow->addWidget(makeModeButton(":/unit/point.png",  "Point",  PatternEntry::ModePoint));
        modeRow->addWidget(makeModeButton(":/unit/ligne.png",  "Ligne",  PatternEntry::ModeLigne));
        modeRow->addWidget(makeModeButton(":/unit/grille.png", "Grille", PatternEntry::ModeGrille));
        modeRow->addWidget(makeModeButton(":/unit/cercle.png", "Cercle", PatternEntry::ModeCercle));
        modeRow->addWidget(makeModeButton(":/unit/cadre.png",  "Cadre",  PatternEntry::ModeCadre));
        modeRow->addStretch();
        mainLay->addLayout(modeRow);

        // ── Aperçu graphique combiné, pleine largeur ──────────────
        mainLay->addWidget(new QLabel("Aperçu (tous motifs combinés) :"));
        m_preview = new PointsPreviewWidget;
        m_preview->setMinimumHeight(200);
        mainLay->addWidget(m_preview, 1);
        m_lblCount = new QLabel;
        mainLay->addWidget(m_lblCount);

        // ── Boutons bas de dialogue ────────────────────────────────
        auto *bottomRow = new QHBoxLayout;
        auto *btnSave   = new QPushButton("Enregistrer");
        auto *btnCancel = new QPushButton("Annuler");
        // IMPORTANT : sans ça, la touche Entrée dans N'IMPORTE QUEL champ
        // (QLineEdit) déclenche automatiquement le bouton par défaut de la
        // boîte de dialogue (comportement Qt standard) — ici "Enregistrer",
        // ce qui sauvegardait/fermait le dialogue en pleine saisie, donnant
        // l'impression que tout s'effaçait.
        btnSave->setAutoDefault(false);   btnSave->setDefault(false);
        btnCancel->setAutoDefault(false); btnCancel->setDefault(false);
        bottomRow->addStretch();
        bottomRow->addWidget(btnSave);
        bottomRow->addWidget(btnCancel);
        mainLay->addLayout(bottomRow);

        connect(btnDel, &QPushButton::clicked, this, &PointFileEditorDialog::onDeleteEntry);
        connect(m_list, &QListWidget::currentRowChanged, this, &PointFileEditorDialog::onSelectEntry);
        connect(btnSave, &QPushButton::clicked, this, &PointFileEditorDialog::onSave);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

        // Sélection auto du contenu au focus — sinon il faut effacer
        // manuellement avant de retaper (comportement TNC standard).
        for (QLineEdit *le : {m_ptX,m_ptY,m_ptZ, m_lP1X,m_lP1Y,m_lPas,
                              m_gOX,m_gOY,m_gOZ,m_gPasX,m_gPasY,
                              m_cCX,m_cCY,m_cZ,m_cDiam,m_cAngDep,m_cAngInc})
            le->installEventFilter(this);
    }

    QWidget* buildPointForm() {
        auto *w = new QWidget;
        auto *lay = new QVBoxLayout(w);
        m_ptX = new QLineEdit; m_ptY = new QLineEdit; m_ptZ = new QLineEdit;
        lay->addWidget(makeRow("X", m_ptX));
        lay->addWidget(makeRow("Y", m_ptY));
        lay->addWidget(makeRow("Z", m_ptZ));
        for (QLineEdit *le : {m_ptX, m_ptY, m_ptZ})
            connect(le, &QLineEdit::textChanged, this, &PointFileEditorDialog::onFieldEdited);
        return w;
    }
    QWidget* buildLigneForm() {
        auto *w = new QWidget;
        auto *lay = new QVBoxLayout(w);
        m_lP1X = new QLineEdit; m_lP1Y = new QLineEdit;
        m_lPas = new QLineEdit;
        m_lNb  = new QSpinBox;  m_lNb->setRange(1, 999);
        m_lNb->setButtonSymbols(QAbstractSpinBox::NoButtons);
        lay->addWidget(makeRow("X départ", m_lP1X));
        lay->addWidget(makeRow("Y départ", m_lP1Y));
        lay->addWidget(makeRow("Pas",      m_lPas));
        lay->addWidget(makeRow("Nb points",m_lNb));
        for (QLineEdit *le : {m_lP1X, m_lP1Y, m_lPas})
            connect(le, &QLineEdit::textChanged, this, &PointFileEditorDialog::onFieldEdited);
        connect(m_lNb, qOverload<int>(&QSpinBox::valueChanged), this, &PointFileEditorDialog::onFieldEdited);
        return w;
    }
    QWidget* buildGrilleForm() {
        auto *w = new QWidget;
        auto *lay = new QVBoxLayout(w);
        m_gOX = new QLineEdit; m_gOY = new QLineEdit; m_gOZ = new QLineEdit;
        m_gPasX = new QLineEdit; m_gPasY = new QLineEdit;
        m_gAngle = new QLineEdit;
        m_gNbX = new QSpinBox; m_gNbX->setRange(1, 999); m_gNbX->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_gNbY = new QSpinBox; m_gNbY->setRange(1, 999); m_gNbY->setButtonSymbols(QAbstractSpinBox::NoButtons);
        lay->addWidget(makeRow("X départ", m_gOX));
        lay->addWidget(makeRow("Y départ", m_gOY));
        lay->addWidget(makeRow("Z départ", m_gOZ));
        lay->addWidget(makeRow("Pas X",    m_gPasX));
        lay->addWidget(makeRow("Pas Y",    m_gPasY));
        lay->addWidget(makeRow("Nb colonnes", m_gNbX));
        lay->addWidget(makeRow("Nb lignes",   m_gNbY));
        lay->addWidget(makeRow("Inclinaison (°)", m_gAngle));
        for (QLineEdit *le : {m_gOX, m_gOY, m_gOZ, m_gPasX, m_gPasY, m_gAngle})
            connect(le, &QLineEdit::textChanged, this, &PointFileEditorDialog::onFieldEdited);
        connect(m_gNbX, qOverload<int>(&QSpinBox::valueChanged), this, &PointFileEditorDialog::onFieldEdited);
        connect(m_gNbY, qOverload<int>(&QSpinBox::valueChanged), this, &PointFileEditorDialog::onFieldEdited);
        return w;
    }
    QWidget* buildCadreForm() {
        auto *w = new QWidget;
        auto *lay = new QVBoxLayout(w);
        m_frOX = new QLineEdit; m_frOY = new QLineEdit; m_frOZ = new QLineEdit;
        m_frPasX = new QLineEdit; m_frPasY = new QLineEdit;
        m_frAngle = new QLineEdit;
        m_frNbX = new QSpinBox; m_frNbX->setRange(2, 999); m_frNbX->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_frNbY = new QSpinBox; m_frNbY->setRange(2, 999); m_frNbY->setButtonSymbols(QAbstractSpinBox::NoButtons);
        lay->addWidget(makeRow("X départ", m_frOX));
        lay->addWidget(makeRow("Y départ", m_frOY));
        lay->addWidget(makeRow("Z départ", m_frOZ));
        lay->addWidget(makeRow("Pas X",    m_frPasX));
        lay->addWidget(makeRow("Pas Y",    m_frPasY));
        lay->addWidget(makeRow("Nb points côté X", m_frNbX));
        lay->addWidget(makeRow("Nb points côté Y", m_frNbY));
        lay->addWidget(makeRow("Inclinaison (°)",  m_frAngle));
        lay->addWidget(new QLabel("Points sur le pourtour uniquement\n(coins inclus, intérieur vide)."));
        for (QLineEdit *le : {m_frOX, m_frOY, m_frOZ, m_frPasX, m_frPasY, m_frAngle})
            connect(le, &QLineEdit::textChanged, this, &PointFileEditorDialog::onFieldEdited);
        connect(m_frNbX, qOverload<int>(&QSpinBox::valueChanged), this, &PointFileEditorDialog::onFieldEdited);
        connect(m_frNbY, qOverload<int>(&QSpinBox::valueChanged), this, &PointFileEditorDialog::onFieldEdited);
        return w;
    }
    QWidget* buildCercleForm() {
        auto *w = new QWidget;
        auto *lay = new QVBoxLayout(w);
        m_cCX = new QLineEdit; m_cCY = new QLineEdit; m_cZ = new QLineEdit;
        m_cDiam = new QLineEdit;
        m_cAngDep = new QLineEdit; m_cAngInc = new QLineEdit;
        m_cNb = new QSpinBox; m_cNb->setRange(1, 360); m_cNb->setButtonSymbols(QAbstractSpinBox::NoButtons);
        lay->addWidget(makeRow("Centre X",  m_cCX));
        lay->addWidget(makeRow("Centre Y",  m_cCY));
        lay->addWidget(makeRow("Z",         m_cZ));
        lay->addWidget(makeRow("Diamètre",  m_cDiam));
        lay->addWidget(makeRow("Nb points", m_cNb));
        lay->addWidget(makeRow("Angle départ", m_cAngDep));
        lay->addWidget(makeRow("Incrément (°)",m_cAngInc));
        for (QLineEdit *le : {m_cCX, m_cCY, m_cZ, m_cDiam, m_cAngDep, m_cAngInc})
            connect(le, &QLineEdit::textChanged, this, &PointFileEditorDialog::onFieldEdited);
        connect(m_cNb, qOverload<int>(&QSpinBox::valueChanged), this, &PointFileEditorDialog::onFieldEdited);
        return w;
    }

    void refreshList() {
        m_list->blockSignals(true);
        m_list->clear();
        for (const auto &e : m_file.entries) {
            auto *it = new QListWidgetItem(e.label());
            QPixmap px(PatternEntry::modeIcon(e.mode));
            if (!px.isNull()) it->setIcon(QIcon(px));
            m_list->addItem(it);
        }
        m_list->blockSignals(false);
        if (!m_file.entries.isEmpty()) m_list->setCurrentRow(m_list->count()-1);
        else { m_currentEntry = -1; m_ptsTable->setRowCount(0); m_formStack->setEnabled(false); clearForms(); }
        updateCount();
    }

    void updateCount() {
        m_file.generate();
        int n = m_file.points.size();
        m_lblCount->setText(QString("%1 position%2 au total (tous motifs)").arg(n).arg(n>1?"s":""));
        m_preview->setPoints(toQPointF(m_file.points));
    }

    // Vide RÉELLEMENT tous les champs (toutes pages confondues) — pas
    // seulement griser. Sinon d'anciens chiffres restent affichés sur
    // un formulaire désactivé, ce qui laisse croire qu'ils sont encore
    // valides. Appelé chaque fois qu'il n'y a plus de motif sélectionné.
    void clearForms() {
        m_loading = true;
        for (QLineEdit *le : {m_ptX, m_ptY, m_ptZ,
                              m_lP1X, m_lP1Y, m_lPas,
                              m_gOX, m_gOY, m_gOZ, m_gPasX, m_gPasY, m_gAngle,
                              m_cCX, m_cCY, m_cZ, m_cDiam, m_cAngDep, m_cAngInc,
                              m_frOX, m_frOY, m_frOZ, m_frPasX, m_frPasY, m_frAngle})
            if (le) le->clear();
        for (QSpinBox *sb : {m_lNb, m_gNbX, m_gNbY, m_cNb, m_frNbX, m_frNbY})
            if (sb) sb->setValue(sb->minimum());
        m_loading = false;
    }

    void updatePointsTable(int idx) {
        m_ptsTable->setRowCount(0);
        if (idx < 0 || idx >= m_file.entries.size()) return;
        QVector<PointEntry> pts = m_file.entries[idx].generate();
        m_ptsTable->setRowCount(pts.size());
        for (int i = 0; i < pts.size(); ++i) {
            m_ptsTable->setItem(i, 0, new QTableWidgetItem(QString::number(pts[i].x, 'f', 3)));
            m_ptsTable->setItem(i, 1, new QTableWidgetItem(QString::number(pts[i].y, 'f', 3)));
            m_ptsTable->setItem(i, 2, new QTableWidgetItem(QString::number(pts[i].z, 'f', 3)));
        }
    }

    void loadEntryIntoForm(int idx) {
        if (idx < 0 || idx >= m_file.entries.size()) return;
        m_loading = true;
        const PatternEntry &e = m_file.entries[idx];
        m_formStack->setCurrentIndex((int)e.mode);
        auto setT = [](QLineEdit *le, double v){ if (le) le->setText(QString::number(v,'f',3)); };
        switch (e.mode) {
        case PatternEntry::ModePoint:
            setT(m_ptX, e.ptX); setT(m_ptY, e.ptY); setT(m_ptZ, e.ptZ);
            break;
        case PatternEntry::ModeLigne:
            setT(m_lP1X, e.ligneP1X); setT(m_lP1Y, e.ligneP1Y);
            setT(m_lPas, e.lignePas);
            m_lNb->setValue(e.ligneNb);
            break;
        case PatternEntry::ModeGrille:
            setT(m_gOX, e.grilleOX); setT(m_gOY, e.grilleOY); setT(m_gOZ, e.grilleOZ);
            setT(m_gPasX, e.grillePasX); setT(m_gPasY, e.grillePasY);
            m_gNbX->setValue(e.grilleNbX); m_gNbY->setValue(e.grilleNbY);
            setT(m_gAngle, e.grilleAngle);
            break;
        case PatternEntry::ModeCercle:
            setT(m_cCX, e.cercleCX); setT(m_cCY, e.cercleCY); setT(m_cZ, e.cercleZ);
            setT(m_cDiam, e.cercleRayon * 2.0);
            setT(m_cAngDep, e.cercleAngDep); setT(m_cAngInc, e.cercleAngInc);
            m_cNb->setValue(e.cercleNb);
            break;
        case PatternEntry::ModeTableau:
            break;
        case PatternEntry::ModeCadre:
            setT(m_frOX, e.cadreOX); setT(m_frOY, e.cadreOY); setT(m_frOZ, e.cadreOZ);
            setT(m_frPasX, e.cadrePasX); setT(m_frPasY, e.cadrePasY);
            m_frNbX->setValue(e.cadreNbX); m_frNbY->setValue(e.cadreNbY);
            setT(m_frAngle, e.cadreAngle);
            break;
        }
        m_loading = false;
    }

    void addEntry(PatternEntry::Mode mode) {
        PatternEntry e;
        e.mode = mode;
        m_file.entries << e;
        refreshList();
        // Fixer explicitement l'entrée courante ET charger son
        // formulaire tout de suite — sans attendre que le signal
        // currentRowChanged() daigne se déclencher. Avant ce correctif,
        // taper dans le champ juste après "+Cercle" n'avait AUCUN effet
        // tant qu'on n'avait pas cliqué sur la ligne dans la liste.
        int newRow = m_file.entries.size() - 1;
        m_currentEntry = newRow;
        m_formStack->setEnabled(true);
        loadEntryIntoForm(newRow);
        updatePointsTable(newRow);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::FocusIn) {
            if (auto *le = qobject_cast<QLineEdit*>(obj))
                QTimer::singleShot(0, le, [le]{ le->selectAll(); });
        }
        return QDialog::eventFilter(obj, event);
    }

private slots:
    void onDeleteEntry() {
        int row = m_list->currentRow();
        if (row < 0 || row >= m_file.entries.size()) return;
        m_file.entries.remove(row);
        refreshList();
    }

    void onSelectEntry(int row) {
        m_currentEntry = row;
        if (row < 0) { m_ptsTable->setRowCount(0); m_formStack->setEnabled(false); clearForms(); return; }
        m_formStack->setEnabled(true);
        loadEntryIntoForm(row);
        updatePointsTable(row);
        updateCount();
    }

    void onFieldEdited() {
        // Ignore les appels réentrants déclenchés par setText() PENDANT
        // le chargement d'un motif (loadEntryIntoForm) ou son vidage
        // (clearForms) — sinon on lit un mélange de champs pas encore
        // à jour et on écrase le motif avec des valeurs incohérentes.
        if (m_loading) return;
        // Lit la ligne RÉELLEMENT sélectionnée dans la liste plutôt que
        // de se fier à m_currentEntry seul — évite tout désync possible
        // entre la liste et le formulaire (cause du bug "il faut cliquer
        // à côté pour que ça marche").
        int idx = m_list->currentRow();
        if (idx < 0) idx = m_currentEntry;
        if (idx < 0 || idx >= m_file.entries.size()) return;
        m_currentEntry = idx;
        PatternEntry &e = m_file.entries[idx];
        auto val = [](QLineEdit *le){ return le ? le->text().toDouble() : 0.0; };
        switch (e.mode) {
        case PatternEntry::ModePoint:
            e.ptX = val(m_ptX); e.ptY = val(m_ptY); e.ptZ = val(m_ptZ);
            break;
        case PatternEntry::ModeLigne:
            e.ligneP1X = val(m_lP1X); e.ligneP1Y = val(m_lP1Y);
            e.lignePas = val(m_lPas);
            e.ligneNb  = m_lNb->value();
            break;
        case PatternEntry::ModeGrille:
            e.grilleOX = val(m_gOX); e.grilleOY = val(m_gOY); e.grilleOZ = val(m_gOZ);
            e.grillePasX = val(m_gPasX); e.grillePasY = val(m_gPasY);
            e.grilleNbX = m_gNbX->value(); e.grilleNbY = m_gNbY->value();
            e.grilleAngle = val(m_gAngle);
            break;
        case PatternEntry::ModeCercle:
            e.cercleCX = val(m_cCX); e.cercleCY = val(m_cCY); e.cercleZ = val(m_cZ);
            e.cercleRayon = val(m_cDiam) / 2.0;
            e.cercleAngDep = val(m_cAngDep); e.cercleAngInc = val(m_cAngInc);
            e.cercleNb = m_cNb->value();
            break;
        case PatternEntry::ModeTableau:
            break;
        case PatternEntry::ModeCadre:
            e.cadreOX = val(m_frOX); e.cadreOY = val(m_frOY); e.cadreOZ = val(m_frOZ);
            e.cadrePasX = val(m_frPasX); e.cadrePasY = val(m_frPasY);
            e.cadreNbX = m_frNbX->value(); e.cadreNbY = m_frNbY->value();
            e.cadreAngle = val(m_frAngle);
            break;
        }
        m_list->item(m_currentEntry)->setText(e.label());
        updatePointsTable(m_currentEntry);
        updateCount();
    }

    void onSave() {
        if (m_file.entries.isEmpty()) {
            QMessageBox::warning(this, "Fichier de points", "Ajoutez au moins un motif avant d'enregistrer.");
            return;
        }
        QString path = m_file.filePath;
        if (path.isEmpty()) {
            PntFileBrowserDialog browserDlg(this);
            path = browserDlg.chooseNewName(m_suggestedDir);
            if (path.isEmpty()) return;
            m_file.name = QFileInfo(path).completeBaseName();
        }
        if (!m_file.save(path)) {
            QMessageBox::warning(this, "Fichier de points", "Impossible d'écrire : " + path);
            return;
        }
        accept();
    }
};
