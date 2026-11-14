#pragma once
#ifndef SIM2DVIEWS_H
#define SIM2DVIEWS_H
// ═══════════════════════════════════════════════════════════════
//  Sim2DViews — 3 vues orthogonales style plan de dessin technique
//  Vue dessus X/Y pleine largeur en haut
//  Vue de face X/Z  |  Vue latérale Y/Z  en bas
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <QPainter>
#include <QVector>
#include <QVector3D>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QRegularExpression>
#include <functional>
#include <cmath>
#include <algorithm>

#include "gcodeengine.h"
#include "sim3dwidget.h"   // SimMove + PassColor + StockData

// ──────────────────────────────────────────────────────────────
//  Segment résolu XYZ
// ──────────────────────────────────────────────────────────────
struct PlanSeg {
    enum Kind { Rapid, Cut, Arc };
    Kind      kind      = Cut;
    PassColor passColor = PassColor::Normal;
    float     zMid      = 0.f;
    QVector3D p0, p1;
};

static inline QColor segColor(const PlanSeg &s, float zMin, float zMax)
{
    if (s.kind == PlanSeg::Rapid)          return QColor(0xE8,0x2E,0x2E);
    if (s.passColor==PassColor::EbaucheFinale)  return QColor(0x2E,0xE1,0x47);
    if (s.passColor==PassColor::FinitionFinale) return QColor(0xF2,0xD9,0x14);
    float t=(zMax-zMin>0.01f)?qBound(0.f,(zMax-s.zMid)/(zMax-zMin),1.f):0.f;
    return QColor(int(0x38*(1.f-t*0.6f)),int(0x85*(1.f-t*0.5f)),int(0xF2*(1.f-t*0.25f)));
}

// ══════════════════════════════════════════════════════════════
//  PlanView — une vue 2D unique
// ══════════════════════════════════════════════════════════════
class PlanView : public QWidget
{
    Q_OBJECT
public:
    enum Axis { XY, XZ, YZ };

    explicit PlanView(Axis axis, QWidget *parent=nullptr)
        : QWidget(parent), m_axis(axis)
    { setMinimumSize(80,80); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding); setMouseTracking(true); }

    void setSegments(const QVector<PlanSeg> &segs, float zMin, float zMax)
    { m_segs=segs; m_zMin=zMin; m_zMax=zMax; computeBounds(); m_zoom=1.0; m_panOffset={0,0}; update(); }

    void setStock(const StockData &s) { m_stock=s; update(); }
    void setShowRapids(bool v)        { m_showRapids=v; update(); }
    void setToolPos(const QVector3D &p){ m_toolPos=p; m_hasToolPos=true; update(); }
    void clearToolPos()                { m_hasToolPos=false; update(); }
    void fitAll()                      { m_zoom=1.0; m_panOffset={0,0}; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect rc=rect();
        p.fillRect(rc, QColor(0xFA,0xFB,0xFC));
        p.setPen(QPen(QColor(0x80,0x90,0xA8),0.8));
        p.drawRect(rc.adjusted(0,0,-1,-1));

        // Titre
        QFont ft; ft.setFamily("IBM Plex Mono"); ft.setPointSize(8); ft.setBold(true);
        p.setFont(ft); p.setPen(QColor(0x20,0x40,0x80));
        p.drawText(QRect(4,2,width()-8,15),Qt::AlignLeft|Qt::AlignVCenter,axisLabel());

        // Zone dessin — marge gauche plus grande pour les cotations
        const int ML=42, MT=18, MR=4, MB=40;
        m_dr=QRect(ML, MT, width()-ML-MR, height()-MT-MB);
        if (m_dr.width()<20||m_dr.height()<20) return;

        if (m_segs.isEmpty()&&!m_stock.valid) {
            QFont f; f.setPointSize(8); p.setFont(f);
            p.setPen(QColor(0xAA,0xAA,0xBB));
            p.drawText(rc,Qt::AlignCenter,"(aucun programme)");
            return;
        }

        // ── Transformation ────────────────────────────────────
        double rangeX=m_maxU-m_minU; if(rangeX<0.001) rangeX=1.0;
        double rangeY=m_maxV-m_minV; if(rangeY<0.001) rangeY=1.0;
        double scBase=std::min((m_dr.width()-8.0)/rangeX,(m_dr.height()-8.0)/rangeY);
        double sc   = scBase * m_zoom;
        double cx   = m_dr.left()+m_dr.width()/2.0  + m_panOffset.x();
        double cy   = m_dr.top() +m_dr.height()/2.0 + m_panOffset.y();
        double midU =(m_minU+m_maxU)/2.0, midV=(m_minV+m_maxV)/2.0;
        auto toScr=[&](double u,double v)->QPointF{
            return {cx+(u-midU)*sc, cy-(v-midV)*sc};
        };

        drawGrid(p,m_dr,toScr,rangeX,rangeY);

        p.setClipRect(m_dr);

        // ── Brut BLK FORM ─────────────────────────────────────
        if (m_stock.valid) {
            double bu0,bv0,bu1,bv1;
            stockBounds(bu0,bv0,bu1,bv1);
            QPointF bl=toScr(bu0,bv0), tr=toScr(bu1,bv1);
            QRectF br(QPointF(qMin(bl.x(),tr.x()),qMin(bl.y(),tr.y())),
                      QPointF(qMax(bl.x(),tr.x()),qMax(bl.y(),tr.y())));
            p.setBrush(QColor(0x70,0x90,0xB0,35));
            p.setPen(QPen(QColor(0x40,0x60,0x90,180),1.2,Qt::DashLine));
            p.drawRect(br);
            p.setBrush(Qt::NoBrush);
        }

        // ── Parcours outil ────────────────────────────────────
        for (const PlanSeg &seg : std::as_const(m_segs)) {
            if (seg.kind==PlanSeg::Rapid&&!m_showRapids) continue;
            double u0,v0,u1,v1;
            proj(seg.p0,u0,v0); proj(seg.p1,u1,v1);
            QPointF a=toScr(u0,v0), b=toScr(u1,v1);
            QColor col=segColor(seg,m_zMin,m_zMax);
            if (seg.kind==PlanSeg::Rapid) {
                QPen pen(col,0.9,Qt::DashLine); pen.setDashPattern({3.0,3.0}); p.setPen(pen);
                p.drawLine(a,b); drawArrow(p,a,b,col,4.5);
            } else {
                p.setPen(QPen(col,(seg.passColor!=PassColor::Normal)?2.0f:1.2f));
                p.drawLine(a,b);
            }
        }

        // ── Repère origine (0,0) ──────────────────────────────
        drawOriginMarker(p,toScr);

        // ── Position outil ────────────────────────────────────
        if (m_hasToolPos) {
            double u,v; proj(m_toolPos,u,v);
            QPointF pt=toScr(u,v);
            p.setPen(QPen(QColor(0xFF,0x80,0x00),1.5)); p.setBrush(Qt::NoBrush);
            p.drawEllipse(pt,5.0,5.0);
            p.drawLine(pt+QPointF(-7,0),pt+QPointF(7,0));
            p.drawLine(pt+QPointF(0,-7),pt+QPointF(0,7));
        }

        // ── Curseur ───────────────────────────────────────────
        p.setClipping(false);
        if (m_showCursor&&m_dr.contains(m_cursor)) {
            p.setPen(QPen(QColor(0x20,0x40,0x80,90),0.5));
            p.drawLine(m_dr.left(),m_cursor.y(),m_dr.right(),m_cursor.y());
            p.drawLine(m_cursor.x(),m_dr.top(),m_cursor.x(),m_dr.bottom());
            double cu=midU+(m_cursor.x()-cx)/sc, cv=midV-(m_cursor.y()-cy)/sc;
            QFont fc; fc.setFamily("IBM Plex Mono"); fc.setPointSize(7); p.setFont(fc);
            p.setPen(QColor(0x20,0x40,0x80));
            QString coord=QString("%1=%2  %3=%4").arg(uLabel()).arg(cu,0,'f',2).arg(vLabel()).arg(cv,0,'f',2);
            QRect cr(m_dr.left()+2,m_dr.bottom()+2,m_dr.width()-4,11);
            p.fillRect(cr.adjusted(-1,-1,1,1),QColor(0xF0,0xF4,0xFF,200));
            p.drawText(cr,Qt::AlignLeft|Qt::AlignVCenter,coord);
        }

        // ── Cotations BLK FORM ────────────────────────────────
        p.setClipping(false);
        if (m_stock.valid)
            drawDimensions(p,toScr);

        // ── Ticks axes ────────────────────────────────────────
        drawTicks(p,m_dr,toScr,rangeX,rangeY);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        m_cursor=e->pos(); m_showCursor=true;
        if (m_dragging){ m_panOffset+=QPointF(e->pos()-m_lastDrag); m_lastDrag=e->pos(); }
        update();
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button()==Qt::MiddleButton||(e->button()==Qt::LeftButton&&e->modifiers()&Qt::AltModifier))
        { m_dragging=true; m_lastDrag=e->pos(); }
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_dragging=false; }
    void leaveEvent(QEvent *)              override { m_showCursor=false; update(); }

    void wheelEvent(QWheelEvent *e) override {
        // Zoom centré sur le curseur : le point monde sous la souris reste fixe
        double factor = (e->angleDelta().y()>0) ? 1.15 : (1.0/1.15);
        double newZoom = qBound(0.05, m_zoom*factor, 200.0);

        // Coordonnées curseur dans le repère vue
        QPointF pos = e->position();
        double cx = m_dr.left()+m_dr.width()/2.0  + m_panOffset.x();
        double cy = m_dr.top() +m_dr.height()/2.0 + m_panOffset.y();

        // Décalage pan pour garder le point sous la souris fixe
        // pan_new = pan_old + (pos - center_dr) * (1 - factor)
        double dPx = pos.x() - (m_dr.left()+m_dr.width()/2.0);
        double dPy = pos.y() - (m_dr.top() +m_dr.height()/2.0);
        m_panOffset.rx() += dPx*(1.0 - newZoom/m_zoom);
        m_panOffset.ry() += dPy*(1.0 - newZoom/m_zoom);
        Q_UNUSED(cx); Q_UNUSED(cy);

        m_zoom = newZoom;
        update();
    }

private:
    Axis              m_axis;
    QVector<PlanSeg>  m_segs;
    StockData         m_stock;
    float   m_zMin=-50.f, m_zMax=0.f;
    double  m_minU=0,m_maxU=100,m_minV=0,m_maxV=100;
    double  m_zoom=1.0;
    QPointF m_panOffset;
    bool    m_showRapids=true, m_dragging=false, m_showCursor=false, m_hasToolPos=false;
    QPointF m_lastDrag;
    QPoint  m_cursor;
    QVector3D m_toolPos;
    mutable QRect m_dr;   // zone dessin courante, mise à jour dans paintEvent

    // ── Projections ───────────────────────────────────────────
    void proj(const QVector3D &pt, double &u, double &v) const {
        switch(m_axis){case XY:u=pt.x();v=pt.y();break;case XZ:u=pt.x();v=pt.z();break;case YZ:u=pt.y();v=pt.z();break;}
    }
    void stockBounds(double &u0,double &v0,double &u1,double &v1) const {
        switch(m_axis){
        case XY: u0=m_stock.xMin;u1=m_stock.xMax;v0=m_stock.yMin;v1=m_stock.yMax; break;
        case XZ: u0=m_stock.xMin;u1=m_stock.xMax;v0=m_stock.zMin;v1=m_stock.zMax; break;
        case YZ: u0=m_stock.yMin;u1=m_stock.yMax;v0=m_stock.zMin;v1=m_stock.zMax; break;
        }
    }
    QString axisLabel() const {
        switch(m_axis){case XY:return"Vue dessus — X / Y";case XZ:return"Vue de face — X / Z";case YZ:return"Vue lat. — Y / Z";}
        return"";
    }
    QString uLabel() const { return(m_axis==YZ)?"Y":"X"; }
    QString vLabel() const { return(m_axis==XY)?"Y":"Z"; }

    void computeBounds() {
        // Inclure l'origine et le brut dans les bornes initiales
        m_minU=0;m_maxU=1;m_minV=0;m_maxV=1;
        bool any=false;
        auto expand=[&](double u,double v){
            if(!any){m_minU=m_maxU=u;m_minV=m_maxV=v;any=true;}
            else{m_minU=qMin(m_minU,u);m_maxU=qMax(m_maxU,u);m_minV=qMin(m_minV,v);m_maxV=qMax(m_maxV,v);}
        };
        // Origine toujours visible
        expand(0,0);
        // Brut
        if(m_stock.valid){
            double u0,v0,u1,v1; stockBounds(u0,v0,u1,v1);
            expand(u0,v0); expand(u1,v1);
        }
        // Segments
        for(const PlanSeg &s:std::as_const(m_segs)){
            double u,v;
            proj(s.p0,u,v); expand(u,v);
            proj(s.p1,u,v); expand(u,v);
        }
        double pu=(m_maxU-m_minU)*0.08+1.0, pv=(m_maxV-m_minV)*0.08+1.0;
        m_minU-=pu;m_maxU+=pu;m_minV-=pv;m_maxV+=pv;
    }

    // ── Repère à l'origine (0,0) ──────────────────────────────
    void drawOriginMarker(QPainter &p,
                          std::function<QPointF(double,double)> toScr) const
    {
        QPointF o=toScr(0,0);
        if(!m_dr.adjusted(-20,-20,20,20).contains(o.toPoint())) return;
        const double len=18.0;
        // Axe U (rouge X ou Y)
        QColor cu=QColor(0xD0,0x20,0x20);
        p.setPen(QPen(cu,1.5)); p.setBrush(cu);
        QPointF uEnd=toScr(len/m_zoom*0.15,0); // en espace monde relatif → utilise échelle
        // On dessine directement en pixels depuis l'origine
        p.drawLine(o, o+QPointF(len,0));
        drawArrow(p, o, o+QPointF(len,0), cu, 5.0);
        // Axe V (vert Y ou Z)
        QColor cv=QColor(0x10,0x90,0x10);
        p.setPen(QPen(cv,1.5)); p.setBrush(cv);
        p.drawLine(o, o+QPointF(0,-len));
        drawArrow(p, o, o+QPointF(0,-len), cv, 5.0);
        // Labels
        QFont fl; fl.setFamily("IBM Plex Mono"); fl.setPointSize(7); fl.setBold(true);
        p.setFont(fl);
        p.setPen(cu); p.drawText(o+QPointF(len+2,-4), uLabel());
        p.setPen(cv); p.drawText(o+QPointF(4,-len-2), vLabel());
        // Point origine
        p.setPen(Qt::NoPen); p.setBrush(QColor(0x20,0x20,0x20));
        p.drawEllipse(o,2.5,2.5);
    }

    // ── Cotations BLK FORM ────────────────────────────────────
    // Deux niveaux : ←—signe—|—signe—→ puis ←——total——→
    void drawDimensions(QPainter &p,
                        std::function<QPointF(double,double)> toScr) const
    {
        double u0,v0,u1,v1;
        stockBounds(u0,v0,u1,v1);

        QFont fd; fd.setFamily("IBM Plex Mono"); fd.setPointSize(7); p.setFont(fd);

        // ── Cotation horizontale U (bas de la zone dessin) ─────
        drawDimLine(p, toScr, u0, u1, v0,
                    /*horizontal=*/true, /*belowOrLeft=*/true);

        // ── Cotation verticale V (gauche de la zone dessin) ────
        drawDimLine(p, toScr, v0, v1, u0,
                    /*horizontal=*/false, /*belowOrLeft=*/true);
    }

    // Dessine une cote complète avec signe depuis l'origine
    // Si horizontal : cote sur l'axe U à la hauteur vBase
    // Si vertical   : cote sur l'axe V à la position uBase
    void drawDimLine(QPainter &p,
                     std::function<QPointF(double,double)> toScr,
                     double lo, double hi, double base,
                     bool horizontal, bool /*belowOrLeft*/) const
    {
        // Trois points : lo, 0 (origine), hi
        // Deux flèches : lo→0 et 0→hi  (niveau 1)
        // Une flèche   : lo→hi          (niveau 2)

        const double OFF1 = 14.0;  // offset pixel niveau 1 en dessous/gauche de la zone
        const double OFF2 = 26.0;  // offset pixel niveau 2

        QFont fd; fd.setFamily("IBM Plex Mono"); fd.setPointSize(7); p.setFont(fd);

        auto scrPt=[&](double u,double v)->QPointF{ return toScr(u,v); };

        // Points de référence écran
        QPointF pLo, pOr, pHi, pLo2, pHi2;
        if(horizontal){
            pLo  = scrPt(lo,  base); pOr = scrPt(0, base); pHi = scrPt(hi, base);
            // Décalage vers le bas (en pixels)
            double yOff1 = m_dr.bottom()+OFF1;
            double yOff2 = m_dr.bottom()+OFF2;
            pLo  = QPointF(pLo.x(),  yOff1);
            pOr  = QPointF(pOr.x(),  yOff1);
            pHi  = QPointF(pHi.x(),  yOff1);
            pLo2 = QPointF(pLo.x(),  yOff2);
            pHi2 = QPointF(pHi.x(),  yOff2);
        } else {
            pLo  = scrPt(base, lo);  pOr = scrPt(base, 0); pHi = scrPt(base, hi);
            double xOff1 = m_dr.left()-OFF1;
            double xOff2 = m_dr.left()-OFF2;
            pLo  = QPointF(xOff1, pLo.y());
            pOr  = QPointF(xOff1, pOr.y());
            pHi  = QPointF(xOff1, pHi.y());
            pLo2 = QPointF(xOff2, pLo.y());
            pHi2 = QPointF(xOff2, pHi.y());
        }

        QColor cDim(0x30,0x50,0x90,200);
        QPen dimPen(cDim, 0.8); p.setBrush(cDim);

        // ── Niveau 1 : lo→0 et 0→hi avec signe ───────────────
        // Ligne de base niveau 1
        p.setPen(dimPen);
        p.drawLine(pLo, pHi);
        // Segment lo→0
        if(std::abs(lo)>0.01){
            drawArrow(p,pLo,pOr,cDim,4.0);
            drawArrow(p,pOr,pLo,cDim,4.0);
            QString lblLo=QString("%1%2").arg(lo>0?"+":"").arg(lo,0,'f',0);
            drawDimLabel(p,(pLo+pOr)/2.0,lblLo,horizontal,cDim);
        }
        // Segment 0→hi
        if(std::abs(hi)>0.01){
            drawArrow(p,pOr,pHi,cDim,4.0);
            drawArrow(p,pHi,pOr,cDim,4.0);
            QString lblHi=QString("%1%2").arg(hi>0?"+":"").arg(hi,0,'f',0);
            drawDimLabel(p,(pOr+pHi)/2.0,lblHi,horizontal,cDim);
        }
        // Marque à l'origine (niveau 1)
        if(horizontal) p.drawLine(QPointF(pOr.x(),pOr.y()-3),QPointF(pOr.x(),pOr.y()+3));
        else           p.drawLine(QPointF(pOr.x()-3,pOr.y()),QPointF(pOr.x()+3,pOr.y()));

        // ── Niveau 2 : lo→hi global ───────────────────────────
        p.setPen(dimPen);
        p.drawLine(pLo2, pHi2);
        drawArrow(p,pLo2,pHi2,cDim,4.0);
        drawArrow(p,pHi2,pLo2,cDim,4.0);
        double total=hi-lo;
        QString lblTot=QString("%1 mm").arg(total,0,'f',0);
        drawDimLabel(p,(pLo2+pHi2)/2.0,lblTot,horizontal,cDim);

        // Lignes d'attache (tiret vertical/horizontal entre zone et cote)
        QPen attach(cDim,0.5,Qt::DotLine);
        p.setPen(attach);
        if(horizontal){
            QPointF aLo=scrPt(lo,base),aHi=scrPt(hi,base);
            p.drawLine(QPointF(aLo.x(),m_dr.bottom()),QPointF(pLo2.x(),pLo2.y()));
            p.drawLine(QPointF(aHi.x(),m_dr.bottom()),QPointF(pHi2.x(),pHi2.y()));
        } else {
            QPointF aLo=scrPt(base,lo),aHi=scrPt(base,hi);
            p.drawLine(QPointF(m_dr.left(),aLo.y()),QPointF(pLo2.x(),pLo2.y()));
            p.drawLine(QPointF(m_dr.left(),aHi.y()),QPointF(pHi2.x(),pHi2.y()));
        }
    }

    void drawDimLabel(QPainter &p, const QPointF &center, const QString &txt,
                      bool horizontal, const QColor &col) const
    {
        QFont fd; fd.setFamily("IBM Plex Mono"); fd.setPointSize(7); p.setFont(fd);
        QFontMetrics fm(fd);
        int tw=fm.horizontalAdvance(txt), th=fm.height();
        QRect lr(int(center.x())-tw/2-1, int(center.y())-(horizontal?th+1:th/2)-1, tw+2, th+2);
        p.fillRect(lr, QColor(0xFA,0xFB,0xFC,210));
        p.setPen(col);
        p.drawText(lr, Qt::AlignCenter, txt);
    }

    // ── Grille ────────────────────────────────────────────────
    void drawGrid(QPainter &p, const QRect &dr,
                  std::function<QPointF(double,double)> toScr,
                  double rangeX, double rangeY) const
    {
        double sU=niceStep(rangeX), sV=niceStep(rangeY);
        double u0=std::floor(m_minU/sU)*sU, v0=std::floor(m_minV/sV)*sV;
        p.setPen(QPen(QColor(0xD0,0xD8,0xE8),0.5));
        for(int i=0;i<qMax(1,int((m_maxU-u0)/sU)+2);++i){
            double u=u0+i*sU; if(u>m_maxU+sU*0.01)break;
            QPointF a=toScr(u,m_minV);
            if(a.x()>=dr.left()&&a.x()<=dr.right())
                p.drawLine(QPointF(a.x(),dr.top()),QPointF(a.x(),dr.bottom()));
        }
        for(int i=0;i<qMax(1,int((m_maxV-v0)/sV)+2);++i){
            double v=v0+i*sV; if(v>m_maxV+sV*0.01)break;
            QPointF a=toScr(m_minU,v);
            if(a.y()>=dr.top()&&a.y()<=dr.bottom())
                p.drawLine(QPointF(dr.left(),a.y()),QPointF(dr.right(),a.y()));
        }
        // Axe zéro
        p.setPen(QPen(QColor(0xA0,0xB0,0xC8),0.8));
        {QPointF a=toScr(0,m_minV); if(a.x()>=dr.left()&&a.x()<=dr.right()) p.drawLine(QPointF(a.x(),dr.top()),QPointF(a.x(),dr.bottom()));}
        {QPointF a=toScr(m_minU,0); if(a.y()>=dr.top()&&a.y()<=dr.bottom()) p.drawLine(QPointF(dr.left(),a.y()),QPointF(dr.right(),a.y()));}
    }

    void drawTicks(QPainter &p, const QRect &dr,
                   std::function<QPointF(double,double)> toScr,
                   double rangeX, double rangeY) const
    {
        QFont f; f.setFamily("IBM Plex Mono"); f.setPointSize(7); p.setFont(f);
        p.setPen(QColor(0x40,0x58,0x80));
        double sU=niceStep(rangeX), sV=niceStep(rangeY);
        double u0=std::floor(m_minU/sU)*sU, v0=std::floor(m_minV/sV)*sV;
        auto lbl=[](double v,double s)->QString{
            return(std::abs(v)<s*0.01)?"0":(std::abs(v)>=10.0)?QString::number(qRound(v)):QString::number(v,'f',1);
        };
        for(int i=0;i<qMax(1,int((m_maxU-u0)/sU)+2);++i){
            double u=u0+i*sU; if(u>m_maxU+sU*0.01)break;
            QPointF pt=toScr(u,m_minV);
            if(pt.x()<dr.left()-2||pt.x()>dr.right()+2)continue;
            p.drawText(QRect(int(pt.x())-18,dr.bottom()+2,36,11),Qt::AlignCenter,lbl(u,sU));
        }
        for(int i=0;i<qMax(1,int((m_maxV-v0)/sV)+2);++i){
            double v=v0+i*sV; if(v>m_maxV+sV*0.01)break;
            QPointF pt=toScr(m_minU,v);
            if(pt.y()<dr.top()-2||pt.y()>dr.bottom()+2)continue;
            p.drawText(QRect(0,int(pt.y())-6,dr.left()-2,12),Qt::AlignRight|Qt::AlignVCenter,lbl(v,sV));
        }
        QFont fb=f; fb.setBold(true); p.setFont(fb); p.setPen(QColor(0x10,0x30,0x70));
        p.drawText(QRect(dr.right()-14,dr.bottom()+2,14,11),Qt::AlignRight,uLabel());
        p.drawText(QRect(0,dr.top(),dr.left()-2,12),Qt::AlignRight|Qt::AlignTop,vLabel());
    }

    static double niceStep(double range,int target=6){
        if(range<=0)return 1.0;
        double ex=std::pow(10.0,std::floor(std::log10(range/target)));
        double fr=(range/target)/ex;
        return((fr<1.5)?1.0:(fr<3.5)?2.0:(fr<7.5)?5.0:10.0)*ex;
    }

    static void drawArrow(QPainter &p,const QPointF &a,const QPointF &b,
                          const QColor &col,double sz){
        double len=std::hypot(b.x()-a.x(),b.y()-a.y()); if(len<sz*2)return;
        double angle=std::atan2(b.y()-a.y(),b.x()-a.x());
        QPointF mid=(a+b)/2.0; double da=0.45;
        QPolygonF tri; tri<<mid
            <<mid-QPointF(sz*std::cos(angle-da),sz*std::sin(angle-da))
            <<mid-QPointF(sz*std::cos(angle+da),sz*std::sin(angle+da));
        p.setPen(Qt::NoPen); p.setBrush(col); p.drawPolygon(tri); p.setBrush(Qt::NoBrush);
    }
};


// ══════════════════════════════════════════════════════════════
//  Sim2DViews — conteneur : dessus en haut + face/latérale en bas
// ══════════════════════════════════════════════════════════════
class Sim2DViews : public QWidget
{
    Q_OBJECT
public:
    explicit Sim2DViews(QWidget *parent=nullptr): QWidget(parent)
    {
        setStyleSheet("background:#EEF0F4;");
        auto *root=new QVBoxLayout(this);
        root->setContentsMargins(4,4,4,4); root->setSpacing(3);
        root->addWidget(buildLegend());

        const QString fs="border:1px solid #8090A8;background:#FAFBFC;border-radius:2px;";
        m_vXY=new PlanView(PlanView::XY,this); m_vXY->setStyleSheet(fs); m_vXY->setMinimumHeight(180);
        root->addWidget(m_vXY,3);

        auto *bot=new QHBoxLayout; bot->setSpacing(3);
        m_vXZ=new PlanView(PlanView::XZ,this); m_vXZ->setStyleSheet(fs); m_vXZ->setMinimumHeight(100);
        m_vYZ=new PlanView(PlanView::YZ,this); m_vYZ->setStyleSheet(fs); m_vYZ->setMinimumHeight(100);
        bot->addWidget(m_vXZ,3); bot->addWidget(m_vYZ,2);
        root->addLayout(bot,2);

        m_lblInfo=new QLabel("Aucun programme");
        m_lblInfo->setStyleSheet("font-family:'IBM Plex Mono';font-size:9px;color:#506080;padding:2px 8px;");
        root->addWidget(m_lblInfo);
    }

    void setStock(const StockData &s){
        m_vXY->setStock(s); m_vXZ->setStock(s); m_vYZ->setStock(s);
    }

    void loadFromMoves(const QVector<SimMove> &moves)
    {
        QVector<PlanSeg> segs; segs.reserve(moves.size()*4);
        for(const SimMove &mv:std::as_const(moves)){
            PlanSeg::Kind k=(mv.kind==SimMove::Rapid)?PlanSeg::Rapid:PlanSeg::Cut;
            for(int i=0;i+1<mv.pts.size();++i){
                PlanSeg s; s.kind=k; s.passColor=mv.passColor; s.zMid=mv.zMid;
                s.p0=mv.pts[i]; s.p1=mv.pts[i+1]; segs.append(s);
            }
        }
        applySegments(segs);
    }

    void loadFromGCode(const QVector<GCodeLine> &lines)
    {
        // ── Pré-scan PassColor : logique G41→G40 identique à Sim3DWidget ──
        static const QRegularExpression reG41(R"(\bG41\b)");
        static const QRegularExpression reG40(R"(\bG40\b)");
        static const QRegularExpression reFinition(
            R"(finition|finishing)", QRegularExpression::CaseInsensitiveOption);
        struct G41Bloc { int s,e; bool isFinition; };
        QVector<G41Bloc> blocs;
        int g41Start=-1; bool inG41=false;
        for(int li=0;li<lines.size();++li){
            const QString up=lines[li].code.toUpper();
            if(up.contains(reG41)&&!inG41){ inG41=true; g41Start=li; }
            if(up.contains(reG40)&&inG41){
                inG41=false; bool isFin=false;
                for(int k=qMax(0,g41Start-15);k<=g41Start;++k){
                    if(lines[k].type!=GCodeLine::Comment) continue;
                    if(reFinition.match(lines[k].code+' '+lines[k].comment).hasMatch())
                    { isFin=true; break; }
                }
                blocs.append({g41Start,li,isFin}); g41Start=-1;
            }
        }
        QVector<PassColor> blocColors(blocs.size(), PassColor::Normal);
        for(int bi=0;bi<blocs.size();++bi)
            if(blocs[bi].isFinition) blocColors[bi]=PassColor::FinitionFinale;
        for(int bi=0;bi<blocs.size();){
            if(!blocs[bi].isFinition){
                int gEnd=bi;
                while(gEnd+1<blocs.size()&&!blocs[gEnd+1].isFinition) ++gEnd;
                blocColors[gEnd]=PassColor::EbaucheFinale; bi=gEnd+1;
            } else { ++bi; }
        }
        QVector<PassColor> linePassColor(lines.size(), PassColor::Normal);
        for(int i=0;i<blocs.size();++i)
            for(int li=blocs[i].s;li<=blocs[i].e;++li)
                linePassColor[li]=blocColors[i];

        QVector<PlanSeg> segs; segs.reserve(lines.size()*2);
        double x=0,y=0,z=0; int mode=0;
        double rotAngle=0.0;  // G68 rotation en radians
        static const QRegularExpression reG68(R"(\bG68\b)");
        static const QRegularExpression reG69(R"(\bG69\b)");
        for(int li=0;li<lines.size();++li){
            const GCodeLine &gl=lines[li];
            if(gl.type==GCodeLine::Comment||gl.type==GCodeLine::Blank) continue;
            PassColor curPC=(li<linePassColor.size())?linePassColor[li]:PassColor::Normal;
            // G68/G69
            QString up=gl.code.toUpper();
            if(up.contains(reG68)){ double rv=0; pv(gl.code,'R',rv); rotAngle=rv*M_PI/180.0; }
            if(up.contains(reG69)){ rotAngle=0.0; }
            updateMode(gl,mode);
            double nx=x,ny=y,nz=z; bool mv=false; double tmp;
            if(pv(gl.code,'X',tmp)){nx=tmp;mv=true;}
            if(pv(gl.code,'Y',tmp)){ny=tmp;mv=true;}
            if(pv(gl.code,'Z',tmp)){nz=tmp;mv=true;}
            // Appliquer rotation G68
            if(rotAngle!=0.0 && mv){
                double cosA=cos(rotAngle), sinA=sin(rotAngle);
                double rx=nx*cosA - ny*sinA;
                double ry=nx*sinA + ny*cosA;
                nx=rx; ny=ry;
            }
            if(mv){
                if(mode==2||mode==3){
                    double ii=0,jj=0,rr=0; bool hasR=pv(gl.code,'R',rr);
                    pv(gl.code,'I',ii); pv(gl.code,'J',jj);
                    bool cw=(mode==2);
                    QVector<QVector3D> arc=hasR
                                                 ?tessArcR({float(x),float(y),float(z)},{float(nx),float(ny),float(nz)},rr,cw)
                                                 :tessArc ({float(x),float(y),float(z)},{float(nx),float(ny),float(nz)},x+ii,y+jj,cw);
                    for(int i=0;i+1<arc.size();++i){
                        PlanSeg s; s.kind=PlanSeg::Arc; s.passColor=curPC;
                        s.zMid=float((arc[i].z()+arc[i+1].z())/2.f);
                        s.p0=arc[i]; s.p1=arc[i+1]; segs.append(s);
                    }
                } else {
                    PlanSeg s;
                    s.kind=(mode==0)?PlanSeg::Rapid:PlanSeg::Cut;
                    s.passColor=(mode==0)?PassColor::Normal:curPC;
                    s.zMid=float((z+nz)/2.0);
                    s.p0={float(x),float(y),float(z)};
                    s.p1={float(nx),float(ny),float(nz)};
                    segs.append(s);
                }
            }
            x=nx;y=ny;z=nz;
        }
        applySegments(segs);
    }

    void setShowRapids(bool v){m_vXY->setShowRapids(v);m_vXZ->setShowRapids(v);m_vYZ->setShowRapids(v);}
    void setToolPos(const QVector3D &pos){m_vXY->setToolPos(pos);m_vXZ->setToolPos(pos);m_vYZ->setToolPos(pos);}
    void clearToolPos(){m_vXY->clearToolPos();m_vXZ->clearToolPos();m_vYZ->clearToolPos();}
    void fitAll(){m_vXY->fitAll();m_vXZ->fitAll();m_vYZ->fitAll();}

private:
    PlanView *m_vXY=nullptr, *m_vXZ=nullptr, *m_vYZ=nullptr;
    QLabel   *m_lblInfo=nullptr;

    void applySegments(const QVector<PlanSeg> &segs)
    {
        float zMin=1e9f,zMax=-1e9f; bool anyZ=false;
        for(const PlanSeg &s:std::as_const(segs))
            if(s.kind!=PlanSeg::Rapid){zMin=qMin(zMin,s.zMid);zMax=qMax(zMax,s.zMid);anyZ=true;}
        if(!anyZ){zMin=-10.f;zMax=0.f;}
        m_vXY->setSegments(segs,zMin,zMax);
        m_vXZ->setSegments(segs,zMin,zMax);
        m_vYZ->setSegments(segs,zMin,zMax);
        if(segs.isEmpty()){m_lblInfo->setText("Aucun segment");return;}
        float xMn=1e9f,xMx=-1e9f,yMn=1e9f,yMx=-1e9f; int nR=0,nC=0,nA=0;
        for(const PlanSeg &s:std::as_const(segs)){
            auto upd=[&](const QVector3D &pt){
                xMn=qMin(xMn,pt.x());xMx=qMax(xMx,pt.x());
                yMn=qMin(yMn,pt.y());yMx=qMax(yMx,pt.y());};
            upd(s.p0);upd(s.p1);
            if(s.kind==PlanSeg::Rapid)++nR; else if(s.kind==PlanSeg::Arc)++nA; else++nC;
        }
        m_lblInfo->setText(QString("X[%1…%2]  Y[%3…%4]  Z[%5…%6] mm  —  G0:%7  G1:%8  Arc:%9")
                               .arg(xMn,0,'f',1).arg(xMx,0,'f',1).arg(yMn,0,'f',1).arg(yMx,0,'f',1)
                               .arg(zMin,0,'f',1).arg(zMax,0,'f',1).arg(nR).arg(nC).arg(nA));
    }

    QWidget *buildLegend()
    {
        auto *w=new QWidget; w->setFixedHeight(20); w->setStyleSheet("background:transparent;");
        auto *lh=new QHBoxLayout(w); lh->setContentsMargins(8,0,8,0); lh->setSpacing(14);
        struct Li{const char*hex;const char*label;bool dash;};
        for(const Li &it:{Li{"#E82E2E","G0 Rapide",true},Li{"#3885F2","G1 Usinage",false},
                             Li{"#2EE147","Bord ébauche",false},Li{"#F2D914","Bord finition",false}}){
            auto *lbl=new QLabel; lbl->setTextFormat(Qt::RichText);
            QString ico=it.dash
                              ?QString("<span style='color:%1;letter-spacing:1px'>- - -</span>").arg(it.hex)
                              :QString("<span style='color:%1;font-weight:900;font-size:13px'>⬛</span>").arg(it.hex);
            lbl->setText(ico+QString("&nbsp;<span style='font-size:10px;color:#203060;"
                                       "font-family:IBM Plex Mono'>%1</span>").arg(it.label));
            lh->addWidget(lbl);
        }
        lh->addStretch(); return w;
    }

    // ── Parsing ───────────────────────────────────────────────
    // Cherche la lettre ch (MAJUSCULE uniquement) suivie d'un nombre.
    // Ignore TOUT ce qui se trouve dans les commentaires inline (...).
    // Vérifie que ch n'est pas précédé d'une lettre (mot isolé) pour
    // éviter de matcher "x" dans "mm/dt x3Z" etc.
    static bool pv(const QString &c, QChar ch, double &out){
        // Tronquer au premier '(' ou ';' pour ignorer les commentaires inline
        int cutAt = c.size();
        for(int k=0;k<c.size();++k){
            QChar cc=c[k];
            if(cc=='(' || cc==';') { cutAt=k; break; }
        }
        // Chercher la lettre MAJUSCULE (pas de toUpper sur le texte)
        const QChar chU = ch.toUpper();
        int i=-1;
        for(int k=0;k<cutAt;++k){
            if(c[k]==chU){
                // Vérifier que le caractère précédent n'est pas une lettre
                // (évite de matcher "X" dans "MAX" par exemple)
                if(k>0 && c[k-1].isLetter()) continue;
                i=k; break;
            }
        }
        if(i<0) return false;
        int j=i+1; while(j<cutAt && c[j]==' ') ++j;
        int st=j; if(j<cutAt && (c[j]=='-' || c[j]=='+')) ++j;
        while(j<cutAt && (c[j].isDigit() || c[j]=='.')) ++j;
        if(j<=st) return false;
        bool ok; out=c.mid(st,j-st).toDouble(&ok); return ok;
    }
    static void updateMode(const GCodeLine &gl, int &mode){
        if(gl.type==GCodeLine::Rapid) {mode=0;return;}
        if(gl.type==GCodeLine::Linear){mode=1;return;}
        const QString up=gl.code.toUpper();
        if(up.contains(QRegularExpression("\\bG0*0\\b")))      mode=0;
        else if(up.contains(QRegularExpression("\\bG0*1\\b"))) mode=1;
        else if(up.contains(QRegularExpression("\\bG0*2\\b"))) mode=2;
        else if(up.contains(QRegularExpression("\\bG0*3\\b"))) mode=3;
    }
    static QVector<QVector3D> tessArc(QVector3D from,QVector3D to,
                                      double cx,double cy,bool cw,int steps=48){
        double r=std::hypot(from.x()-cx,from.y()-cy);
        double a0=std::atan2(from.y()-cy,from.x()-cx),a1=std::atan2(to.y()-cy,to.x()-cx);
        if(cw&&a1>=a0)a1-=M_PI*2; if(!cw&&a1<=a0)a1+=M_PI*2;
        QVector<QVector3D> pts; pts.reserve(steps+1);
        float dz=(to.z()-from.z())/steps;
        for(int i=0;i<=steps;++i){
            double a=a0+(a1-a0)*double(i)/steps;
            pts.append({float(cx+std::cos(a)*r),float(cy+std::sin(a)*r),from.z()+dz*i});
        }
        return pts;
    }
    static QVector<QVector3D> tessArcR(QVector3D from,QVector3D to,
                                       double R,bool cw,int steps=48){
        double dx=to.x()-from.x(),dy=to.y()-from.y(),d=std::hypot(dx,dy);
        if(d<1e-9||std::abs(R)<d/2.0)return{from,to};
        double h=std::sqrt(R*R-d*d/4.0);
        double mx=(from.x()+to.x())/2.0,my=(from.y()+to.y())/2.0;
        double nx=-dy/d,ny=dx/d,sign=(R>0?1.0:-1.0)*(cw?-1.0:1.0);
        return tessArc(from,to,mx+sign*h*nx,my+sign*h*ny,cw,steps);
    }
};

#endif // SIM2DVIEWS_H
