#pragma once
#pragma execution_character_set("utf-8")

// ═══════════════════════════════════════════════════════════════
//  StockAlphaSoftkey — softkey custom 3 zones :  −  Brut XX%  +
//
//  Visuel :
//    ┌─────────────────┐
//    │   −   Brut   +  │
//    │      50%        │
//    └─────────────────┘
//
//  Clic gauche sur la zone "−" : diminue l'opacité de 10%
//  Clic gauche sur la zone "+" : augmente l'opacité de 10%
//  Plage : 0% → 100%, pas de 10%
//
//  Style aligné sur les autres softkeys TNC (QToolButton).
// ═══════════════════════════════════════════════════════════════

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QFrame>

class StockAlphaSoftkey : public QWidget
{
    Q_OBJECT
public:
    explicit StockAlphaSoftkey(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("StockAlphaSoftkey");
        // Style général : on imite les softkeys TNC
        setStyleSheet(
            "#StockAlphaSoftkey { background: transparent; }"
            "QToolButton#alphaMinus, QToolButton#alphaPlus {"
            "  background: #2A2D33; color: #E0E0E0;"
            "  border: 1px solid #555; border-radius: 2px;"
            "  font-size: 13pt; font-weight: bold;"
            "  min-width: 20px;"
            "}"
            "QToolButton#alphaMinus:hover, QToolButton#alphaPlus:hover {"
            "  background: #3A3F47;"
            "}"
            "QToolButton#alphaMinus:pressed, QToolButton#alphaPlus:pressed {"
            "  background: #4A5057;"
            "}"
            "QLabel#alphaTitle { color: #E0E0E0; font-size: 9pt; }"
            "QLabel#alphaValue {"
            "  color: #000000;"            // noir comme demandé
            "  font-size: 11pt;"
            "  font-weight: bold;"
            "  background: transparent;"
            "}"
            );

        // Layout horizontal unique : [−] [zone centrale] [+]
        // La zone centrale contient "Brut" en haut et "XX%" en bas,
        // avec le pourcentage parfaitement centré verticalement
        // dans la hauteur disponible du widget.
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(2, 2, 2, 2);
        row->setSpacing(2);

        m_btnMinus = new QToolButton(this);
        m_btnMinus->setObjectName("alphaMinus");
        m_btnMinus->setText("−");

        // Zone centrale : Brut + XX% empilés verticalement, centrés
        auto *centerWrap = new QWidget(this);
        auto *centerLay  = new QVBoxLayout(centerWrap);
        centerLay->setContentsMargins(0, 0, 0, 0);
        centerLay->setSpacing(0);

        m_lblTitle = new QLabel("Brut", centerWrap);
        m_lblTitle->setObjectName("alphaTitle");
        m_lblTitle->setAlignment(Qt::AlignCenter);

        m_lblValue = new QLabel(centerWrap);
        m_lblValue->setObjectName("alphaValue");
        m_lblValue->setAlignment(Qt::AlignCenter);

        // Le pourcentage prend toute la hauteur centrale,
        // le titre "Brut" reste au-dessus
        centerLay->addWidget(m_lblTitle, 0);
        centerLay->addWidget(m_lblValue, 1);  // stretch=1 → centre vertical

        m_btnPlus = new QToolButton(this);
        m_btnPlus->setObjectName("alphaPlus");
        m_btnPlus->setText("+");

        row->addWidget(m_btnMinus);
        row->addWidget(centerWrap, 1);
        row->addWidget(m_btnPlus);

        connect(m_btnMinus, &QToolButton::clicked, this, [this]{
            setAlpha(m_alpha - 0.025f);
        });
        connect(m_btnPlus, &QToolButton::clicked, this, [this]{
            setAlpha(m_alpha + 0.025f);
        });

        setAlpha(0.50f);  // par défaut 50%
    }

    float alpha() const { return m_alpha; }

public slots:
    void setAlpha(float a) {
        // Clamp et arrondi au pas de 2.5%
        // Maximum 97.5% (= 39 pas de 2.5%) : le mode 100% pose des
        // problèmes de rendu sur les contours (z-fighting), 97.5%
        // donne un résultat visuellement opaque sans ces artefacts.
        a = qBound(0.0f, a, 0.975f);
        // Arrondir au plus proche multiple de 0.025 (= 2.5%)
        int steps = int(a * 40.f + 0.5f);  // 0..39 pas de 2.5%
        steps = qBound(0, steps, 39);
        a = steps / 40.f;
        // Affichage : 50.0 % ou 2.5 % avec une décimale
        const float pct = a * 100.f;
        const QString lbl = QString::number(pct, 'f', 1) + "%";
        if (qFuzzyCompare(a + 1.f, m_alpha + 1.f)) {
            m_lblValue->setText(lbl);
            return;
        }
        m_alpha = a;
        m_lblValue->setText(lbl);
        emit alphaChanged(m_alpha);
    }

signals:
    void alphaChanged(float alpha);

private:
    QToolButton *m_btnMinus = nullptr;
    QToolButton *m_btnPlus  = nullptr;
    QLabel      *m_lblTitle = nullptr;
    QLabel      *m_lblValue = nullptr;
    float        m_alpha    = 0.50f;
};
