#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  contourelement.h — Élément de contour EazyNC
//
//  7 types d'éléments, identiques au dialogue Heidenhain :
//    L   Droite               X2 Y2  comp  F  M
//    CHF Chanfrein            longueur  F
//    RND Arrondi d'angle      R  F
//    CC  Centre de cercle     CX CY
//    C   Arc (centre CC)      X2 Y2  DR+/-  comp  F  M
//    CR  Arc (rayon)          X2 Y2  R  DR+/-  comp  F  M
//    CT  Arc tangentiel       X2 Y2  comp  F  M
// ═══════════════════════════════════════════════════════════════
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>

// ───────────────────────────────────────────────────────────────
//  ContourElement
// ───────────────────────────────────────────────────────────────
struct ContourElement
{
    enum Type { L=0, CHF, RND, CC, C, CR, CT };
    enum Comp { R0=0, RL, RR };

    Type    type   = L;

    // Point final (L, C, CR, CT)
    double  x2     = 0.0;
    double  y2     = 0.0;

    // Centre de cercle (CC, C)
    double  cx     = 0.0;
    double  cy     = 0.0;

    // Rayon (CR, RND)
    double  r      = 0.0;

    // Sens rotation (C, CR) : true = DR+ (CCW), false = DR- (CW)
    bool    drPlus = true;

    // Correction rayon (L, C, CR, CT)
    Comp    comp   = R0;

    // Avance (L, CHF, RND, C, CR, CT)
    double  f      = 0.0;    // 0 = FMAX

    // Longueur chanfrein (CHF)
    double  longueur = 0.0;

    // Fonction auxiliaire M optionnelle (L, C, CR, CT)
    int     mFunc  = -1;    // -1 = aucune

    // Résumé lisible pour la liste
    QString summary() const;

    // JSON
    QJsonObject toJson()                  const;
    static ContourElement fromJson(const QJsonObject &o);
};

// ───────────────────────────────────────────────────────────────
//  ContourFile — fichier .cnt complet
// ───────────────────────────────────────────────────────────────
struct ContourFile
{
    QString                 name;       // ex: "Profil_bride"
    QString                 filePath;   // chemin .cnt sur disque
    QVector<ContourElement> elements;

    bool    save(const QString &path = {});
    bool    load(const QString &path);

    QJsonObject toJson()                  const;
    static ContourFile fromJson(const QJsonObject &o);

    // Calcule les points du tracé pour le rendu graphique
    // Retourne une liste de polylignes (chaque polyligne = un arc ou une droite)
    struct Segment { QVector<QPointF> pts; bool isArc; };
    QVector<Segment> toSegments(int arcSteps = 48) const;

    // Dernier point du contour (pour pré-remplir la prochaine saisie)
    QPointF lastPoint() const;
};

// ───────────────────────────────────────────────────────────────
//  Implémentation inline
// ───────────────────────────────────────────────────────────────
#include <QFile>
#include <QJsonDocument>
#include <QtMath>
#include <cmath>

inline QString ContourElement::summary() const
{
    auto fc = [](double v){ return QString::number(v,'f',3); };
    switch(type){
    case L:   return QString("L  X%1  Y%2  %3  F%4")
                  .arg(fc(x2)).arg(fc(y2))
                  .arg(comp==RL?"RL":comp==RR?"RR":"R0")
                  .arg(f==0?"MAX":fc(f));
    case CHF: return QString("CHF  %1  F%2").arg(fc(longueur)).arg(f==0?"MAX":fc(f));
    case RND: return QString("RND  R%1  F%2").arg(fc(r)).arg(f==0?"MAX":fc(f));
    case CC:  return QString("CC  X%1  Y%2").arg(fc(cx)).arg(fc(cy));
    case C:   return QString("C  X%1  Y%2  %3  %4")
                  .arg(fc(x2)).arg(fc(y2))
                  .arg(drPlus?"DR+":"DR-")
                  .arg(comp==RL?"RL":comp==RR?"RR":"R0");
    case CR:  return QString("CR  X%1  Y%2  R%3  %4  %5")
                  .arg(fc(x2)).arg(fc(y2)).arg(fc(r))
                  .arg(drPlus?"DR+":"DR-")
                  .arg(comp==RL?"RL":comp==RR?"RR":"R0");
    case CT:  return QString("CT  X%1  Y%2  %3")
                  .arg(fc(x2)).arg(fc(y2))
                  .arg(comp==RL?"RL":comp==RR?"RR":"R0");
    }
    return "?";
}

inline QJsonObject ContourElement::toJson() const
{
    QJsonObject o;
    o["type"]     = (int)type;
    o["x2"]       = x2;  o["y2"] = y2;
    o["cx"]       = cx;  o["cy"] = cy;
    o["r"]        = r;
    o["drPlus"]   = drPlus;
    o["comp"]     = (int)comp;
    o["f"]        = f;
    o["longueur"] = longueur;
    o["mFunc"]    = mFunc;
    return o;
}

inline ContourElement ContourElement::fromJson(const QJsonObject &o)
{
    ContourElement e;
    e.type     = (Type)o["type"].toInt();
    e.x2       = o["x2"].toDouble();   e.y2 = o["y2"].toDouble();
    e.cx       = o["cx"].toDouble();   e.cy = o["cy"].toDouble();
    e.r        = o["r"].toDouble();
    e.drPlus   = o["drPlus"].toBool(true);
    e.comp     = (Comp)o["comp"].toInt();
    e.f        = o["f"].toDouble();
    e.longueur = o["longueur"].toDouble();
    e.mFunc    = o["mFunc"].toInt(-1);
    return e;
}

inline QJsonObject ContourFile::toJson() const
{
    QJsonObject o;
    o["name"] = name;
    QJsonArray arr;
    for(const ContourElement &e : elements) arr << e.toJson();
    o["elements"] = arr;
    return o;
}

inline ContourFile ContourFile::fromJson(const QJsonObject &o)
{
    ContourFile f;
    f.name = o["name"].toString();
    for(const auto &v : o["elements"].toArray())
        f.elements << ContourElement::fromJson(v.toObject());
    return f;
}

inline bool ContourFile::save(const QString &path)
{
    QString p = path.isEmpty() ? filePath : path;
    if(p.isEmpty()) return false;
    filePath = p;
    QFile f(p);
    if(!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson());
    return true;
}

inline bool ContourFile::load(const QString &path)
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if(err.error != QJsonParseError::NoError) return false;
    *this = fromJson(doc.object());
    filePath = path;
    return true;
}

inline QPointF ContourFile::lastPoint() const
{
    for(int i = elements.size()-1; i >= 0; --i){
        const ContourElement &e = elements[i];
        switch(e.type){
        case ContourElement::L:
        case ContourElement::C:
        case ContourElement::CR:
        case ContourElement::CT:
            return QPointF(e.x2, e.y2);
        default: break;
        }
    }
    return QPointF(0,0);
}

inline QVector<ContourFile::Segment> ContourFile::toSegments(int arcSteps) const
{
    QVector<Segment> segs;
    QPointF cur(0,0);
    double ccx=0, ccy=0;

    // Pré-calcul avec lookahead CHF/RND
    struct RawSeg { bool isArc; QVector<QPointF> pts; };
    QVector<RawSeg> raw;

    for (int ei = 0; ei < elements.size(); ++ei) {
        const ContourElement &el = elements[ei];
        // Regarder si l'élément suivant est CHF ou RND
        const ContourElement *mod = nullptr;
        if (ei+1 < elements.size() &&
            (elements[ei+1].type==ContourElement::CHF ||
             elements[ei+1].type==ContourElement::RND))
            mod = &elements[ei+1];

        switch (el.type) {
        case ContourElement::CC:
            ccx=el.cx; ccy=el.cy; break;

        case ContourElement::L:
        case ContourElement::CT: {
            if (ei==0) { cur=QPointF(el.x2,el.y2); break; } // premier = départ
            QPointF dst(el.x2,el.y2);
            if (mod && mod->type==ContourElement::CHF && ei+2<elements.size()) {
                const ContourElement &nxt=elements[ei+2];
                if (nxt.type==ContourElement::L||nxt.type==ContourElement::CT) {
                    double L=mod->longueur;
                    double dx=dst.x()-cur.x(), dy=dst.y()-cur.y();
                    double d=std::sqrt(dx*dx+dy*dy);
                    if (d>L) {
                        QPointF cut1(cur.x()+dx/d*(d-L), cur.y()+dy/d*(d-L));
                        raw.append({false,{cur,cut1}});
                        double dx2=nxt.x2-dst.x(), dy2=nxt.y2-dst.y();
                        double d2=std::sqrt(dx2*dx2+dy2*dy2);
                        if (d2>L) {
                            QPointF cut2(dst.x()+dx2/d2*L, dst.y()+dy2/d2*L);
                            raw.append({false,{cut1,cut2}});
                            cur=cut2;
                        } else { cur=cut1; }
                        break;
                    }
                }
            } else if (mod && mod->type==ContourElement::RND && ei+2<elements.size()) {
                const ContourElement &nxt=elements[ei+2];
                if (nxt.type==ContourElement::L||nxt.type==ContourElement::CT) {
                    double R=mod->r;
                    double dx=dst.x()-cur.x(), dy=dst.y()-cur.y();
                    double d=std::sqrt(dx*dx+dy*dy);
                    if (d>R) {
                        QPointF cut1(cur.x()+dx/d*(d-R), cur.y()+dy/d*(d-R));
                        raw.append({false,{cur,cut1}});
                        double dx2=nxt.x2-dst.x(), dy2=nxt.y2-dst.y();
                        double d2=std::sqrt(dx2*dx2+dy2*dy2);
                        if (d2>R) {
                            QPointF cut2(dst.x()+dx2/d2*R, dst.y()+dy2/d2*R);
                            double cross=dx/d*dy2/d2-dy/d*dx2/d2;
                            double sign=(cross>0)?1.0:-1.0;
                            double cx=cut1.x()+sign*R*(-dy/d);
                            double cy=cut1.y()+sign*R*(dx/d);
                            double a1=std::atan2(cut1.y()-cy, cut1.x()-cx);
                            double a2=std::atan2(cut2.y()-cy, cut2.x()-cx);
                            bool ccw=(cross>0);
                            if(ccw){while(a2<a1)a2+=2*M_PI;}
                            else   {while(a2>a1)a2-=2*M_PI;}
                            int n=qMax(4,int(arcSteps*std::abs(a2-a1)/(2*M_PI)));
                            RawSeg arc; arc.isArc=true;
                            for(int k=0;k<=n;++k){
                                double a=a1+k*(a2-a1)/n;
                                arc.pts<<QPointF(cx+R*std::cos(a),cy+R*std::sin(a));
                            }
                            raw.append(arc);
                            cur=cut2; break;
                        }
                    }
                }
            }
            // Pas de CHF/RND applicable : segment normal
            raw.append({false,{cur,dst}});
            cur=dst;
            break;
        }

        case ContourElement::CR: {
            double dx=el.x2-cur.x(),dy=el.y2-cur.y(),d=std::sqrt(dx*dx+dy*dy);
            if(d<1e-9||el.r<d/2.0){ raw.append({false,{cur,QPointF(el.x2,el.y2)}}); }
            else {
                double h2=std::sqrt(el.r*el.r-(d/2)*(d/2));
                double mx=(cur.x()+el.x2)/2,my=(cur.y()+el.y2)/2;
                double px=-dy/d,py=dx/d;
                double sg=el.drPlus?1.0:-1.0;
                double ocx=mx+sg*h2*px,ocy=my+sg*h2*py;
                double a1=std::atan2(cur.y()-ocy,cur.x()-ocx);
                double a2=std::atan2(el.y2-ocy,el.x2-ocx);
                if(el.drPlus){while(a2<a1)a2+=2*M_PI;}
                else          {while(a2>a1)a2-=2*M_PI;}
                int n=qMax(2,int(arcSteps*std::abs(a2-a1)/(2*M_PI)));
                RawSeg arc; arc.isArc=true;
                for(int k=0;k<=n;++k){
                    double a=a1+k*(a2-a1)/n;
                    arc.pts<<QPointF(ocx+el.r*std::cos(a),ocy+el.r*std::sin(a));
                }
                raw.append(arc);
            }
            cur=QPointF(el.x2,el.y2); break;
        }

        case ContourElement::C: {
            double a1=std::atan2(cur.y()-ccy,cur.x()-ccx);
            double a2=std::atan2(el.y2-ccy,el.x2-ccx);
            double rr=std::sqrt((cur.x()-ccx)*(cur.x()-ccx)+(cur.y()-ccy)*(cur.y()-ccy));
            if(el.drPlus){while(a2<a1)a2+=2*M_PI;}
            else          {while(a2>a1)a2-=2*M_PI;}
            int n=qMax(2,int(arcSteps*std::abs(a2-a1)/(2*M_PI)));
            RawSeg arc; arc.isArc=true;
            for(int k=0;k<=n;++k){
                double a=a1+k*(a2-a1)/n;
                arc.pts<<QPointF(ccx+rr*std::cos(a),ccy+rr*std::sin(a));
            }
            raw.append(arc); cur=QPointF(el.x2,el.y2); break;
        }

        case ContourElement::CHF:
        case ContourElement::RND:
            break;  // traités en lookahead

        default: break;
        }
    }

    for (const RawSeg &rs : raw) {
        if (rs.pts.size()<2) continue;
        Segment s; s.isArc=rs.isArc; s.pts=rs.pts;
        segs << s;
    }
    return segs;
}
