#pragma execution_character_set("utf-8")
// ================================================================
//  filebrowserpage.cpp  -  Gestionnaire fichiers style PGM MGT TNC 620
// ================================================================
#include "cadviewwidget.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QFileInfo>
#include "filebrowserpage.h"
#include "cadview3d.h"
#include "decoupe3d.h"
#include <QDoubleSpinBox>
#include <QLabel>
#include <QApplication>
#include <memory>
#include "cadimporter.h"
#include <QStackedWidget>
#include <QButtonGroup>
#include <QPushButton>
#include "ui_FileBrowserPage.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QDateTime>
#include <QTimer>
#include <QTime>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>
#include <QFile>
#include <QTextStream>

// ================================================================
//  Construction
// ================================================================
FileBrowserPage::FileBrowserPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FileBrowserPage)
{
    ui->setupUi(this);

    // Horloge
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &FileBrowserPage::updateClock);
    m_clockTimer->start(10000);
    updateClock();

    // Arbre gauche
    connect(ui->treeDir, &QTreeWidget::itemSelectionChanged,
            this, &FileBrowserPage::onDirSelectionChanged);
    connect(ui->treeDir, &QTreeWidget::itemExpanded,
            this, &FileBrowserPage::onDirItemExpanded);

    // Liste droite
    connect(ui->treeFiles, &QTreeWidget::itemSelectionChanged,
            this, &FileBrowserPage::onFileSelectionChanged);
    connect(ui->treeFiles, &QTreeWidget::itemDoubleClicked,
            this, &FileBrowserPage::onFileDoubleClicked);

    // Softkeys
    connect(ui->skOpen,       &QPushButton::clicked, this, &FileBrowserPage::onOpen);
    connect(ui->skNewFile,    &QPushButton::clicked, this, &FileBrowserPage::onNewFile);
    connect(ui->skNewDir,     &QPushButton::clicked, this, &FileBrowserPage::onNewDir);
    connect(ui->skRename,     &QPushButton::clicked, this, &FileBrowserPage::onRename);
    connect(ui->skDelete,     &QPushButton::clicked, this, &FileBrowserPage::onDelete);
    connect(ui->skCopy,       &QPushButton::clicked, this, &FileBrowserPage::onCopy);
    connect(ui->skSelectType, &QPushButton::clicked, this, &FileBrowserPage::onSelectType);
    connect(ui->skPageUp,     &QPushButton::clicked, this, [this]{
        ui->treeFiles->verticalScrollBar()->triggerAction(QScrollBar::SliderPageStepSub);
    });
    connect(ui->skPageDown,   &QPushButton::clicked, this, [this]{
        ui->treeFiles->verticalScrollBar()->triggerAction(QScrollBar::SliderPageStepAdd);
    });

    // Taille icones
    ui->treeFiles->setIconSize(QSize(28, 28));
    ui->treeDir->setIconSize(QSize(28, 28));

    // Largeurs colonnes
    ui->treeFiles->setColumnWidth(0, 260);
    ui->treeFiles->setColumnWidth(1,  55);
    ui->treeFiles->setColumnWidth(2,  70);
    ui->treeFiles->setColumnWidth(3, 100);
    ui->treeFiles->setColumnWidth(4,  60);

    // Preview vide au depart
    // Creer FilePreviewWidget dans panelPreview
    if (ui->panelPreview) {
        auto *lay = new QVBoxLayout(ui->panelPreview);
        lay->setContentsMargins(0,0,0,0);
        lay->setSpacing(0);
        m_preview = new FilePreviewWidget(ui->panelPreview);
        lay->addWidget(m_preview);
    }
}

FileBrowserPage::~FileBrowserPage()
{
    delete ui;
}

// ================================================================
//  Initialisation
// ================================================================
void FileBrowserPage::setRootPath(const QString &path)
{
    m_rootPath       = path;
    m_currentDirPath = path;
    QDir().mkpath(path);
    updatePathBar(path);
    buildDirTree();
    loadFilesForDir(path);
}

QString FileBrowserPage::selectedFilePath() const
{
    const auto sel = ui->treeFiles->selectedItems();
    if (sel.isEmpty()) return {};
    return sel.first()->data(0, Qt::UserRole).toString();
}

EazyFileType FileBrowserPage::selectedFileType() const
{
    const auto sel = ui->treeFiles->selectedItems();
    if (sel.isEmpty()) return EazyFileType::Unknown;
    return (EazyFileType)sel.first()->data(0, Qt::UserRole + 1).toInt();
}

// ================================================================
//  Arbre gauche
// ================================================================
void FileBrowserPage::buildDirTree()
{
    ui->treeDir->clear();
    QDir root(m_rootPath);
    if (!root.exists()) return;

    QTreeWidgetItem *rootItem = new QTreeWidgetItem(ui->treeDir);
    rootItem->setText(0, root.dirName().isEmpty() ? m_rootPath : root.dirName().toUpper());
    rootItem->setData(0, Qt::UserRole, m_rootPath);
    rootItem->setIcon(0, iconForDir());
    rootItem->setExpanded(true);

    populateDir(rootItem, m_rootPath);

    // Autres emplacements (comme TNC: / USB sur la commande) : bureau,
    // documents, téléchargements, puis tous les disques et clés USB.
    // Leurs sous-dossiers ne sont lus qu'à l'ouverture (rapide).
    auto lecteur = [this](const QString &libelle, const QString &chemin) {
        if (chemin.isEmpty() || !QFileInfo(chemin).isDir()) return;
        auto *it = new QTreeWidgetItem(ui->treeDir);
        it->setText(0, libelle);
        it->setData(0, Qt::UserRole, QDir::cleanPath(chemin));
        it->setIcon(0, iconForDir());
        auto *vide = new QTreeWidgetItem(it);           // pour la flèche d'ouverture
        vide->setData(0, Qt::UserRole + 2, true);
    };
    lecteur(QString::fromUtf8("BUREAU"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    lecteur(QString::fromUtf8("DOCUMENTS"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    lecteur(QString::fromUtf8("TÉLÉCHARGEMENTS"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady()) continue;
        const QString r = v.rootPath();
        const QString nom = v.displayName() == r ? QString() : v.displayName();
        lecteur(QString("%1  %2").arg(r.left(2), nom).trimmed(), r);
    }
    ui->treeDir->setCurrentItem(rootItem);
}

void FileBrowserPage::populateDir(QTreeWidgetItem *parent, const QString &dirPath)
{
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &fi : entries) {
        QTreeWidgetItem *item = new QTreeWidgetItem(parent);
        item->setText(0, fi.fileName().toUpper());
        item->setData(0, Qt::UserRole, fi.absoluteFilePath());
        item->setIcon(0, iconForDir());
        populateDir(item, fi.absoluteFilePath());
    }
}

// ================================================================
//  Liste fichiers droite
// ================================================================
void FileBrowserPage::loadFilesForDir(const QString &dirPath)
{
    ui->treeFiles->clear();
    m_currentDirPath = dirPath;
    updatePathBar(dirPath);

    QDir dir(dirPath);

    // Sous-dossiers d'abord
    const QFileInfoList dirs = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : dirs) {
        QTreeWidgetItem *item = new QTreeWidgetItem(ui->treeFiles);
        item->setText(0, fi.fileName().toUpper());
        item->setText(1, "DIR");
        item->setText(2, "");
        item->setText(3, fi.lastModified().toString("dd.MM.yyyy"));
        item->setText(4, fi.lastModified().toString("HH:mm"));
        item->setData(0, Qt::UserRole,     fi.absoluteFilePath());
        item->setData(0, Qt::UserRole + 1, (int)EazyFileType::Directory);
        item->setIcon(0, iconForDir());
    }

    // Filtres selon le type selectionne
    QStringList filters;
    if (m_filtreActif && !m_filtreExt.isEmpty()) {
        for (const QString &e : std::as_const(m_filtreExt)) filters << "*." + e;
    } else
    switch (m_filterType) {
    case EazyFileType::ENC: filters << "*.enc"; break;
    case EazyFileType::NC:  filters << "*.nc";  break;
    case EazyFileType::DXF: filters << "*.dxf" << "*.stp" << "*.step" << "*.igs" << "*.iges"; break;
    case EazyFileType::CNT: filters << "*.cnt"; break;
    case EazyFileType::PNT: filters << "*.pnt"; break;
    default:
        filters << "*.enc" << "*.nc" << "*.dxf" << "*.stp" << "*.step"
                << "*.igs" << "*.iges" << "*.cnt" << "*.pnt";
        break;
    }

    const QFileInfoList files = dir.entryInfoList(
        filters, QDir::Files, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &fi : files) {
        EazyFileType type = detectFileType(fi.fileName());
        QTreeWidgetItem *item = new QTreeWidgetItem(ui->treeFiles);
        item->setText(0, fi.completeBaseName().toUpper());
        // STEP / IGES s'ouvrent dans le même convertisseur que le DXF,
        // mais on affiche leur vrai type (avant : « .DXF », trompeur)
        const QString ext = fi.suffix().toLower();
        const bool modele3D = (ext == "stp" || ext == "step" || ext == "igs" || ext == "iges");
        item->setText(1, modele3D ? "." + fi.suffix().toUpper() : labelForType(type));
        item->setText(2, formatSize(fi.size()));
        item->setText(3, fi.lastModified().toString("dd.MM.yyyy"));
        item->setText(4, fi.lastModified().toString("HH:mm"));
        item->setData(0, Qt::UserRole,     fi.absoluteFilePath());
        item->setData(0, Qt::UserRole + 1, (int)type);
        item->setIcon(0, modele3D ? makeBadge("#BB6600", "3D") : iconForType(type));
    }

    updateStatusBar();

    // Vider le preview quand on change de dossier
    if (m_preview) m_preview->clear();
}

// ================================================================
//  Preview
// ================================================================
void FileBrowserPage::updatePreview(const QString &filePath, EazyFileType type)
{
    if (!m_preview) return;
    if (filePath.isEmpty() || type == EazyFileType::Directory || type == EazyFileType::Unknown) {
        m_preview->clear();
        return;
    }
    m_preview->loadFile(filePath);
}

// ================================================================
//  Slots arbre gauche
// ================================================================
void FileBrowserPage::onDirSelectionChanged()
{
    const auto sel = ui->treeDir->selectedItems();
    if (sel.isEmpty()) return;
    const QString path = sel.first()->data(0, Qt::UserRole).toString();
    if (!path.isEmpty()) loadFilesForDir(path);
}

void FileBrowserPage::onDirItemExpanded(QTreeWidgetItem *item)
{
    // Lecture d'un niveau à la demande (disques, bureau…)
    if (!item || item->childCount() != 1 || !item->child(0)->data(0, Qt::UserRole + 2).toBool()) return;
    delete item->takeChild(0);
    const QFileInfoList entries = QDir(item->data(0, Qt::UserRole).toString()).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : entries) {
        auto *it = new QTreeWidgetItem(item);
        it->setText(0, fi.fileName().toUpper());
        it->setData(0, Qt::UserRole, fi.absoluteFilePath());
        it->setIcon(0, iconForDir());
        auto *vide = new QTreeWidgetItem(it);
        vide->setData(0, Qt::UserRole + 2, true);
    }
}

// ================================================================
//  Slots liste droite
// ================================================================
void FileBrowserPage::onFileSelectionChanged()
{
    const auto sel = ui->treeFiles->selectedItems();
    if (sel.isEmpty()) return;

    const QString path = sel.first()->data(0, Qt::UserRole).toString();
    const EazyFileType type = (EazyFileType)sel.first()->data(0, Qt::UserRole + 1).toInt();

    updatePreview(path, type);
    emit fileSelected(path, type);
}

void FileBrowserPage::onFileDoubleClicked(QTreeWidgetItem *item, int col)
{
    Q_UNUSED(col)
    if (!item) return;

    const EazyFileType type = (EazyFileType)item->data(0, Qt::UserRole + 1).toInt();
    const QString path = item->data(0, Qt::UserRole).toString();

    if (type == EazyFileType::Directory) {
        loadFilesForDir(path);
        syncDirTree(path);
        return;
    }
    if (m_modeChoix) { emit fichierChoisi(path); return; }

    if (type == EazyFileType::ENC || type == EazyFileType::CNT || type == EazyFileType::PNT) {
        emit openEncFile(path);
        emit navigateTo("PROGRAMME");
    } else if (type == EazyFileType::NC) {
        emit openNcFile(path);
        emit navigateTo("EXECUTION");
    } else if (type == EazyFileType::DXF) {
        // Convertisseur DXF (façon TNC) : calques, origine, sélection,
        // export du contour en .cnt ou des positions en .pnt
        auto *dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle("Convertisseur CAO — " + QFileInfo(path).fileName());
        auto *lay = new QVBoxLayout(dlg);
        lay->setContentsMargins(2, 2, 2, 2);
        auto *view = new CadViewWidget(dlg);
        const QString ext = QFileInfo(path).suffix().toLower();
        const bool modele3D = (ext == "stp" || ext == "step" || ext == "igs" || ext == "iges");
        if (modele3D) {
            // STEP / IGES : vue 3D ombrée + vue 2D des contours, au choix
            auto *barre = new QWidget;
            auto *bl = new QHBoxLayout(barre);
            bl->setContentsMargins(4, 2, 4, 2);
            auto *b3d = new QPushButton("Vue 3D");
            auto *b2d = new QPushButton("Contours 2D");
            for (QPushButton *b : {b3d, b2d}) { b->setCheckable(true); b->setMinimumWidth(130); }
            b3d->setChecked(true);
            auto *grp = new QButtonGroup(barre);
            grp->addButton(b3d, 0); grp->addButton(b2d, 1);
            grp->setExclusive(true);
            bl->addWidget(b3d); bl->addWidget(b2d);
            bl->addSpacing(30);
            bl->addWidget(new QLabel(QString::fromUtf8("Passe ébauche")));
            auto *passe = new QDoubleSpinBox;
            passe->setRange(0.1, 50.0); passe->setDecimals(2); passe->setValue(2.0);
            passe->setSuffix(" mm"); passe->setMinimumWidth(110);
            auto *bCouches = new QPushButton("Couches Z");
            bCouches->setCheckable(true); bCouches->setMinimumWidth(130);
            bl->addWidget(passe); bl->addWidget(bCouches);
            bl->addStretch();
            lay->addWidget(barre);

            auto *pile = new QStackedWidget;
            auto *v3d = new CadView3D;
            pile->addWidget(v3d);    // 0
            pile->addWidget(view);   // 1
            lay->addWidget(pile, 1);
            connect(grp, &QButtonGroup::idClicked, pile, &QStackedWidget::setCurrentIndex);
            auto doc3d = std::make_shared<CadDocument>(CadImporter::load(path));
            v3d->setDocument(*doc3d);
            auto majCouches = [v3d, doc3d, passe, bCouches]() {
                if (!bCouches->isChecked()) { v3d->setCouches({}); return; }
                QApplication::setOverrideCursor(Qt::WaitCursor);
                v3d->setCouches(Decoupe3D::couches(doc3d->mesh, doc3d->bbMax[2],
                                                   doc3d->bbMin[2], passe->value()));
                QApplication::restoreOverrideCursor();
            };
            connect(bCouches, &QPushButton::toggled, v3d, majCouches);
            connect(passe, &QDoubleSpinBox::editingFinished, v3d, majCouches);
        } else {
            lay->addWidget(view);
        }
        dlg->resize(1200, 760);
        view->loadFile(path);
        dlg->showMaximized();
        dlg->setWindowModality(Qt::ApplicationModal);
        // A la fermeture du convertisseur : EazyNC revient au premier plan
        // (avant, la fenetre principale restait cachee et semblait fermee).
        connect(dlg, &QDialog::finished, this, [this]{
            loadFilesForDir(m_currentDirPath);
            QWidget *w = window();
            if (w) {
                if (w->isMinimized()) w->showMaximized();
                w->show(); w->raise(); w->activateWindow();
            }
        });
    }
}

// ================================================================
//  Softkeys actions
// ================================================================
void FileBrowserPage::onOpen()
{
    const auto sel = ui->treeFiles->selectedItems();
    if (!sel.isEmpty()) onFileDoubleClicked(sel.first(), 0);
}

void FileBrowserPage::onNewDir()
{
    QString name = askName("Nouveau dossier", "NOM-PIECE");
    if (name.isEmpty()) return;
    name = name.toUpper().replace(' ', '-');
    if (!QDir().mkpath(m_currentDirPath + "/" + name))
        QMessageBox::warning(this, "Erreur", "Impossible de creer le dossier.");
    else {
        buildDirTree();
        loadFilesForDir(m_currentDirPath);
    }
}

void FileBrowserPage::onNewFile()
{
    QString name = askName("Nouveau programme EazyNC", "OP1-FRAISAGE");
    if (name.isEmpty()) return;
    name = name.toUpper().replace(' ', '-');
    if (!name.endsWith(".enc", Qt::CaseInsensitive)) name += ".enc";

    const QString fullPath = m_currentDirPath + "/" + name;
    QFile f(fullPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Erreur", "Impossible de creer le fichier.");
        return;
    }
    f.write("{\n  \"version\": 1,\n  \"blocks\": [\n"
            "    { \"id\": 1, \"type\": \"BlkForm\", \"name\": \"BlkForm\" }\n"
            "  ]\n}\n");
    f.close();

    loadFilesForDir(m_currentDirPath);
    emit openEncFile(fullPath);
    emit navigateTo("PROGRAMME");
}

void FileBrowserPage::onRename()
{
    const auto sel = ui->treeFiles->selectedItems();
    if (sel.isEmpty()) return;
    const QString oldPath = sel.first()->data(0, Qt::UserRole).toString();
    const QFileInfo fi(oldPath);

    QString newName = askName("Renommer", fi.completeBaseName());
    if (newName.isEmpty()) return;
    newName = newName.toUpper().replace(' ', '-');

    const QString ext = fi.isDir() ? "" : "." + fi.suffix();
    const QString newPath = fi.absolutePath() + "/" + newName + ext;
    if (!QFile::rename(oldPath, newPath))
        QMessageBox::warning(this, "Erreur", "Impossible de renommer.");
    else
        refreshCurrentDir();
}

void FileBrowserPage::onDelete()
{
    const auto sel = ui->treeFiles->selectedItems();
    if (sel.isEmpty()) return;
    const QString path = sel.first()->data(0, Qt::UserRole).toString();
    const QFileInfo fi(path);

    if (QMessageBox::question(this, "Supprimer",
            QString("Supprimer \"%1\" ?").arg(fi.fileName()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    const bool ok = fi.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!ok)
        QMessageBox::warning(this, "Erreur", "Impossible de supprimer.");
    else {
        if (fi.isDir()) buildDirTree();
        refreshCurrentDir();
    }
}

void FileBrowserPage::onCopy()
{
    const auto sel = ui->treeFiles->selectedItems();
    if (sel.isEmpty()) return;
    const QString path = sel.first()->data(0, Qt::UserRole).toString();
    const QFileInfo fi(path);
    if (fi.isDir()) return;

    QString newName = askName("Copier sous le nom", fi.completeBaseName() + "-COPIE");
    if (newName.isEmpty()) return;
    newName = newName.toUpper().replace(' ', '-');
    if (!newName.endsWith("." + fi.suffix(), Qt::CaseInsensitive))
        newName += "." + fi.suffix().toLower();

    const QString dest = m_currentDirPath + "/" + newName;
    if (!QFile::copy(path, dest))
        QMessageBox::warning(this, "Erreur", "Impossible de copier.");
    else
        refreshCurrentDir();
}

void FileBrowserPage::setFilterType(EazyFileType t)
{
    m_filterType = t;
    QString label = "SELECT.\nTYPE";
    if (m_filterType != EazyFileType::Unknown)
        label = QString("TYPE:\n%1").arg(labelForType(m_filterType));
    ui->skSelectType->setText(label);
    loadFilesForDir(m_currentDirPath);
}

void FileBrowserPage::setModeChoix(bool on)
{
    m_modeChoix = on;
    if (ui->skNewFile) ui->skNewFile->setVisible(!on);      // pas de nouveau .enc ici
}

void FileBrowserPage::setFiltre(const QStringList &extensions, const QString &libelle)
{
    m_filtreExt.clear();
    for (const QString &e : extensions) m_filtreExt << e.toLower().remove('.');
    m_filtreLib = libelle;
    m_filtreActif = !m_filtreExt.isEmpty();
    majLibelleType();
    loadFilesForDir(m_currentDirPath);
}

void FileBrowserPage::majLibelleType()
{
    if (!m_filtreExt.isEmpty())
        ui->skSelectType->setText(m_filtreActif ? QString("TYPE:\n%1").arg(m_filtreLib) : QString("TYPE:\nTOUS"));
}

void FileBrowserPage::allerA(const QString &dossier)
{
    if (dossier.isEmpty() || !QFileInfo(dossier).isDir()) return;
    const QString d = QDir::cleanPath(QFileInfo(dossier).absoluteFilePath());
    loadFilesForDir(d);
    syncDirTree(d);
}

void FileBrowserPage::onSelectType()
{
    if (!m_filtreExt.isEmpty()) {                 // mode choix : tri du besoin <-> tous
        m_filtreActif = !m_filtreActif;
        majLibelleType();
        loadFilesForDir(m_currentDirPath);
        return;
    }
    // Cycle entre les types de filtre
    switch (m_filterType) {
    case EazyFileType::Unknown: m_filterType = EazyFileType::ENC; break;
    case EazyFileType::ENC:     m_filterType = EazyFileType::NC;  break;
    case EazyFileType::NC:      m_filterType = EazyFileType::DXF; break;
    case EazyFileType::DXF:     m_filterType = EazyFileType::CNT; break;
    case EazyFileType::CNT:     m_filterType = EazyFileType::PNT; break;
    default:                    m_filterType = EazyFileType::Unknown; break;
    }

    // Mettre a jour le libelle du softkey
    QString label = "SELECT.\nTYPE";
    if (m_filterType != EazyFileType::Unknown)
        label = QString("TYPE:\n%1").arg(labelForType(m_filterType));
    ui->skSelectType->setText(label);

    loadFilesForDir(m_currentDirPath);
}

void FileBrowserPage::onLastFiles()
{
    // Placeholder - afficher les derniers fichiers ouverts
}

void FileBrowserPage::navigateUp()
{
    QDir dir(m_currentDirPath);
    if (dir.cdUp() && dir.absolutePath() != m_currentDirPath) {
        loadFilesForDir(dir.absolutePath());
        syncDirTree(dir.absolutePath());
    }
}

// ================================================================
//  Clavier
// ================================================================
void FileBrowserPage::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        onOpen();
    else if (e->key() == Qt::Key_Delete)
        onDelete();
    else if (e->key() == Qt::Key_F2)
        onRename();
    else if (e->key() == Qt::Key_Backspace)
        navigateUp();
    else
        QWidget::keyPressEvent(e);
}

// ================================================================
//  Utilitaires
// ================================================================
void FileBrowserPage::refreshCurrentDir()
{
    loadFilesForDir(m_currentDirPath);
}

void FileBrowserPage::updateStatusBar()
{
    const int count = ui->treeFiles->topLevelItemCount();
    ui->lblFileCount->setText(QString("%1 objet(s)").arg(count));
    ui->lblCurrentPath->setText(QDir::toNativeSeparators(m_currentDirPath));

    QStorageInfo si(m_currentDirPath);
    if (si.isValid()) {
        const double gb = si.bytesFree() / (1024.0 * 1024 * 1024);
        ui->lblDiskFree->setText(QString("%1 Go libres").arg(gb, 0, 'f', 1));
    }
}

void FileBrowserPage::updatePathBar(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    ui->lblFullPath->setText(native);
    ui->lblCurrentPath->setText(native);
}

void FileBrowserPage::syncDirTree(const QString &path)
{
    QTreeWidgetItemIterator it(ui->treeDir);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == path) {
            ui->treeDir->setCurrentItem(*it);
            (*it)->setExpanded(true);
            break;
        }
        ++it;
    }
}

void FileBrowserPage::updateClock()
{
    ui->lblClock->setText(QTime::currentTime().toString("HH:mm"));
}

// ================================================================
//  Types de fichiers
// ================================================================
EazyFileType FileBrowserPage::detectFileType(const QString &fileName) const
{
    const QString lower = fileName.toLower();
    if (lower.endsWith(".enc")) return EazyFileType::ENC;
    if (lower.endsWith(".nc"))  return EazyFileType::NC;
    if (lower.endsWith(".dxf")) return EazyFileType::DXF;
    // STEP / IGES : même convertisseur que le DXF (lecture OpenCASCADE)
    if (lower.endsWith(".stp") || lower.endsWith(".step")
        || lower.endsWith(".igs") || lower.endsWith(".iges")) return EazyFileType::DXF;
    if (lower.endsWith(".cnt")) return EazyFileType::CNT;
    if (lower.endsWith(".pnt")) return EazyFileType::PNT;
    return EazyFileType::Unknown;
}

QString FileBrowserPage::labelForType(EazyFileType type) const
{
    switch (type) {
    case EazyFileType::ENC: return ".ENC";
    case EazyFileType::NC:  return ".NC";
    case EazyFileType::DXF: return ".DXF";
    case EazyFileType::CNT: return ".CNT";
    case EazyFileType::PNT: return ".PNT";
    default:                return "---";
    }
}

QString FileBrowserPage::colorForType(EazyFileType type) const
{
    switch (type) {
    case EazyFileType::ENC: return "#0000FF";
    case EazyFileType::NC:  return "#007700";
    case EazyFileType::DXF: return "#BB6600";
    case EazyFileType::CNT: return "#880088";
    case EazyFileType::PNT: return "#006688";
    default:                return "#878787";
    }
}

QIcon FileBrowserPage::iconForType(EazyFileType type) const
{
    switch (type) {
    case EazyFileType::ENC: return makeBadge("#0000FF", "E");
    case EazyFileType::NC:  return makeBadge("#007700", "G");
    case EazyFileType::DXF: return makeBadge("#BB6600", "X");
    case EazyFileType::CNT: return makeBadge("#880088", "C");
    case EazyFileType::PNT: return makeBadge("#006688", "P");
    default:                return makeBadge("#878787", "?");
    }
}

QIcon FileBrowserPage::iconForDir() const
{
    return makeBadge("#737373", "D");
}

QString FileBrowserPage::formatSize(qint64 bytes) const
{
    if (bytes < 1024)       return QString("%1 o").arg(bytes);
    if (bytes < 1024*1024)  return QString("%1 Ko").arg(bytes / 1024);
    return QString("%1 Mo").arg(bytes / (1024LL * 1024));
}

QString FileBrowserPage::askName(const QString &title, const QString &def)
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, title, "Nom :", QLineEdit::Normal, def, &ok);
    return ok ? name.trimmed() : QString();
}

// ================================================================
//  Icone badge TNC 620 style
//  Rectangle gris avec lettre blanche — style machine-outil
// ================================================================
QIcon FileBrowserPage::makeBadge(const QString &color, const QString &letter, int size) const
{
    // Toujours utiliser 28px minimum pour la lisibilite
    const int sz = qMax(size, 28);
    QPixmap pix(sz, sz);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, false);

    if (letter == "D") {
        // Icone dossier style TNC 620 - jaune/beige
        QColor fc(180, 160, 60);  // jaune dossier
        QColor fc2(200, 180, 80);
        // Onglet du dossier (partie haute)
        p.setBrush(fc2);
        p.setPen(QPen(QColor(120,100,20), 1));
        p.drawRect(1, 2, sz/2, 4);
        // Corps du dossier
        p.setBrush(fc);
        p.drawRect(1, 5, sz-3, sz-8);
        // Reflet leger
        p.setBrush(QColor(220,200,100,80));
        p.setPen(Qt::NoPen);
        p.drawRect(3, 7, sz-7, 4);
    } else {
        // Badge fichier : rectangle avec coin plié style document
        QColor bg(color);
        p.setBrush(bg);
        p.setPen(QPen(bg.darker(150), 1));
        // Corps
        QPolygon poly;
        int fold = sz/4;
        poly << QPoint(1,1) << QPoint(sz-fold-1,1)
             << QPoint(sz-1,fold+1) << QPoint(sz-1,sz-1)
             << QPoint(1,sz-1);
        p.drawPolygon(poly);
        // Coin plié
        p.setBrush(bg.lighter(130));
        QPolygon corner;
        corner << QPoint(sz-fold-1,1) << QPoint(sz-1,fold+1) << QPoint(sz-fold-1,fold+1);
        p.drawPolygon(corner);
        // Texte
        p.setPen(Qt::white);
        p.setFont(QFont("TNC640Global", sz > 24 ? 9 : 7, QFont::Bold));
        QRect textR(1, sz/2 - 2, sz-2, sz/2);
        p.drawText(textR, Qt::AlignCenter, letter);
    }
    p.end();
    return QIcon(pix);
}
