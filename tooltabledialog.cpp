#pragma execution_character_set("utf-8")
#include "tooltabledialog.h"

#include <utility>
#include <QTableView>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QFrame>
#include <QScrollArea>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QEvent>
#include <QtMath>

// ─────────────────────────────────────────────────────────────
static const QString DIALOG_STYLE = R"(
QDialog { background: #d8dadd; }
QLabel#dlgTitle {
    font-family: 'IBM Plex Mono', monospace;
    font-size: 16px; font-weight: 700;
    color: #1a3a6a; letter-spacing: 3px;
}
QLabel#panelTitle {
    font-family: 'IBM Plex Mono', monospace;
    font-size: 12px; font-weight: 700;
    color: #1a3a6a; letter-spacing: 2px;
    padding: 6px 4px; border-bottom: 2px solid #2b6cd4;
    background: transparent;
}
QSplitter#toolSplitter::handle { background: #c2c6cc; }
QSplitter#toolSplitter::handle:hover { background: #2b6cd4; }
QLabel { color: #1c2024; font-size: 13px; background: transparent; }
QLineEdit#search {
    background: #ffffff; border: 1px solid #9aa0a8;
    border-radius: 5px; color: #102a55;
    font-family: 'IBM Plex Mono', monospace; font-size: 14px;
    padding: 8px 12px; min-height: 36px; min-width: 320px;
}
QLineEdit#search:focus { border: 2px solid #2b6cd4; }
QTableView {
    background: #f4f5f6; alternate-background-color: #e9ebee;
    color: #1c2024; border: 1px solid #a8acb2;
    font-family: 'IBM Plex Mono', monospace; font-size: 14px;
    gridline-color: #c2c6cc;
    selection-background-color: #bcd4f5;
    selection-color: #0a2a5a; outline: none;
}
QTableView::item { padding: 6px 10px; }
QHeaderView::section {
    background: #c4c8ce; color: #2a2e34;
    border: none;
    border-right: 1px solid #aab0b8;
    border-bottom: 1px solid #9aa0a8;
    font-family: 'IBM Plex Mono', monospace;
    font-size: 11px; font-weight: 700;
    letter-spacing: 1px; padding: 8px 10px;
}
QPushButton {
    background: #e4e6e9; border: 1px solid #9aa0a8;
    border-radius: 5px; color: #1c2024;
    font-family: 'IBM Plex Mono', monospace;
    font-size: 13px; font-weight: 700;
    padding: 8px 20px; min-height: 38px;
}
QPushButton:hover { background: #d2d6db; }
QPushButton:pressed { background: #c0c4ca; }
QPushButton#btnSelect {
    background: #d6ecd2; border: 1px solid #3a8f34;
    color: #246c1f; font-size: 14px;
    min-width: 160px; min-height: 44px;
}
QPushButton#btnSelect:hover { background: #c2e2bc; }
QPushButton#btnEdit {
    background: #f4f5f6; border: 1px solid #c89a30;
    color: #9a6b10; font-size: 13px;
    min-width: 120px; min-height: 38px;
}
QPushButton#btnEdit:hover   { background: #f3ecd8; border-color: #c89a30; color: #7a5510; }
QPushButton#btnEdit:checked { background: #f0e2b8; border-color: #c89a30; color: #7a5510; }
QLabel#lblInfo {
    font-size: 12px; color: #5a6068;
    font-family: 'IBM Plex Mono', monospace;
    padding: 4px 0px;
}
)";

// ═══════════════════════════════════════════════════════════════
//  Delegate : QComboBox pour la colonne Type
// ═══════════════════════════════════════════════════════════════
class TypeDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override {
        auto *cb = new QComboBox(parent);
        cb->addItems(ToolData::toolTypeNames());
        cb->setStyleSheet("background:#ffffff;color:#102a55;font-size:14px;padding:4px;");
        return cb;
    }
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override {
        if (auto *cb = qobject_cast<QComboBox*>(editor))
            cb->setCurrentIndex(idx.data(Qt::EditRole).toInt());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override {
        if (auto *cb = qobject_cast<QComboBox*>(editor))
            model->setData(idx, cb->currentIndex(), Qt::EditRole);
    }
};

// ═══════════════════════════════════════════════════════════════
//  ToolTableModel
// ═══════════════════════════════════════════════════════════════
ToolTableModel::ToolTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{ reload(); }

void ToolTableModel::reload()
{
    beginResetModel();
    m_tools.clear();
    // Ne garder que les outils non vides
    for (const ToolData &d : ToolDatabase::instance().tools())
        if (!d.isEmpty()) m_tools << d;
    sortByT();
    endResetModel();
}

void ToolTableModel::sortByT()
{
    std::sort(m_tools.begin(), m_tools.end(),
              [](const ToolData &a, const ToolData &b){ return a.T < b.T; });
}

int ToolTableModel::rowCount(const QModelIndex &) const    { return m_tools.size(); }
int ToolTableModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant ToolTableModel::headerData(int section, Qt::Orientation ori, int role) const
{
    if (role != Qt::DisplayRole) return {};
    // En-tête vertical : numérotation continue 1,2,3… (sans le saut des T machine)
    if (ori == Qt::Vertical)
        return section + 1;
    if (ori != Qt::Horizontal) return {};
    static const QStringList hdrs = {
        "T", "Nom outil", "Type",
        "Ø mm", "R mm", "Long. mm", "Lu mm",
        "R bec mm", "R ép. mm", "Angle °", "R pointe mm",
        "Dents", "DL mm", "DR mm"
    };
    return (section < hdrs.size()) ? hdrs[section] : QVariant{};
}

QVariant ToolTableModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_tools.size()) return {};
    const ToolData &d = m_tools[idx.row()];
    const int col = idx.column();

    // Colonnes lecture seule = grisées
    if (role == Qt::ForegroundRole) {
        if (col == ColT)        return QColor(0xc0, 0x70, 0x00); // orange foncé
        if (col == ColDiametre) return QColor(0x44, 0x66, 0x99); // gris-bleu (lecture seule)
        if (col == ColLong)     return QColor(0x44, 0x66, 0x99); // gris-bleu (Centroid)
        if (col == ColRoutil)   return QColor(0x1e, 0x7a, 0x18); // vert foncé
        if (col == ColDL || col == ColDR) return QColor(0x6a, 0x44, 0xb0); // violet foncé
    }

    if (role == Qt::BackgroundRole) {
        if (col == ColDiametre || col == ColLong)
            return QColor(0xe0, 0xe2, 0xe6); // fond légèrement gris = lecture seule
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == ColDescription || col == ColType)
            return QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
        return QVariant(int(Qt::AlignHCenter | Qt::AlignVCenter));
    }

    // Icône du type d'outil, affichée dans la colonne Type.
    // Le PNG est résolu via ToolData::toolTypeIcon (préfixe :/tools/).
    if (role == Qt::DecorationRole && col == ColType) {
        QPixmap px(ToolData::toolTypeIcon(d.toolType));
        if (!px.isNull())
            return px.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return {};
    }

    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    auto fmt = [](double v, int dec = 3) -> QString {
        return v > 1e-9 ? QString::number(v, 'f', dec) : QString("—");
    };

    switch (col) {
    case ColT:           return d.T;
    case ColDescription: return d.description;
    case ColType:
        return (role == Qt::EditRole)
                   ? QVariant(ToolData::toolTypeToIndex(d.toolType))
                   : QVariant(ToolData::toolTypeName(d.toolType));
    case ColDiametre:    return fmt(d.Routil * 2.0); // lecture seule
    case ColRoutil:      return fmt(d.Routil);
    case ColLong:        return fmt(d.Long, 2);      // lecture seule — valeur H Centroid
    case ColLu:          return fmt(d.Lu, 2);
    case ColRoutil2:     return fmt(d.Routil2);
    case ColREpaulement: return fmt(d.REpaulement);
    case ColAnglePointe: return d.AnglePointe > 1e-9
                   ? QString::number(d.AnglePointe,'f',1)+"°"
                   : QString("—");
    case ColRayonPointe: return d.rayonPointe > 1e-9
                   ? QString::number(d.rayonPointe,'f',3)
                   : QString("—");
    case ColNbDents:     return d.nbDents > 0 ? QVariant(d.nbDents) : QVariant("—");
    case ColDL:          return d.DL != 0.0 ? QString::number(d.DL,'f',3) : QString("");
    case ColDR:          return d.DR != 0.0 ? QString::number(d.DR,'f',3) : QString("");
    }
    return {};
}

Qt::ItemFlags ToolTableModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid()) return Qt::NoItemFlags;
    // T, Ø, Long = lecture seule
    if (idx.column() == ColT ||
        idx.column() == ColDiametre ||
        idx.column() == ColLong)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool ToolTableModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    if (!idx.isValid() || role != Qt::EditRole) return false;
    if (idx.row() >= m_tools.size()) return false;
    ToolData &d = m_tools[idx.row()];

    switch (idx.column()) {
    case ColDescription: d.description  = value.toString();  break;
    case ColType:        d.toolType     = ToolData::toolTypeFromIndex(value.toInt()); break;
    case ColRoutil: {
        d.Routil = value.toDouble();
        // Ø se met à jour
        emit dataChanged(index(idx.row(),ColDiametre), index(idx.row(),ColDiametre), {Qt::DisplayRole});
        break;
    }
    case ColLu:          d.Lu          = value.toDouble(); break;
    case ColRoutil2:     d.Routil2     = value.toDouble(); break;
    case ColREpaulement: d.REpaulement = value.toDouble(); break;
    case ColAnglePointe: d.AnglePointe = value.toDouble(); break;
    case ColRayonPointe: d.rayonPointe = value.toDouble(); break;
    case ColNbDents:     d.nbDents     = value.toInt();    break;
    case ColDL:          d.DL          = value.toDouble(); break;
    case ColDR:          d.DR          = value.toDouble(); break;
    default: return false;
    }

    emit dataChanged(idx, idx, {role});
    emit toolModified(d);
    return true;
}

void ToolTableModel::addTool()
{
    int newT = 1;
    for (; newT <= ToolDatabase::MAX_TOOLS; ++newT) {
        bool taken = false;
        for (const ToolData &d : std::as_const(m_tools))
            if (d.T == newT) { taken = true; break; }
        if (!taken) break;
    }
    beginInsertRows({}, m_tools.size(), m_tools.size());
    ToolData d;
    d.T           = newT;
    d.description = QString("T%1").arg(newT);
    d.toolType    = ToolData::Fraise4T;
    d.nbDents     = 4;
    m_tools << d;
    endInsertRows();
}

void ToolTableModel::removeTool(int row)
{
    if (row < 0 || row >= m_tools.size()) return;
    beginRemoveRows({}, row, row);
    m_tools.removeAt(row);
    endRemoveRows();
}

// ═══════════════════════════════════════════════════════════════
//  ToolTableDialog
// ═══════════════════════════════════════════════════════════════
ToolTableDialog::ToolTableDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Tableau des outils — EazyNC");
    // Force le dialog à se comporter comme une vraie fenêtre indépendante,
    // avec une taille minimale large et un état maximisé. Cela évite que la
    // fenêtre ne s'ouvre petite selon l'environnement Qt (certains styles
    // ignorent showMaximized() sur un dialog modal classique).
    setWindowFlag(Qt::Window);
    setMinimumSize(1200, 700);
    QScreen *scr = QGuiApplication::primaryScreen();
    if (scr) resize(scr->availableGeometry().size());
    setupUi();
    applyStyle();

    // Empêche la touche Entrée de "valider" automatiquement le premier
    // bouton du dialog : sinon, taper Entrée dans un champ déclenche le
    // bouton par défaut (Annuler ou Valider) et la saisie est perdue.
    // On retire cette propriété pour TOUS les QPushButton du dialog.
    for (QPushButton *b : findChildren<QPushButton*>()) {
        b->setAutoDefault(false);
        b->setDefault(false);
    }

    QTimer::singleShot(0, this, [this]{
        setWindowState(windowState() | Qt::WindowMaximized);
        showMaximized();
    });
}

void ToolTableDialog::setByName(bool byName)
{
    m_byName = byName;
    m_search->setPlaceholderText(byName
                                     ? "Rechercher par description..."
                                     : "Rechercher par numéro T ou description...");
}

void ToolTableDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    // ── Titre + recherche ─────────────────────────────────────
    auto *topRow = new QHBoxLayout;
    auto *title = new QLabel("TABLEAU DES OUTILS");
    title->setObjectName("dlgTitle");
    topRow->addWidget(title);
    topRow->addStretch();
    m_search = new QLineEdit;
    m_search->setObjectName("search");
    m_search->setPlaceholderText("Rechercher...");
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &ToolTableDialog::onSearch);
    topRow->addWidget(m_search);
    root->addLayout(topRow);

    // ── Modèle + proxy ────────────────────────────────────────
    m_model = new ToolTableModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1);

    // ── Tableau ───────────────────────────────────────────────
    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSortingEnabled(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(
        ToolTableModel::ColDescription, QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(55);
    m_table->verticalHeader()->setVisible(false);  // juste T, pas de N° continu
    m_table->verticalHeader()->setDefaultSectionSize(45); // lignes 45px
    m_table->setShowGrid(true);

    // Delegate Type
    m_table->setItemDelegateForColumn(
        ToolTableModel::ColType, new TypeDelegate(m_table));

    // Largeurs colonnes
    m_table->setColumnWidth(ToolTableModel::ColT,           55);
    m_table->setColumnWidth(ToolTableModel::ColType,       110);
    m_table->setColumnWidth(ToolTableModel::ColDiametre,    90);
    m_table->setColumnWidth(ToolTableModel::ColRoutil,      90);
    m_table->setColumnWidth(ToolTableModel::ColLong,        90);
    m_table->setColumnWidth(ToolTableModel::ColLu,          80);
    m_table->setColumnWidth(ToolTableModel::ColRoutil2,     90);
    m_table->setColumnWidth(ToolTableModel::ColREpaulement, 80);
    m_table->setColumnWidth(ToolTableModel::ColAnglePointe, 75);
    m_table->setColumnWidth(ToolTableModel::ColRayonPointe, 110);
    m_table->setColumnWidth(ToolTableModel::ColNbDents,     65);
    m_table->setColumnWidth(ToolTableModel::ColDL,          80);
    m_table->setColumnWidth(ToolTableModel::ColDR,          80);

    // ── Liste minimale ──────────────────────────────────────────────
    //  Colonnes visibles : T, Type (+icône), Nom, Longueur, Rayon.
    //  Tout le reste reste dans ToolData et s'édite dans le formulaire.
    m_table->setColumnWidth(ToolTableModel::ColDescription, 260);
    m_table->setColumnWidth(ToolTableModel::ColType,        140);
    m_table->setColumnWidth(ToolTableModel::ColLong,        110);
    m_table->setColumnWidth(ToolTableModel::ColRoutil,      110);
    for (int c : { ToolTableModel::ColDiametre,
                   ToolTableModel::ColLu,
                   ToolTableModel::ColRoutil2,
                   ToolTableModel::ColREpaulement,
                   ToolTableModel::ColAnglePointe,
                   ToolTableModel::ColRayonPointe,
                   ToolTableModel::ColNbDents,
                   ToolTableModel::ColDL,
                   ToolTableModel::ColDR })
        m_table->hideColumn(c);

    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ToolTableDialog::onSelectionChanged);
    connect(m_table, &QTableView::doubleClicked,
            this, &ToolTableDialog::onDoubleClick);
    connect(m_model, &ToolTableModel::toolModified,
            this, [](const ToolData &tool) {
                ToolDatabase::instance().writeToolToCentroid(tool);
            });

    // ── Splitter horizontal : tableau à gauche, formulaire à droite ──
    //  Layout façon TNC 620/640 : on choisit l'outil dans la liste de
    //  gauche, et ses données s'affichent/éditent dans le formulaire
    //  de droite (qui contient aussi une zone dessin avec aide visuelle).
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("toolSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(6);

    // Panneau gauche : entoure le tableau d'un titre (rappel maquette)
    auto *leftPanel = new QWidget;
    auto *leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(6);
    auto *leftTitle = new QLabel("MAGASIN OUTILS");
    leftTitle->setObjectName("panelTitle");
    leftLay->addWidget(leftTitle);
    leftLay->addWidget(m_table, 1);
    splitter->addWidget(leftPanel);

    // Panneau droit : formulaire complet
    buildFormPanel();
    splitter->addWidget(m_formPanel);

    // Proportions initiales : 55% liste / 45% formulaire
    splitter->setStretchFactor(0, 55);
    splitter->setStretchFactor(1, 45);

    root->addWidget(splitter, 1);

    // ── Info ──────────────────────────────────────────────────
    m_lblInfo = new QLabel("Sélectionnez un outil");
    m_lblInfo->setObjectName("lblInfo");
    root->addWidget(m_lblInfo);

    // ── Boutons ───────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);

    m_btnReload = new QPushButton("↺  Recharger");
    connect(m_btnReload, &QPushButton::clicked, this, &ToolTableDialog::onReload);
    btnRow->addWidget(m_btnReload);

    m_btnEdit = new QPushButton("🔒  Éditer");
    m_btnEdit->setObjectName("btnEdit");
    m_btnEdit->setCheckable(true);
    connect(m_btnEdit, &QPushButton::toggled, this, [this](bool editing) {
        if (editing) {
            m_btnEdit->setText("🔓  Verrouiller");
            m_table->setEditTriggers(
                QAbstractItemView::DoubleClicked |
                QAbstractItemView::EditKeyPressed |
                QAbstractItemView::AnyKeyPressed);
            m_btnAdd->setEnabled(true);
            m_btnRemove->setEnabled(currentSourceRow() >= 0);
            m_btnSave->setEnabled(true);
            m_lblInfo->setText("Mode édition — double-cliquez pour modifier  ·  Ø calculé depuis R automatiquement  ·  Long. = valeur Centroid (lecture seule)");
        } else {
            m_btnEdit->setText("🔒  Éditer");
            m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            m_btnAdd->setEnabled(false);
            m_btnRemove->setEnabled(false);
            onSave();
        }
    });
    btnRow->addWidget(m_btnEdit);

    m_btnAdd = new QPushButton("+  Nouveau");
    m_btnAdd->setEnabled(false);
    connect(m_btnAdd, &QPushButton::clicked, this, &ToolTableDialog::onAddTool);
    btnRow->addWidget(m_btnAdd);

    m_btnRemove = new QPushButton("−  Supprimer");
    m_btnRemove->setEnabled(false);
    connect(m_btnRemove, &QPushButton::clicked, this, &ToolTableDialog::onRemoveTool);
    btnRow->addWidget(m_btnRemove);

    m_btnSave = new QPushButton("💾  Sauvegarder");
    m_btnSave->setEnabled(false);
    connect(m_btnSave, &QPushButton::clicked, this, &ToolTableDialog::onSave);
    btnRow->addWidget(m_btnSave);

    btnRow->addStretch();

    m_btnSelect = new QPushButton("✓  Utiliser cet outil");
    m_btnSelect->setObjectName("btnSelect");
    m_btnSelect->setEnabled(false);
    connect(m_btnSelect, &QPushButton::clicked, this, [this]() {
        int row = currentSourceRow();
        if (row < 0) return;
        m_selectedTool = m_model->toolAt(row);
        m_hasSelection = true;
        emit toolSelected(m_selectedTool);
        accept();
    });
    btnRow->addWidget(m_btnSelect);

    auto *btnClose = new QPushButton("Fermer");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(btnClose);

    root->addLayout(btnRow);
}

void ToolTableDialog::applyStyle() { setStyleSheet(DIALOG_STYLE); }

int ToolTableDialog::currentSourceRow() const
{
    const auto sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty()) return -1;
    return m_proxy->mapToSource(sel.first()).row();
}

void ToolTableDialog::onSearch(const QString &text)
{ m_proxy->setFilterFixedString(text); }

void ToolTableDialog::onSelectionChanged()
{
    int row = currentSourceRow();
    bool has = (row >= 0);
    m_btnRemove->setEnabled(has && m_btnEdit->isChecked());
    m_btnSelect->setEnabled(has);

    if (!has) { m_lblInfo->setText("Sélectionnez un outil"); return; }

    const ToolData &d = m_model->toolAt(row);
    QString info = QString("T%1  ·  %2  ·  %3  ·  Ø=%4 mm  ·  R=%5 mm")
                       .arg(d.T)
                       .arg(d.description.isEmpty() ? "—" : d.description)
                       .arg(ToolData::toolTypeName(d.toolType))
                       .arg(d.Routil*2.0, 0,'f',2)
                       .arg(d.Routil,     0,'f',3);
    if (d.Long > 0)    info += QString("  ·  Long=%1 mm").arg(d.Long,0,'f',2);
    if (d.nbDents > 0) info += QString("  ·  %1 dents").arg(d.nbDents);
    if (d.DL != 0.0)   info += QString("  ·  DL=%1").arg(d.DL,0,'f',3);
    if (d.DR != 0.0)   info += QString("  ·  DR=%1").arg(d.DR,0,'f',3);
    m_lblInfo->setText(info);

    // Charge l'outil sélectionné dans le formulaire de droite.
    loadToolToForm(d);
}

void ToolTableDialog::onDoubleClick(const QModelIndex &idx)
{
    if (m_btnEdit->isChecked()) return;
    int row = m_proxy->mapToSource(idx).row();
    if (row < 0) return;
    m_selectedTool = m_model->toolAt(row);
    m_hasSelection = true;
    emit toolSelected(m_selectedTool);
    accept();
}

void ToolTableDialog::onAddTool()
{
    m_model->addTool();
    m_table->scrollToBottom();
    m_table->selectRow(m_proxy->rowCount() - 1);
}

void ToolTableDialog::onRemoveTool()
{
    int row = currentSourceRow();
    if (row < 0) return;
    const ToolData &d = m_model->toolAt(row);
    if (QMessageBox::question(this, "Supprimer",
                              QString("Supprimer T%1 — %2 ?").arg(d.T).arg(d.description),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    m_model->removeTool(row);
}

void ToolTableDialog::onSave()
{
    ToolDatabase &db = ToolDatabase::instance();
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const ToolData &d = m_model->toolAt(i);
        ToolData *td = db.ensureTool(d.T);   // nouveaux outils inclus
        if (td) *td = d;
    }
    db.save();
    m_lblInfo->setText("✓  Sauvegardé dans tools.json");
}

void ToolTableDialog::onReload()
{
    ToolDatabase::instance().load();
    m_model->reload();
    m_lblInfo->setText("✓  Rechargé");
}

// ═══════════════════════════════════════════════════════════════
//  ToolPreviewWidget — dessin dynamique de l'outil (silhouette 2D)
//
//  Équivalent du BrutPreviewWidget de la BLK FORM, mais pour les
//  outils. Dessine une vue de profil de l'outil selon ses cotes,
//  et se redessine immédiatement à chaque modification de champ.
//  Les cotes principales sont annotées dans le dessin.
// ═══════════════════════════════════════════════════════════════
ToolPreviewWidget::ToolPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background:#f8f9fa;border:2px solid #2b6cd4;border-radius:4px;");
}

void ToolPreviewWidget::setTool(ToolData::ToolType type,
                                double R, double R2,
                                double Lu, double Lcoupe,
                                double anglePointe, double rayonPointe,
                                int    nbDents)
{
    m_type        = type;
    m_R           = qMax(R, 0.1);
    m_R2          = qMax(R2, 0.0);
    m_Lu          = qMax(Lu, 0.1);
    m_Lcoupe      = qMax(Lcoupe, 0.0);
    m_anglePointe = anglePointe;
    m_rayonPointe = rayonPointe;
    m_nbDents     = nbDents;
    update();
}

void ToolPreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int W = width();
    const int H = height();
    const int margin = 30;

    // Axe vertical centré
    const int cx = W / 2;

    // Échelle : on cale la hauteur d'outil sur la hauteur dispo
    //   Hauteur outil estimée = Lu + un peu de queue (visuel)
    double hauteurOutil = m_Lu + qMax(m_Lu * 0.3, 10.0); // queue ≈ 30% de Lu
    double largeurOutil = m_R * 2.5; // un peu plus que le diamètre
    double sx = (W - 2.0 * margin) / largeurOutil;
    double sy = (H - 2.0 * margin) / hauteurOutil;
    double s  = qMin(sx, sy);
    if (s <= 0) s = 1.0;

    // Pixel du rayon outil
    int rPx = int(m_R * s);

    // Y haut = haut de la queue,  Y bas = pointe de l'outil
    int yHaut = margin;
    int yBas  = yHaut + int(hauteurOutil * s);

    // Position du bas de la zone Lu (= haut de la pointe pour foret)
    int yPointe   = yBas;
    int yHautZone = yBas - int(m_Lu * s); // haut de la partie Lu

    // ── 1. Queue cylindrique (au-dessus de Lu) ──────────────────
    QRect queue(cx - rPx, yHaut, 2*rPx, yHautZone - yHaut);
    p.setBrush(QColor(0xc8, 0xcc, 0xd2));
    p.setPen(QPen(QColor(0x2a, 0x2e, 0x34), 1.5));
    p.drawRect(queue);

    // ── 2. Partie utile / coupe selon le type d'outil ───────────
    p.setBrush(QColor(0x2a, 0x2e, 0x34));   // partie coupante = sombre

    if (m_type == ToolData::Foret) {
        // FORET : tronc cylindrique Lu - L_pointe, puis cône (tronqué si rayonPointe>0)
        double anglePointeRad = qDegreesToRadians(qMax(m_anglePointe, 1.0) / 2.0);
        double Lpointe = (m_R - m_rayonPointe) / qTan(anglePointeRad);
        if (Lpointe < 0) Lpointe = 0;
        int yColCoupe = yPointe - int(Lpointe * s);

        // Corps cylindrique de Lu jusqu'au début de la pointe
        QRect corps(cx - rPx, yHautZone, 2*rPx, yColCoupe - yHautZone);
        p.drawRect(corps);

        // Pointe (cône tronqué ou plein)
        int rPointePx = int(m_rayonPointe * s);
        QPolygon cone;
        cone << QPoint(cx - rPx,        yColCoupe)
             << QPoint(cx + rPx,        yColCoupe)
             << QPoint(cx + rPointePx,  yPointe)
             << QPoint(cx - rPointePx,  yPointe);
        p.drawPolygon(cone);
    }
    else if (m_type == ToolData::Boule) {
        // FRAISE BOULE : cylindre + demi-sphère en bout
        int yBoule = yPointe - rPx;
        QRect corps(cx - rPx, yHautZone, 2*rPx, yBoule - yHautZone);
        p.drawRect(corps);
        p.drawChord(QRect(cx - rPx, yBoule - rPx, 2*rPx, 2*rPx),
                    180 * 16, 180 * 16);
    }
    else if (m_type == ToolData::Torique && m_R2 > 0) {
        // FRAISE TORIQUE : cylindre + congé au bout (R2)
        int r2Px = int(m_R2 * s);
        int yArr = yPointe - r2Px;
        QRect corps(cx - rPx, yHautZone, 2*rPx, yArr - yHautZone);
        p.drawRect(corps);
        QPainterPath pth;
        pth.moveTo(cx - rPx, yArr);
        pth.lineTo(cx - rPx, yArr); // anchor
        pth.arcTo(cx - rPx, yArr - r2Px, 2*r2Px, 2*r2Px, 180, 90);
        pth.lineTo(cx + rPx - r2Px, yPointe);
        pth.arcTo(cx + rPx - 2*r2Px, yArr - r2Px, 2*r2Px, 2*r2Px, 270, 90);
        pth.closeSubpath();
        p.drawPath(pth);
    }
    else {
        // FRAISE classique (2T/4T/conique/autre/taraud) : cylindre plein
        QRect corps(cx - rPx, yHautZone, 2*rPx, yPointe - yHautZone);
        p.drawRect(corps);

        // Si une zone LCUTS distincte : tracer une ligne pointillée
        if (m_Lcoupe > 0 && m_Lcoupe < m_Lu) {
            int yHautCoupe = yPointe - int(m_Lcoupe * s);
            QPen pp(QColor(0xc0, 0x70, 0x00), 1, Qt::DashLine);
            p.setPen(pp);
            p.drawLine(cx - rPx - 6, yHautCoupe, cx + rPx + 6, yHautCoupe);
            p.setPen(QPen(QColor(0x2a, 0x2e, 0x34), 1.5));
        }

        // Taraud : indique des "filets" stylisés
        if (m_type == ToolData::Taraud) {
            p.setPen(QPen(QColor(0xa0, 0xa6, 0xb0), 1));
            for (int y = yHautZone + 4; y < yPointe - 2; y += 5)
                p.drawLine(cx - rPx + 2, y, cx + rPx - 2, y);
        }
    }

    // ── 3. Axe central pointillé ─────────────────────────────────
    QPen axePen(QColor(0x70, 0x76, 0x80), 1, Qt::DashDotLine);
    p.setPen(axePen);
    p.drawLine(cx, yHaut - 10, cx, yPointe + 10);

    // ── 4. Annotations cotes (textes courts à côté) ─────────────
    p.setPen(QColor(0x1a, 0x3a, 0x6a));
    QFont f = p.font();
    f.setFamily("IBM Plex Mono");
    f.setPointSize(9);
    f.setBold(true);
    p.setFont(f);

    p.drawText(cx + rPx + 8, yHautZone + (yPointe - yHautZone)/2,
               QString("Lu = %1").arg(m_Lu, 0, 'f', 1));
    p.drawText(cx - rPx - 80, yHaut + (yPointe - yHaut)/2,
               QString("R = %1").arg(m_R, 0, 'f', 2));
    if (m_nbDents > 0) {
        p.drawText(margin, yHaut - 8,
                   QString("Z = %1").arg(m_nbDents));
    }
}

// ═══════════════════════════════════════════════════════════════
//  buildFormPanel — panneau formulaire complet (façon TNC 620/640)
//
//  Layout horizontal : champs à gauche / dessin + aide à droite.
//  Sections : Identification, Cotes maîtresses (L,R), Cotes dérivées
//  (Lsp, Lu, Lcoupe, Rqueue, R2, Répaul), Géométrie spécifique
//  (angle pointe, rayon pointe, angle rampe, pitch, dents), Usure.
//
//  À droite : ToolPreviewWidget (dessin dynamique) + cadre d'aide
//  (image PNG qui change selon le champ qui a le focus).
// ═══════════════════════════════════════════════════════════════
void ToolTableDialog::buildFormPanel()
{
    m_formPanel = new QWidget;
    auto *root  = new QVBoxLayout(m_formPanel);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto *title = new QLabel("DONNÉES TECHNOLOGIQUES");
    title->setObjectName("panelTitle");
    root->addWidget(title);

    // ── Splitter horizontal : params à gauche / visuel à droite ──
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(6);

    // ╔═══════════════════════════════════════════════════════╗
    // ║  COLONNE GAUCHE : tous les champs                      ║
    // ╚═══════════════════════════════════════════════════════╝
    auto *scrollL = new QScrollArea;
    scrollL->setWidgetResizable(true);
    scrollL->setFrameShape(QFrame::NoFrame);
    auto *contentL = new QWidget;
    auto *cl = new QVBoxLayout(contentL);
    cl->setContentsMargins(10, 10, 10, 10);
    cl->setSpacing(10);

    auto secLabel = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#1a3a6a;font-size:11px;font-weight:700;"
                         "letter-spacing:2px;padding-top:6px;"
                         "border-bottom:1px solid #c2c6cc;");
        return l;
    };
    auto fieldLabel = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#5a6068;font-size:11px;font-weight:600;");
        return l;
    };

    // ── Identification (T / Nom / Type) ─────────────────────────
    cl->addWidget(secLabel("IDENTIFICATION"));
    {
        auto *gI = new QGridLayout;
        gI->setHorizontalSpacing(10);
        gI->setVerticalSpacing(4);
        gI->addWidget(fieldLabel("N° (T)"),         0, 0);
        gI->addWidget(fieldLabel("Nom de l'outil"), 0, 1);
        gI->addWidget(fieldLabel("Type"),           0, 2);
        m_fT = new QLineEdit;  m_fT->setReadOnly(true); m_fT->setMaximumWidth(80);
        m_fNom = new QLineEdit;
        m_fType = new QComboBox;
        for (const QString &n : ToolData::toolTypeNames())
            m_fType->addItem(n);
        gI->addWidget(m_fT,    1, 0);
        gI->addWidget(m_fNom,  1, 1);
        gI->addWidget(m_fType, 1, 2);
        gI->setColumnStretch(1, 2);
        gI->setColumnStretch(2, 2);
        cl->addLayout(gI);
    }

    // ── Helper : ajoute une ligne "label + champ" et installe le
    //  filtre pour mettre à jour l'icône d'aide à droite quand le
    //  champ reçoit le focus.
    auto addLine = [this, fieldLabel](QVBoxLayout *target,
                                       const QString &label,
                                       QLineEdit *&edit,
                                       const QString &helpKey)
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        auto *lbl = fieldLabel(label);
        lbl->setMinimumWidth(170);
        edit = new QLineEdit;
        edit->setProperty("helpKey", helpKey);
        edit->installEventFilter(this);
        row->addWidget(lbl);
        row->addWidget(edit, 1);
        target->addLayout(row);
    };

    // ── Cotes maîtresses ────────────────────────────────────────
    cl->addWidget(secLabel("COTES MAÎTRESSES"));
    addLine(cl, "Longueur totale (L)",  m_fLong, "L");
    addLine(cl, "Rayon (R)",            m_fR,    "R");

    // ── Cotes dérivées ──────────────────────────────────────────
    cl->addWidget(secLabel("COTES DÉRIVÉES"));
    addLine(cl, "Longueur sous pince",  m_fLsousPince,  "Lsp");
    addLine(cl, "Longueur utile (Lu)",  m_fLu,          "Lu");
    addLine(cl, "Longueur coupe LCUTS", m_fLcoupe,      "Lcoupe");
    addLine(cl, "Rayon de queue",       m_fRayonQueue,  "Rqueue");
    addLine(cl, "Rayon de bec (R2)",    m_fR2,          "R2");
    addLine(cl, "Rayon épaulement",     m_fREpaulement, "Repaul");

    // ── Géométrie spécifique ────────────────────────────────────
    cl->addWidget(secLabel("GÉOMÉTRIE SPÉCIFIQUE"));
    addLine(cl, "Angle de pointe (°)",  m_fAnglePointe, "anglePointe");
    addLine(cl, "Rayon de pointe (mm)", m_fRayonPointe, "rayonPointe");
    addLine(cl, "Angle de rampe (°)",   m_fAngleRampe,  "angleRampe");
    addLine(cl, "Pas filetage (Pitch)", m_fPitch,       "pitch");
    addLine(cl, "Nombre de dents (Z)",  m_fNbDents,     "dents");

    // ── Usure ───────────────────────────────────────────────────
    cl->addWidget(secLabel("USURE"));
    addLine(cl, "DL (delta longueur)",  m_fDL,          "DL");
    addLine(cl, "DR (delta rayon)",     m_fDR,          "DR");

    cl->addStretch(1);
    scrollL->setWidget(contentL);
    split->addWidget(scrollL);

    // ╔═══════════════════════════════════════════════════════╗
    // ║  COLONNE DROITE : dessin dynamique + cadre d'aide      ║
    // ╚═══════════════════════════════════════════════════════╝
    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 10, 10, 0);
    rl->setSpacing(10);

    // Dessin dynamique de l'outil (en haut)
    m_fPreview = new ToolPreviewWidget;
    rl->addWidget(m_fPreview, 1);

    // Cadre d'aide visuelle (image PNG qui change selon le champ)
    auto *helpFrame = new QLabel;
    helpFrame->setObjectName("helpFrame");
    helpFrame->setAlignment(Qt::AlignCenter);
    helpFrame->setMinimumHeight(240);
    helpFrame->setStyleSheet(
        "QLabel#helpFrame{border:2px dashed #c2c6cc;background:#ffffff;"
        "color:#9aa0a8;font-style:italic;border-radius:4px;}");
    helpFrame->setText("(Aide visuelle\nselon le champ sélectionné)");
    helpFrame->setProperty("isHelpFrame", true);  // marqueur pour eventFilter
    rl->addWidget(helpFrame, 1);

    split->addWidget(right);
    split->setStretchFactor(0, 60);
    split->setStretchFactor(1, 40);

    root->addWidget(split, 1);

    // ── Boutons Annuler / Valider ───────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(10, 0, 10, 8);
    btnRow->setSpacing(10);
    m_fAnnuler = new QPushButton("Annuler");
    m_fValider = new QPushButton("Valider");
    m_fValider->setObjectName("btnSelect");
    // Empêche la touche Entrée de déclencher automatiquement un bouton
    //   du dialog (sinon Entrée dans un champ "valide" et écrase la saisie).
    m_fAnnuler->setAutoDefault(false);
    m_fAnnuler->setDefault(false);
    m_fValider->setAutoDefault(false);
    m_fValider->setDefault(false);
    btnRow->addWidget(m_fAnnuler);
    btnRow->addWidget(m_fValider);
    root->addLayout(btnRow);

    // Annuler : recharger les données de l'outil sélectionné
    connect(m_fAnnuler, &QPushButton::clicked, this, [this]{
        int r = currentSourceRow();
        if (r >= 0) loadToolToForm(m_model->toolAt(r));
    });

    // Valider : écrire les valeurs du formulaire dans ToolData + save
    connect(m_fValider, &QPushButton::clicked, this, [this]{
        // On retrouve l'outil par son NUMÉRO T (stable), pas par l'index
        // de ligne : le tri (sortByT) peut avoir réordonné m_tools, donc
        // l'index n'est pas fiable, alors que le T l'est toujours.
        if (m_currentT < 1) return;
        // ensureTool : un outil créé par « Nouveau » n'existe encore que
        // dans le tableau affiché, pas dans la base -> avant, Valider ne
        // faisait rien et l'outil n'était jamais enregistré.
        ToolData *pd = ToolDatabase::instance().ensureTool(m_currentT);
        if (!pd) return;
        ToolData &d = *pd;
        d.description   = m_fNom->text();
        d.toolType      = (ToolData::ToolType)m_fType->currentIndex();
        d.Long          = m_fLong->text().toDouble();
        d.Routil        = m_fR->text().toDouble();
        d.LsousPince    = m_fLsousPince->text().toDouble();
        d.Lu            = m_fLu->text().toDouble();
        d.Lcoupe        = m_fLcoupe->text().toDouble();
        d.rayonQueue    = m_fRayonQueue->text().toDouble();
        d.Routil2       = m_fR2->text().toDouble();
        d.REpaulement   = m_fREpaulement->text().toDouble();
        d.AnglePointe   = m_fAnglePointe->text().toDouble();
        d.rayonPointe   = m_fRayonPointe->text().toDouble();
        d.angleRampe    = m_fAngleRampe->text().toDouble();
        d.pitch         = m_fPitch->text().toDouble();
        d.nbDents       = m_fNbDents->text().toInt();
        d.DL            = m_fDL->text().toDouble();
        d.DR            = m_fDR->text().toDouble();
        ToolDatabase::instance().save();
        // Repaint de toute la table (l'outil peut avoir changé de place
        // après un tri interne au save).
        m_model->reload();
        if (m_lblInfo) m_lblInfo->setText("✓  Outil T" +
                                          QString::number(m_currentT) + " enregistré");
    });

    // ── Câblage temps réel : à chaque modif de champ, on rafraîchit
    //  le dessin dynamique de l'outil (façon BrutPreviewWidget).
    auto refreshPreview = [this]{
        if (!m_fPreview) return;
        m_fPreview->setTool(
            (ToolData::ToolType)m_fType->currentIndex(),
            m_fR->text().toDouble(),
            m_fR2->text().toDouble(),
            m_fLu->text().toDouble(),
            m_fLcoupe->text().toDouble(),
            m_fAnglePointe->text().toDouble(),
            m_fRayonPointe->text().toDouble(),
            m_fNbDents->text().toInt());
    };
    for (QLineEdit *le : { m_fR, m_fR2, m_fLu, m_fLcoupe,
                           m_fAnglePointe, m_fRayonPointe, m_fNbDents })
        connect(le, &QLineEdit::textChanged, this, [refreshPreview](const QString&){
            refreshPreview();
        });
    connect(m_fType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [refreshPreview](int){ refreshPreview(); });
}

// ═══════════════════════════════════════════════════════════════
//  loadToolToForm — remplit le formulaire de droite avec les
//  données de l'outil sélectionné dans la liste, et rafraîchit
//  le dessin dynamique.
// ═══════════════════════════════════════════════════════════════
void ToolTableDialog::loadToolToForm(const ToolData &t)
{
    if (!m_formPanel) return;
    m_currentT = t.T;   // mémorise l'outil chargé (par son T, stable au tri)
    auto numOrEmpty = [](double v, int dec = 3) -> QString {
        return v > 1e-9 ? QString::number(v, 'f', dec) : QString();
    };
    m_fT->setText(QString::number(t.T));
    m_fNom->setText(t.description);
    m_fType->setCurrentIndex(int(t.toolType));

    m_fLong->setText(numOrEmpty(t.Long));
    m_fR->setText(QString::number(t.Routil, 'f', 3));

    m_fLsousPince->setText(numOrEmpty(t.LsousPince));
    m_fLu->setText(numOrEmpty(t.Lu));
    m_fLcoupe->setText(numOrEmpty(t.Lcoupe));
    m_fRayonQueue->setText(numOrEmpty(t.rayonQueue));
    m_fR2->setText(numOrEmpty(t.Routil2));
    m_fREpaulement->setText(numOrEmpty(t.REpaulement));

    m_fAnglePointe->setText(t.AnglePointe > 1e-9
                            ? QString::number(t.AnglePointe, 'f', 1) : QString());
    m_fRayonPointe->setText(numOrEmpty(t.rayonPointe));
    m_fAngleRampe->setText(QString::number(t.angleRampe, 'f', 1));
    m_fPitch->setText(numOrEmpty(t.pitch));
    m_fNbDents->setText(t.nbDents > 0 ? QString::number(t.nbDents) : QString());

    m_fDL->setText(t.DL != 0.0 ? QString::number(t.DL, 'f', 3) : QString());
    m_fDR->setText(t.DR != 0.0 ? QString::number(t.DR, 'f', 3) : QString());

    if (m_fPreview)
        m_fPreview->setTool(t.toolType, t.Routil, t.Routil2,
                            t.Lu, t.Lcoupe,
                            t.AnglePointe, t.rayonPointe, t.nbDents);
}

// ═══════════════════════════════════════════════════════════════
//  eventFilter — quand un champ du formulaire reçoit le focus,
//  on met à jour l'icône d'aide à droite (cadre helpFrame).
//  Chaque QLineEdit porte une propriété "helpKey" qui donne le
//  nom du PNG à charger (ex. helpKey="L" -> :/help/help_L.png).
// ═══════════════════════════════════════════════════════════════
// Nom de sous-dossier d'icônes d'aide selon le type d'outil.
//   Les icônes spécifiques au type vivent dans :/help/{dossier}/help_X.png
//   et une version générique de secours dans :/help/help_X.png
static QString helpTypeDir(ToolData::ToolType t)
{
    switch (t) {
    case ToolData::Fraise2T: return "fraise2t";
    case ToolData::Fraise4T: return "fraise4t";
    case ToolData::Foret:    return "foret";
    case ToolData::Torique:  return "torique";
    case ToolData::Boule:    return "boule";
    case ToolData::Conique:  return "conique";
    case ToolData::Taraud:   return "taraud";
    case ToolData::Alesoir:  return "alesoir";
    default:                 return "autre";
    }
}

bool ToolTableDialog::eventFilter(QObject *obj, QEvent *e)
{
    // On observe seulement le focus, on ne consomme aucun event :
    //   return false (et NON QDialog::eventFilter) pour que tous les
    //   events (clavier, souris) continuent leur route vers le QLineEdit.
    if (e->type() == QEvent::FocusIn) {
        QVariant v = obj->property("helpKey");
        if (v.isValid()) {
            QLabel *help = m_formPanel
                ? m_formPanel->findChild<QLabel*>("helpFrame")
                : nullptr;
            if (help) {
                QString key = v.toString();
                // 1. on tente l'icône spécifique au type d'outil courant
                ToolData::ToolType type =
                    (ToolData::ToolType)m_fType->currentIndex();
                QString dir  = helpTypeDir(type);
                QPixmap px(QString(":/help/%1/help_%2.png").arg(dir, key));
                // 2. fallback : icône générique commune
                if (px.isNull())
                    px.load(QString(":/help/help_%1.png").arg(key));

                if (!px.isNull()) {
                    help->setPixmap(px.scaled(help->size() - QSize(20,20),
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
                } else {
                    help->setPixmap(QPixmap());  // efface l'ancienne image
                    help->setText(QString("(Aide : help_%1.png\n"
                                          " à créer)").arg(key));
                }
            }
        }
    }
    return false;  // ne JAMAIS consommer — laisser passer au QLineEdit
}
