#pragma execution_character_set("utf-8")
#include "modeusin_widget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QButtonGroup>
#include <QPixmap>
#include <QIcon>
#include <QEvent>

// ═══════════════════════════════════════════════════════════════
ModeUsinWidget::ModeUsinWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

QString ModeUsinWidget::styleTuile()
{
    return QStringLiteral(
        "QToolButton { border:1px solid #808080; border-radius:4px;"
        " background:transparent; color:#404040; font-size:11pt; padding:6px; }"
        "QToolButton:hover { border-color:#00AA00; }"
        "QToolButton:checked { border:2px solid #00AA00; background:#E0E8FF;"
        " color:#004400; font-weight:bold; }");
}

void ModeUsinWidget::setupUi()
{
    setStyleSheet(
        "ModeUsinWidget {background:#EAEAEA;border-bottom:1px solid #808080;}");

    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(8,6,8,8);
    mainLay->setSpacing(4);

    auto *title = new QLabel("Opérations d'usinage");
    title->setStyleSheet(
        "QLabel{color:#606060;font-size:9px;"
        "font-weight:bold;letter-spacing:2px;background:transparent;}");
    mainLay->addWidget(title);

    auto *row = new QHBoxLayout;
    row->setSpacing(8);

    struct BtnDef { QString iconPath; QString label; QString help; };
    const BtnDef defs[3] = {
        { ":/unit/ebauche finition.png", "Ebauche + Finition", "mode_eb_fin"   },
        { ":/unit/ebauche.png",          "Ebauche",            "mode_ebauche"  },
        { ":/unit/finition.png",         "Finition",           "mode_finition" },
    };

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    for (int i = 0; i < 3; ++i) {
        auto *btn = new QToolButton;
        btn->setText(defs[i].label);
        QPixmap px(defs[i].iconPath);
        if (!px.isNull())
            btn->setIcon(QIcon(px));
        btn->setIconSize({kIconW, kIconH});
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setMinimumWidth(kTuileMinW);
        btn->setMaximumWidth(kTuileMaxW);
        btn->setStyleSheet(styleTuile());
        btn->setProperty("helpKey", defs[i].help);
        btn->installEventFilter(this);
        m_group->addButton(btn, i);
        row->addWidget(btn, 1);
    }
    row->addStretch(0);
    mainLay->addLayout(row);

    connect(m_group, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &ModeUsinWidget::modeChanged);

    m_group->button(0)->setChecked(true);
}

bool ModeUsinWidget::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::Enter)
        emit helpHover(obj->property("helpKey").toString());
    else if (ev->type() == QEvent::Leave)
        emit helpHover(QString());
    return QWidget::eventFilter(obj, ev);
}

int ModeUsinWidget::mode() const
{
    return m_group ? m_group->checkedId() : 0;
}

void ModeUsinWidget::setMode(int m)
{
    if (!m_group) return;
    auto *btn = m_group->button(m);
    if (btn) btn->setChecked(true);
}
