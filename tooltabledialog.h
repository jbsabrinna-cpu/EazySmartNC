#pragma once
#pragma execution_character_set("utf-8")

#include <QDialog>
#include <QTimer>
#include <QAbstractTableModel>
#include <QVector>
#include <QSortFilterProxyModel>
#include "tooldatabase.h"

// ═══════════════════════════════════════════════════════════════
//  ToolTableModel
//
//  Colonnes :
//    T | Nom | Type | Ø(mm) | R(mm) | Long. | Lu |
//    R bec | R épaulement | Angle° | Dents | DL | DR
//
//  Ø ↔ R : modifier l'un met à jour l'autre automatiquement
//  Type   : QComboBox via delegate
// ═══════════════════════════════════════════════════════════════
class ToolTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ToolTableModel(QObject *parent = nullptr);

    void reload();

    enum Column {
        ColT          = 0,
        ColDescription,
        ColType,
        ColDiametre,      // Ø = R×2 — modif auto→R
        ColRoutil,        // R rayon — modif auto→Ø
        ColLong,
        ColLu,
        ColRoutil2,
        ColREpaulement,
        ColAnglePointe,
        ColRayonPointe,
        ColNbDents,
        ColDL,
        ColDR,
        ColCount
    };

    int      rowCount   (const QModelIndex & = {}) const override;
    int      columnCount(const QModelIndex & = {}) const override;
    QVariant data       (const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    QVariant headerData (int section, Qt::Orientation ori, int role = Qt::DisplayRole) const override;
    bool     setData    (const QModelIndex &idx, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &idx) const override;

    const ToolData& toolAt(int row) const { return m_tools.at(row); }
    ToolData&       toolAt(int row)       { return m_tools[row]; }

    void addTool();
    void removeTool(int row);

signals:
    void toolModified(const ToolData &tool);

private:
    QVector<ToolData> m_tools;
    void sortByT();
};

// ═══════════════════════════════════════════════════════════════
//  ToolTableDialog
// ═══════════════════════════════════════════════════════════════
class QTableView;
class QLineEdit;
class QPushButton;
class QLabel;
class QComboBox;

// ═══════════════════════════════════════════════════════════════
//  ToolPreviewWidget — aperçu graphique temps réel de l'outil
//
//  Équivalent du BrutPreviewWidget pour la BLK FORM : dessine une
//  silhouette de l'outil (vue de profil) avec ses cotes annotées,
//  qui se redessine immédiatement quand on modifie un paramètre.
//  Le dessin s'adapte au type d'outil (foret, fraise, taraud…).
// ═══════════════════════════════════════════════════════════════
class ToolPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ToolPreviewWidget(QWidget *parent = nullptr);

    void setTool(ToolData::ToolType type,
                 double R, double R2,
                 double Lu, double Lcoupe,
                 double anglePointe, double rayonPointe,
                 int    nbDents);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    ToolData::ToolType m_type = ToolData::Autre;
    double m_R           = 5.0;
    double m_R2          = 0.0;
    double m_Lu          = 30.0;
    double m_Lcoupe      = 20.0;
    double m_anglePointe = 0.0;
    double m_rayonPointe = 0.0;
    int    m_nbDents     = 0;
};

class ToolTableDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ToolTableDialog(QWidget *parent = nullptr);

    ToolData selectedTool() const { return m_selectedTool; }
    bool     hasSelection() const { return m_hasSelection; }
    void     setByName(bool byName);

signals:
    void toolSelected(const ToolData &tool);

private slots:
    void onSearch(const QString &text);
    void onSelectionChanged();
    void onDoubleClick(const QModelIndex &idx);
    void onAddTool();
    void onRemoveTool();
    void onSave();
    void onReload();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void setupUi();
    void buildFormPanel();      // construit le panneau formulaire à droite
    void loadToolToForm(const ToolData &t);
    void applyStyle();
    int  currentSourceRow() const;

    ToolTableModel        *m_model     = nullptr;
    QSortFilterProxyModel *m_proxy     = nullptr;
    QTableView            *m_table     = nullptr;
    QLineEdit             *m_search    = nullptr;
    QPushButton           *m_btnAdd    = nullptr;
    QPushButton           *m_btnRemove = nullptr;
    QPushButton           *m_btnSave   = nullptr;
    QPushButton           *m_btnEdit   = nullptr;
    QPushButton           *m_btnReload = nullptr;
    QPushButton           *m_btnSelect = nullptr;
    QLabel                *m_lblInfo   = nullptr;

    // ── Formulaire (panneau droit, façon TNC 620/640) ──────────────
    QWidget   *m_formPanel    = nullptr;

    // En-tête
    QLineEdit *m_fT           = nullptr;
    QLineEdit *m_fNom         = nullptr;
    QComboBox *m_fType        = nullptr;

    // Dessin dynamique (en haut à droite)
    ToolPreviewWidget *m_fPreview = nullptr;

    // Cotes maîtresses
    QLineEdit *m_fLong        = nullptr;   // Longueur totale L
    QLineEdit *m_fR           = nullptr;   // Rayon R

    // Cotes dérivées
    QLineEdit *m_fLsousPince  = nullptr;
    QLineEdit *m_fLu          = nullptr;
    QLineEdit *m_fLcoupe      = nullptr;   // LCUTS
    QLineEdit *m_fRayonQueue  = nullptr;
    QLineEdit *m_fR2          = nullptr;
    QLineEdit *m_fREpaulement = nullptr;

    // Géométrie spécifique
    QLineEdit *m_fAnglePointe = nullptr;
    QLineEdit *m_fRayonPointe = nullptr;
    QLineEdit *m_fAngleRampe  = nullptr;
    QLineEdit *m_fPitch       = nullptr;
    QLineEdit *m_fNbDents     = nullptr;

    // Usure
    QLineEdit *m_fDL          = nullptr;
    QLineEdit *m_fDR          = nullptr;

    // Boutons
    QPushButton *m_fAnnuler   = nullptr;
    QPushButton *m_fValider   = nullptr;

    ToolData m_selectedTool;
    bool     m_hasSelection = false;
    bool     m_byName       = false;
    int      m_currentT     = -1;   // T de l'outil chargé dans le formulaire
};
