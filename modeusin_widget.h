#pragma once
#pragma execution_character_set("utf-8")

#include <QWidget>
#include <QButtonGroup>

// ═══════════════════════════════════════════════════════════════
//  ModeUsinWidget — 3 boutons radio "Opérations d'usinage"
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
// ═══════════════════════════════════════════════════════════════
class QAbstractButton;

class ModeUsinWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ModeUsinWidget(QWidget *parent = nullptr);

    int  mode() const;
    void setMode(int m);

signals:
    void modeChanged(int mode);   // émis quand l'opérateur change le bouton

private:
    void setupUi();
    QButtonGroup *m_group = nullptr;
};
