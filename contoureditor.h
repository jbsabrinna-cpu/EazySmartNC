#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  contoureditor.h — Éditeur de contour conversationnel EazyNC
//
//  Fonctionnement :
//    1. L'utilisateur clique sur une icône (L, CR, C, CT, CHF, RND, CC)
//    2. Les questions apparaissent une par une dans le panneau gauche
//    3. À chaque réponse validée, le dessin se met à jour en live
//    4. L'élément terminé s'ajoute à la liste en bas
//
//  Inspiré du dialogue conversationnel Heidenhain Texte clair.
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <QKeyEvent>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QLineEdit>
#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QButtonGroup>
#include "contourelement.h"

// ───────────────────────────────────────────────────────────────
//  Paramètre de dialogue — une question + sa réponse
// ───────────────────────────────────────────────────────────────
struct DialogParam
{
    QString  label;     // ex : "COORD. POINT FINAL X ?"
    QString  unit;      // ex : "mm" (affiché après le champ)
    QString  value;     // valeur saisie
    bool     optional;  // peut être ignoré avec NO ENT
    enum InputType { Number, Choice };
    InputType inputType = Number;
    QStringList choices; // pour inputType == Choice
};

// ───────────────────────────────────────────────────────────────
//  ContourEditorView — QGraphicsView avec zoom/pan
// ───────────────────────────────────────────────────────────────
class ContourEditorView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ContourEditorView(QWidget *parent = nullptr);
    void fitAll();
protected:
    void wheelEvent      (QWheelEvent *e) override;
    void mousePressEvent (QMouseEvent *e) override;
    void mouseMoveEvent  (QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
signals:
    void cursorMoved(QPointF scenePos);
    void rightClicked(QPointF scenePos);   // clic droit sans déplacement
    void elementClicked(int index);         // clic gauche → sélection
    void elementDoubleClicked(int index);   // double-clic gauche → édition
private:
    bool            m_panning  = false;
    bool            m_panMoved = false;    // a-t-on bougé pendant le pan ?
    Qt::MouseButton m_panButton = Qt::MiddleButton;
    QPoint          m_panStart;

};

// ───────────────────────────────────────────────────────────────
//  ContourEditor — widget principal
// ───────────────────────────────────────────────────────────────
class ContourEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ContourEditor(QWidget *parent = nullptr);

    // Charger un fichier .cnt existant
    void loadFile(const QString &path);

    // Définir les limites du BlkForm (pour le fond graphique)
    void setWorkArea(double xMin, double xMax,
                     double yMin, double yMax);

    // Accès au contour courant
    const ContourFile& contourFile() const { return m_contour; }

    // Sauvegarder → retourne le chemin
    bool save(const QString &path = {});

    // Nom du contour
    QString contourName() const { return m_contour.name; }
    void    setContourName(const QString &n);

signals:
    void saved(const QString &filePath);
    void contourChanged();

public slots:
    void onValidate();      // ENT — valide la réponse courante
    void onSkip();          // NO ENT — ignore le paramètre optionnel
    void onDeleteLast();    // Supprimer le dernier élément
    void onClearAll();      // Tout effacer

protected:
    void keyPressEvent(QKeyEvent *e) override;
private slots:
    void startElement(ContourElement::Type t);
    void onFieldChanged();
    void onListDoubleClicked(int row);
    void populateForm(const ContourElement &el);
    void rebuildListWidget();

private:
    // ── UI ───────────────────────────────────────────────────
    void buildUi();
    void buildToolbar();
    void applyStyle();

    // ── Dialogue ─────────────────────────────────────────────
    void buildParams(ContourElement::Type t);
    void buildForm(ContourElement::Type t);
    void clearForm();
    void showCurrentParam();
    void advanceDialog();
    void finishElement();
    void cancelElement();

    // ── Dessin ───────────────────────────────────────────────
    void rebuildScene();
    void drawWorkArea();
    void drawContour();
    void drawCurrentPreview();
    void drawGrid();
    void drawAxes();

    // ── Membres ──────────────────────────────────────────────
    ContourFile                 m_contour;
    ContourElement              m_current;       // élément en cours de saisie
    bool                        m_editing    = false;
    int                         m_editingRow = -1;   // -1=ajout, ≥0=modification

    QVector<DialogParam>        m_params;
    int                         m_paramIdx = 0;  // question courante

    double  m_xMin=-100, m_xMax=100, m_yMin=-60, m_yMax=60;

    // ── Widgets ──────────────────────────────────────────────
    QToolBar            *m_toolbar     = nullptr;
    ContourEditorView   *m_view        = nullptr;
    QGraphicsScene      *m_scene       = nullptr;

    // Panneau dialogue gauche — formulaire tout-en-un
    QWidget             *m_dlgPanel    = nullptr;
    QLabel              *m_lblElement  = nullptr;   // titre type ex: "DROITE L"
    QWidget             *m_formWidget  = nullptr;   // contient le formulaire courant
    QVBoxLayout         *m_formLay     = nullptr;   // layout du formulaire

    // Champs X/Y côte à côte
    QLineEdit           *m_fldX        = nullptr;   // X final ou X centre
    QLineEdit           *m_fldY        = nullptr;   // Y final ou Y centre
    QLineEdit           *m_fldR        = nullptr;   // Rayon (CR, RND)
    QLineEdit           *m_fldLen      = nullptr;   // Longueur chanfrein (CHF)
    // Choix DR+/DR- (C, CR)
    QPushButton         *m_btnDRplus   = nullptr;
    QPushButton         *m_btnDRminus  = nullptr;

    // Bouton ENT unique
    QPushButton         *m_btnEnt      = nullptr;

    // Dernière saisie curseur graphique
    QPointF              m_lastCursor;
    bool                 m_rebuilding = false;
    bool                 m_inPreview  = false;

    // Compatibilité (peut rester nullptr)
    QLabel              *m_lblQuestion = nullptr;
    QLineEdit           *m_fieldInput  = nullptr;
    QLabel              *m_lblUnit     = nullptr;
    QWidget             *m_choicePanel = nullptr;
    QButtonGroup        *m_choiceGroup = nullptr;
    QPushButton         *m_btnNoEnt    = nullptr;

    // Liste des éléments saisis
    QListWidget         *m_list        = nullptr;

    // Barre de statut coords
    QLabel              *m_lblCoords   = nullptr;

    // Actions toolbar
    QAction *m_actL   = nullptr;
    QAction *m_actCHF = nullptr;
    QAction *m_actRND = nullptr;
    QAction *m_actCC  = nullptr;
    QAction *m_actC   = nullptr;
    QAction *m_actCR  = nullptr;
    QAction *m_actCT  = nullptr;
    QAction *m_actDel    = nullptr;
    QAction *m_actDelSel = nullptr;   // Supprimer l'élément sélectionné
    QAction *m_actClr = nullptr;
    QAction *m_actSave= nullptr;
};
