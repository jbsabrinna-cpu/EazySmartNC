#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pointspreviewwidget.h
//
//  Petit widget de dessin (QPainter) affichant en vue de dessus
//  (X/Y) l'ensemble des positions calculées pour un cycle — quel
//  que soit le modèle utilisé (Point/Ligne/Grille/Cercle/Tableau).
//
//  Volontairement indépendant de toute struct de données (juste
//  un QVector<QPointF>) pour rester réutilisable par n'importe
//  quel cycle plus tard, sans dépendance circulaire d'includes.
//
//  UTILISATION :
//     m_preview = new PointsPreviewWidget;
//     m_preview->setPoints(FraisageTrouGenerator::computePositions(d));
// ═══════════════════════════════════════════════════════════════

#include <QWidget>
#include <QPainter>
#include <QPointF>
#include <QVector>
#include <QtMath>

class PointsPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PointsPreviewWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(140);
        setStyleSheet("background:#1a1a1a;border:1px solid #909090;border-radius:4px;");
    }

    void setPoints(const QVector<QPointF> &pts)
    {
        m_pts = pts;
        update();  // repaint immédiat — c'est ça qui donne l'effet "live"
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int W = width(), H = height();
        p.fillRect(rect(), QColor(0x1a,0x1a,0x1a));

        if (m_pts.isEmpty()) {
            p.setPen(QColor(0x80,0x80,0x80));
            p.drawText(rect(), Qt::AlignCenter, "Aucune position définie");
            return;
        }

        // ── Calcul des limites pour cadrer tous les points ────────
        double xMin = m_pts[0].x(), xMax = m_pts[0].x();
        double yMin = m_pts[0].y(), yMax = m_pts[0].y();
        for (const QPointF &pt : m_pts) {
            xMin = qMin(xMin, pt.x()); xMax = qMax(xMax, pt.x());
            yMin = qMin(yMin, pt.y()); yMax = qMax(yMax, pt.y());
        }
        double spanX = qMax(xMax - xMin, 1.0);
        double spanY = qMax(yMax - yMin, 1.0);
        double cx = (xMin + xMax) / 2.0, cy = (yMin + yMax) / 2.0;

        const int margin = 20;
        double scale = qMin((W - 2*margin) / spanX, (H - 2*margin) / spanY);
        scale = qMin(scale, 40.0);  // évite un zoom excessif sur un point unique

        auto toScreen = [&](const QPointF &pt) {
            double sx = W/2.0 + (pt.x() - cx) * scale;
            double sy = H/2.0 - (pt.y() - cy) * scale;  // Y inversé (écran vs machine)
            return QPointF(sx, sy);
        };

        // ── Axes (croix au centre du repère machine, pas du groupe) ──
        p.setPen(QPen(QColor(0x40,0x40,0x40), 1, Qt::DashLine));
        QPointF origin = toScreen(QPointF(0,0));
        p.drawLine(QPointF(0, origin.y()), QPointF(W, origin.y()));
        p.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), H));

        // ── Points ────────────────────────────────────────────────
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xA6,0xCE,0x39));  // vert TNC
        for (const QPointF &pt : m_pts) {
            QPointF s = toScreen(pt);
            p.drawEllipse(s, 4, 4);
        }

        // ── Compteur ──────────────────────────────────────────────
        p.setPen(QColor(0xA6,0xCE,0x39));
        p.drawText(6, 14, QString("%1 position%2")
                              .arg(m_pts.size())
                              .arg(m_pts.size() > 1 ? "s" : ""));
    }

private:
    QVector<QPointF> m_pts;
};
