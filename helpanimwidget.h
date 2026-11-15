#ifndef HELPANIMWIDGET_H
#define HELPANIMWIDGET_H

// ─────────────────────────────────────────────────────────────
//  HelpAnimWidget — dessins d'aide animés (cadre AIDE)
//  Chaque clé d'aide (ex. "plongee_0", "prof_passe") correspond à
//  une petite scène dessinée au QPainter qui tourne en boucle :
//  l'outil fait le mouvement du paramètre survolé.
// ─────────────────────────────────────────────────────────────
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointF>
#include <QRectF>

class QPainter;
class QPainterPath;
class QMovie;

class HelpAnimWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HelpAnimWidget(QWidget *parent = nullptr);

    // Retourne true si la clé a une animation (et la lance)
    bool setScene(const QString &key);
    static bool hasScene(const QString &key);
    void stop();

    QSize sizeHint() const override { return {400, 280}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;

private:
    QString       m_key;
    QMovie       *m_movie = nullptr;  // animation 3D (GIF) si elle existe
    static QString cheminGif(const QString &key);
    QTimer        m_timer;
    QElapsedTimer m_clock;
    double        m_period = 5.0;   // secondes par boucle

    double phase() const;           // 0..1

    // Scènes
    void scenePlongee(QPainter &p, double t, int mode);
    void scenePasses(QPainter &p, double t, bool highlightPasse);
    void sceneApproche(QPainter &p, double t);
    void sceneRecouvrement(QPainter &p, double t);
    void sceneSurepLat(QPainter &p, double t);
    void sceneSurepFond(QPainter &p, double t);
    void sceneFraisage(QPainter &p, double t, bool avalant);
    void sceneRayon(QPainter &p, double t);
    void sceneLongueur(QPainter &p, double t);
    void sceneDiametre(QPainter &p, double t);
    void sceneSurepLatCirc(QPainter &p, double t);
    void scenePosition(QPainter &p, double t, int point, bool angle);

    // Outils de dessin
    static void drawMatiere(QPainter &p, const QRectF &r);
    static void drawMatierePath(QPainter &p, const QPainterPath &path);
    static void drawOutilProfil(QPainter &p, double x, double yBas, double d);
    static void drawOutilDessus(QPainter &p, const QPointF &c, double r, double angleDeg);
    static void drawCote(QPainter &p, const QPointF &a, const QPointF &b,
                         const QString &txt, bool actif, bool texteADroite = true);
    static void drawFleche(QPainter &p, const QPointF &a, const QPointF &b, const QColor &c);
    static void drawNiveau(QPainter &p, double y, double x0, double x1,
                           const QString &txt, const QColor &c);
    static void drawTitre(QPainter &p, const QString &txt);
};

#endif // HELPANIMWIDGET_H
