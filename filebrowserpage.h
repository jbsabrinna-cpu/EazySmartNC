#pragma once
#include "filepreviewwidget.h"
#pragma execution_character_set("utf-8")
// ================================================================
//  filebrowserpage.h  -  Gestionnaire fichiers style PGM MGT TNC 620
// ================================================================
#include <QWidget>
#include <QTreeWidgetItem>
#include <QDir>
#include <QTimer>
#include <QLabel>
#include <QFileInfo>
#include <QStorageInfo>
#include <QKeyEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class FileBrowserPage; }
QT_END_NAMESPACE

enum class EazyFileType {
    Directory,
    ENC,    // Programme EazyNC (.enc)
    NC,     // G-code brut (.nc)
    DXF,    // Dessin CAO (.dxf)
    CNT,    // Contour (.cnt)
    PNT,    // Positions (.pnt)
    Unknown
};

class FileBrowserPage : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserPage(QWidget *parent = nullptr);
    ~FileBrowserPage();

    void    setRootPath(const QString &path);
    QString rootPath() const { return m_rootPath; }
    void    refreshCurrentDir();
    // Filtre directement sur un type (ex: PNT) — sans passer par le
    // cycle du softkey "SELECT. TYPE". Utilisé par les dialogues de
    // sélection de fichier .pnt (éditeur de points).
    void    setFilterType(EazyFileType t);

    // Acces depuis MainWindow
    QString selectedFilePath() const;
    EazyFileType selectedFileType() const;

signals:
    void openEncFile(const QString &filePath);
    void openNcFile(const QString &filePath);
    void navigateTo(const QString &page);
    void fileSelected(const QString &path, EazyFileType type);

public slots:
    void onOpen();
    void onNewDir();
    void onNewFile();
    void onRename();
    void onDelete();
    void onCopy();
    void onSelectType();
    void onLastFiles();
    void navigateUp();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onDirSelectionChanged();
    void onDirItemExpanded(QTreeWidgetItem *item);
    void onFileSelectionChanged();
    void onFileDoubleClicked(QTreeWidgetItem *item, int col);
    void updateClock();

private:
    FilePreviewWidget *m_preview = nullptr;
    Ui::FileBrowserPage *ui;

    QString      m_rootPath;
    QString      m_currentDirPath;
    QTimer      *m_clockTimer   = nullptr;
    EazyFileType m_filterType   = EazyFileType::Unknown; // Unknown = tous

    // Construction UI
    void buildDirTree();
    void populateDir(QTreeWidgetItem *parent, const QString &dirPath);
    void loadFilesForDir(const QString &dirPath);
    void updatePreview(const QString &filePath, EazyFileType type);
    void updateStatusBar();
    void updatePathBar(const QString &path);
    void syncDirTree(const QString &path);

    // Utilitaires
    EazyFileType detectFileType(const QString &fileName) const;
    QIcon        iconForType(EazyFileType type) const;
    QIcon        iconForDir() const;
    QString      labelForType(EazyFileType type) const;
    QString      colorForType(EazyFileType type) const;
    QString      formatSize(qint64 bytes) const;
    QString      askName(const QString &title, const QString &def = {});
    QIcon        makeBadge(const QString &color, const QString &letter, int size = 20) const;
};
