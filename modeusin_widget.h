#pragma once
#pragma execution_character_set("utf-8")

#include <QWidget>
#include <QButtonGroup>

// ═══════════════════════════════════════════════════════════════
//  ModeUsinWidget — 3 grandes tuiles "Opérations d'usinage"
//
//  Valeurs :
//    0 = Ébauche + Finition  (défaut)
//    1 = Ébauche seule
//    2 = Finition seule
//
//  Usage :
//    auto *w = new ModeUsinWidget(this);
//    w->setMode(d.modeUsin);
//    connect(w, &ModeUsinWidget::modeChanged, this, [&](int m){ d.modeUsin = m; });
//
//  styleTuile() : feuille de style commune des tuiles de mode
//  (aussi utilisée par le Fraisage de trou).
// ═══════════════════════════════════════════════════════════════
class QAbstractButton;

class ModeUsinWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ModeUsinWidget(QWidget *parent = nullptr);

    int  mode() const;
    void setMode(int m);

    // Taille commune des tuiles
    static constexpr int kIconW   = 150;
    static constexpr int kIconH   = 112;
    static constexpr int kTuileMinW = 150;
    static constexpr int kTuileMaxW = 260;
    static QString styleTuile();

signals:
    void modeChanged(int mode);          // émis quand l'opérateur change le bouton
    void helpHover(const QString &key);  // survol : clé d'aide ("" en sortie)

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void setupUi();
    QButtonGroup *m_group = nullptr;
};
