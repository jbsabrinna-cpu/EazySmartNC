// ═══════════════════════════════════════════════════════════════
//  cadimporter.cpp — Lecteur DXF / STEP / IGES pour EazyNC
// ═══════════════════════════════════════════════════════════════

// ── OCC includes — AVANT tous les autres ──────────────────────
#ifdef WITH_OCC
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <STEPControl_Reader.hxx>
#  include <IGESControl_Reader.hxx>
#  include <IFSelect_ReturnStatus.hxx>
#  include <TopoDS_Shape.hxx>
#  include <TopoDS_Edge.hxx>
#  include <TopoDS_Wire.hxx>
#  include <TopoDS_Face.hxx>
#  include <TopoDS.hxx>
#  include <TopExp_Explorer.hxx>
#  include <TopAbs_ShapeEnum.hxx>
#  include <BRep_Tool.hxx>
#  include <BRepAdaptor_Curve.hxx>
#  include <GeomAbs_CurveType.hxx>
#  include <gp_Pnt.hxx>
#  include <gp_Circ.hxx>
#  include <gp_Lin.hxx>
#  include <GCPnts_UniformAbscissa.hxx>
#endif

#include "cadimporter.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>

// ───────────────────────────────────────────────────────────────
//  CadLayer::colorFromIndex — table couleurs DXF (ACI)
// ───────────────────────────────────────────────────────────────
QColor CadLayer::colorFromIndex(int idx)
{
    // Couleurs ACI standard DXF (les 9 premières)
    static const QColor aci[] = {
        Qt::black,        // 0 = BYBLOCK
        Qt::red,          // 1
        Qt::yellow,       // 2
        Qt::green,        // 3
        Qt::cyan,         // 4
        Qt::blue,         // 5
        QColor(255,0,255),// 6 magenta
        Qt::white,        // 7
        QColor(64,64,64), // 8 gris foncé
        QColor(128,128,128),//9 gris clair
    };
    if(idx<0) return Qt::white;
    if(idx<10) return aci[qBound(0,idx,9)];
    // Pour les autres, palette fixe cyclique
    static const QColor ext[] = {
                                 {255,0,0},{255,127,0},{255,255,0},{0,255,0},
                                 {0,255,255},{0,0,255},{127,0,255},{255,0,127},
                                 };
    return ext[idx % 8];
}

// ───────────────────────────────────────────────────────────────
//  CadSegment — implémentation
// ───────────────────────────────────────────────────────────────
static QPointF polarPt(double cx,double cy,double r,double deg)
{
    double rad = qDegreesToRadians(deg);
    return QPointF(cx + r*std::cos(rad), cy + r*std::sin(rad));
}

QPointF CadSegment::startPt() const
{
    switch(type){
    case Line:   return QPointF(x1,y1);
    case Arc:    return polarPt(x1,y1,radius,angle1);
    case Circle: return polarPt(x1,y1,radius,0.0);
    }
    return {};
}

QPointF CadSegment::endPt() const
{
    switch(type){
    case Line:   return QPointF(x2,y2);
    case Arc:    return polarPt(x1,y1,radius,angle2);
    case Circle: return polarPt(x1,y1,radius,0.0); // fermé
    }
    return {};
}

double CadSegment::length() const
{
    switch(type){
    case Line: {
        double dx=x2-x1,dy=y2-y1;
        return std::sqrt(dx*dx+dy*dy);
    }
    case Arc: {
        double span = angle2 - angle1;
        if(!ccw) span = -span;
        while(span<0) span+=360.0;
        return radius * qDegreesToRadians(span);
    }
    case Circle:
        return 2.0*M_PI*radius;
    }
    return 0;
}

QVector<QPointF> CadSegment::toPolyline(int arcSegments) const
{
    QVector<QPointF> pts;
    switch(type){
    case Line:
        pts << QPointF(x1,y1) << QPointF(x2,y2);
        break;
    case Arc: {
        double a1=angle1, a2=angle2;
        if(ccw){ while(a2<=a1) a2+=360.0; }
        else   { while(a2>=a1) a2-=360.0; }
        double span=qAbs(a2-a1);
        int n=qMax(2,int(arcSegments*span/360.0));
        for(int i=0;i<=n;++i){
            double t=double(i)/n;
            double a=ccw ? a1+t*(a2-a1) : a1+t*(a2-a1);
            pts << polarPt(x1,y1,radius,a);
        }
        break;
    }
    case Circle: {
        for(int i=0;i<=arcSegments;++i){
            double a=360.0*i/arcSegments;
            pts << polarPt(x1,y1,radius,a);
        }
        break;
    }
    }
    return pts;
}

// ───────────────────────────────────────────────────────────────
//  CadContour — implémentation
// ───────────────────────────────────────────────────────────────
QRectF CadContour::bbox() const
{
    if(segments.isEmpty()) return {};
    QVector<QPointF> pts=toPolyline(16);
    if(pts.isEmpty()) return {};
    double xMin=pts[0].x(),xMax=pts[0].x(),yMin=pts[0].y(),yMax=pts[0].y();
    for(const QPointF&p:pts){
        xMin=qMin(xMin,p.x());xMax=qMax(xMax,p.x());
        yMin=qMin(yMin,p.y());yMax=qMax(yMax,p.y());
    }
    return QRectF(xMin,yMin,xMax-xMin,yMax-yMin);
}

double CadContour::length() const
{
    double l=0;
    for(const CadSegment&s:segments) l+=s.length();
    return l;
}

double CadContour::diameter() const
{
    QRectF b=bbox();
    return qMax(b.width(),b.height());
}

QVector<QPointF> CadContour::toPolyline(int arcSegments) const
{
    QVector<QPointF> pts;
    for(const CadSegment&s:segments){
        auto sp=s.toPolyline(arcSegments);
        if(!pts.isEmpty()&&!sp.isEmpty()&&
            (pts.last()-sp.first()).manhattanLength()<0.001)
            sp.removeFirst();
        pts.append(sp);
    }
    return pts;
}

bool CadContour::contains(const QPointF& pt) const
{
    if(!closed) return false;
    QVector<QPointF> poly=toPolyline(32);
    if(poly.size()<3) return false;
    // Ray-casting algorithm
    int n=poly.size(); bool inside=false;
    double x=pt.x(),y=pt.y();
    for(int i=0,j=n-1;i<n;j=i++){
        double xi=poly[i].x(),yi=poly[i].y();
        double xj=poly[j].x(),yj=poly[j].y();
        if(((yi>y)!=(yj>y))&&(x<(xj-xi)*(y-yi)/(yj-yi)+xi))
            inside=!inside;
    }
    return inside;
}

// ───────────────────────────────────────────────────────────────
//  CadDocument — implémentation
// ───────────────────────────────────────────────────────────────
const CadLayer* CadDocument::layerByName(const QString& name) const
{
    for(const CadLayer&l:layers) if(l.name==name) return &l;
    return nullptr;
}

QVector<const CadContour*> CadDocument::contoursInLayer(const QString& ln) const
{
    QVector<const CadContour*> r;
    for(const CadContour&c:contours) if(c.layer==ln) r<<&c;
    return r;
}

QVector<const CadContour*> CadDocument::closedContours() const
{
    QVector<const CadContour*> r;
    for(const CadContour&c:contours) if(c.closed) r<<&c;
    return r;
}

QVector<int> CadDocument::islandsOf(int parentId) const
{
    const CadContour* parent=nullptr;
    for(const CadContour&c:contours) if(c.id==parentId){parent=&c;break;}
    if(!parent||!parent->closed) return {};
    QVector<int> islands;
    for(const CadContour&c:contours){
        if(c.id==parentId||!c.closed) continue;
        // Un contour est un îlot si son centroïde est dans le parent
        QRectF b=c.bbox();
        QPointF ctr=b.center();
        if(parent->contains(ctr)) islands<<c.id;
    }
    return islands;
}

// ───────────────────────────────────────────────────────────────
//  ContourCadData — JSON
// ───────────────────────────────────────────────────────────────
QJsonObject ContourCadData::toJson() const
{
    QJsonObject o;
    o["filePath"]     = filePath;
    o["modeUsinage"]  = (int)modeUsinage;
    o["toolRef"]      = toolRef;
    o["toolByName"]   = toolByName;
    o["spindleVal"]   = spindleVal;
    o["spindleByVc"]  = spindleByVc;
    o["descenteVal"]  = descenteVal;
    o["descenteMode"] = descenteMode;
    o["avanceVal"]    = avanceVal;
    o["avanceMode"]   = avanceMode;
    o["avanceFinVal"] = avanceFinVal;
    o["avanceFinMode"]= avanceFinMode;
    o["brocheCW"]     = brocheCW;
    o["fonctionM1"]   = fonctionM1;
    o["fonctionM2"]   = fonctionM2;
    o["DL"]           = DL;
    o["profondeur"]   = profondeur;
    o["profPasse"]    = profPasse;
    o["surepLat"]     = surepLat;
    o["surepProf"]    = surepProf;
    o["passeFinition"]= passeFinition;
    o["modeFraisage"] = modeFraisage;
    o["origineX"]     = origineX;
    o["origineY"]     = origineY;
    o["origineZ"]     = origineZ;
    o["approche"]     = approche;
    o["sautBride"]    = sautBride;
    QJsonArray lArr; for(const QString&s:selectedLayers) lArr<<s;
    o["selectedLayers"]=lArr;
    QJsonArray iArr; for(int id:selectedContourIds) iArr<<id;
    o["selectedContourIds"]=iArr;
    return o;
}

ContourCadData ContourCadData::fromJson(const QJsonObject& o)
{
    ContourCadData d;
    d.filePath      = o["filePath"].toString();
    d.modeUsinage   = (ModeUsinage)o["modeUsinage"].toInt(0);
    d.toolRef       = o["toolRef"].toString();
    d.toolByName    = o["toolByName"].toBool();
    d.spindleVal    = o["spindleVal"].toDouble();
    d.spindleByVc   = o["spindleByVc"].toBool();
    d.descenteVal   = o["descenteVal"].toDouble();
    d.descenteMode  = o["descenteMode"].toInt();
    d.avanceVal     = o["avanceVal"].toDouble();
    d.avanceMode    = o["avanceMode"].toInt();
    d.avanceFinVal  = o["avanceFinVal"].toDouble();
    d.avanceFinMode = o["avanceFinMode"].toInt();
    d.brocheCW      = o["brocheCW"].toBool(true);
    d.fonctionM1    = o["fonctionM1"].toString();
    d.fonctionM2    = o["fonctionM2"].toString();
    d.DL            = o["DL"].toDouble();
    d.profondeur    = o["profondeur"].toDouble(10.0);
    d.profPasse     = o["profPasse"].toDouble(2.0);
    d.surepLat      = o["surepLat"].toDouble();
    d.surepProf     = o["surepProf"].toDouble();
    d.passeFinition = o["passeFinition"].toDouble();
    d.modeFraisage  = o["modeFraisage"].toInt();
    d.origineX      = o["origineX"].toDouble();
    d.origineY      = o["origineY"].toDouble();
    d.origineZ      = o["origineZ"].toDouble();
    d.approche      = o["approche"].toDouble(2.0);
    d.sautBride     = o["sautBride"].toDouble(50.0);
    for(const auto&v:o["selectedLayers"].toArray())   d.selectedLayers<<v.toString();
    for(const auto&v:o["selectedContourIds"].toArray()) d.selectedContourIds<<v.toInt();
    return d;
}

// ═══════════════════════════════════════════════════════════════
//  CadImporter — chargement unifié
// ═══════════════════════════════════════════════════════════════
QString CadImporter::fileFilter()
{
    return "Fichiers CAD (*.dxf *.DXF *.stp *.STP *.step *.STEP *.igs *.IGS *.iges *.IGES);;"
           "DXF (*.dxf *.DXF);;"
           "STEP (*.stp *.STP *.step *.STEP);;"
           "IGES (*.igs *.IGS *.iges *.IGES);;"
           "Tous (*)";
}

CadDocument CadImporter::load(const QString& filePath)
{
    QString ext=QFileInfo(filePath).suffix().toLower();
    if(ext=="dxf")  return loadDxf(filePath);
    if(ext=="step"||ext=="stp") return loadStep(filePath);
    if(ext=="iges"||ext=="igs") return loadIges(filePath);
    CadDocument d; d.error="Format non reconnu : "+ext; return d;
}

// ═══════════════════════════════════════════════════════════════
//  PARSER DXF
// ═══════════════════════════════════════════════════════════════
namespace {
// Lire une paire (code, valeur) depuis le fichier DXF
bool readPair(QTextStream& ts, int& code, QString& value)
{
    if(ts.atEnd()) return false;
    QString codeLine=ts.readLine().trimmed();
    if(ts.atEnd()) return false;
    QString valueLine=ts.readLine(); // PAS trimmed — les noms peuvent avoir des espaces
    bool ok=false;
    code=codeLine.toInt(&ok);
    value=valueLine.trimmed();
    return ok;
}
} // namespace

QVector<CadImporter::DxfEntity> CadImporter::parseDxfEntities(
    const QString& filePath, QVector<CadLayer>& layersOut)
{
    QFile f(filePath);
    if(!f.open(QIODevice::ReadOnly|QIODevice::Text)) return {};
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);

    QVector<DxfEntity> entities;
    layersOut.clear();

    // Ajouter calque "0" par défaut
    CadLayer layer0; layer0.name="0"; layer0.color=Qt::white;
    layersOut<<layer0;

    int code=0; QString value;
    bool inEntities=false, inBlocks=false, inTables=false;
    bool inModelSpace=false;
    DxfEntity cur;
    bool parsingEntity=false;
    DxfEntity::Vertex curVertex;
    bool inVertex=false;

    auto commitEntity=[&](){
        if(parsingEntity && !cur.type.isEmpty()){
            if(inVertex){
                cur.vertices<<curVertex;
                curVertex=DxfEntity::Vertex{};
                inVertex=false;
            }
            // Convertir CIRCLE en ARC fermé
            if(cur.type=="CIRCLE"){ cur.angle1=0; cur.angle2=360; }
            // Filtrer les entités sans géométrie utile
            if(cur.type=="LINE"||cur.type=="ARC"||cur.type=="CIRCLE"||
                cur.type=="LWPOLYLINE"||cur.type=="POLYLINE"||cur.type=="SPLINE"){
                entities<<cur;
            }
        }
        cur=DxfEntity{};
        parsingEntity=false;
    };

    while(!ts.atEnd()){
        if(!readPair(ts,code,value)) break;

        // ── Navigation sections ─────────────────────────────
        if(code==0){
            if(value=="SECTION"){}
            else if(value=="ENDSEC"){ inEntities=false; inBlocks=false; inTables=false; inModelSpace=false; }
            else if(value=="EOF") break;
        }
        if(code==2){
            if(value=="ENTITIES") inEntities=true;
            if(value=="BLOCKS")   inBlocks=true;
            if(value=="TABLES")   inTables=true;
            if(value=="*Model_Space"||value=="*MODEL_SPACE") inModelSpace=true;
        }

        // ── Lecture calques (TABLES section) ───────────────
        if(inTables && code==0 && value=="LAYER"){
            CadLayer nl; parsingEntity=false;
            // Lire les attributs du calque
            while(!ts.atEnd()){
                int lc; QString lv;
                if(!readPair(ts,lc,lv)) break;
                if(lc==0){ // fin de cet enregistrement LAYER
                    bool found=false;
                    for(auto&el:layersOut) if(el.name==nl.name){found=true;break;}
                    if(!found && !nl.name.isEmpty()) layersOut<<nl;
                    // Remettre ce token pour l'itération principale
                    // (on ne peut pas "pushback" un token facilement, on traite ici)
                    if(lv=="LAYER"){
                        nl=CadLayer{};
                    } else if(lv=="ENDTAB"||lv=="ENDSEC"){
                        inTables=false; break;
                    } else {
                        break;
                    }
                }
                else if(lc==2) nl.name=lv;
                else if(lc==62) nl.color=CadLayer::colorFromIndex(qAbs(lv.toInt()));
                else if(lc==70){ int fl=lv.toInt(); nl.frozen=(fl&1); nl.visible=!(fl&4); }
            }
        }

        // ── Lecture entités ────────────────────────────────
        bool inScope=(inEntities)||(inBlocks&&inModelSpace);
        if(!inScope) continue;

        if(code==0){
            commitEntity();
            // Nouveaux types d'entité
            static const QStringList supported={"LINE","ARC","CIRCLE","LWPOLYLINE","POLYLINE","SPLINE"};
            if(supported.contains(value)){
                cur.type=value;
                parsingEntity=true;
            }
            // Sommet POLYLINE
            else if(value=="VERTEX"&&parsingEntity&&cur.type=="POLYLINE"){
                if(inVertex) cur.vertices<<curVertex;
                curVertex=DxfEntity::Vertex{};
                inVertex=true;
            }
            else if(value=="SEQEND"&&inVertex){
                cur.vertices<<curVertex;
                inVertex=false;
            }
        }
        else if(parsingEntity){
            // Attributs communs
            if(code==8) { cur.layer=value; }
            else if(code==62) cur.color=value.toInt();
            // Coordonnées
            // code 10/20/30 : comportement selon le type d'entité
            else if(code==10){
                if(inVertex)               curVertex.x=value.toDouble();
                else if(cur.type=="LWPOLYLINE"){ DxfEntity::Vertex v; v.x=value.toDouble(); cur.vertices<<v; }
                else if(cur.type=="SPLINE") cur.splineCtrl<<QPointF(value.toDouble(),0.0);
                else                        cur.x1=value.toDouble();
            }
            else if(code==20){
                if(inVertex)               curVertex.y=value.toDouble();
                else if(cur.type=="LWPOLYLINE"){ if(!cur.vertices.isEmpty()) cur.vertices.last().y=value.toDouble(); }
                else if(cur.type=="SPLINE"){ if(!cur.splineCtrl.isEmpty()) cur.splineCtrl.last().setY(value.toDouble()); }
                else                        cur.y1=value.toDouble();
            }
            else if(code==30){ if(inVertex) curVertex.z=value.toDouble(); else cur.z1=value.toDouble(); }
            else if(code==11) cur.x2=value.toDouble();
            else if(code==21) cur.y2=value.toDouble();
            else if(code==31) cur.z2=value.toDouble();
            // Arc/cercle
            else if(code==40) cur.radius=value.toDouble();
            else if(code==50) cur.angle1=value.toDouble();
            else if(code==51) cur.angle2=value.toDouble();
            else if(code==70) cur.flags=value.toInt();
            else if(code==42 && cur.type=="LWPOLYLINE"){
                if(!cur.vertices.isEmpty()) cur.vertices.last().bulge=value.toDouble();
            }
            // SPLINE : points de contrôle accumulés dans splineCtrl (voir code==10 ci-dessus)
        }
    }
    commitEntity();
    return entities;
}

// ── Chaînage des entités en contours connectés ──────────────────
QVector<CadContour> CadImporter::chainEntities(
    const QVector<DxfEntity>& entities,
    const QVector<CadLayer>& layers,
    double tolerance)
{
    QVector<CadContour> result;
    int nextId=1;

    // 1. Convertir les entités autonomes (CIRCLE, LWPOLYLINE, POLYLINE fermée)
    //    directement en contours
    QVector<int> unchained; // indices des entités LINE/ARC à chaîner

    for(int i=0;i<entities.size();++i){
        const DxfEntity& e=entities[i];

        if(e.type=="CIRCLE"){
            CadContour c=entityToContour(e,nextId++,layers);
            c.closed=true;
            result<<c;
            continue;
        }
        if(e.type=="LWPOLYLINE"||e.type=="POLYLINE"){
            CadContour c=entityToContour(e,nextId++,layers);
            result<<c;
            continue;
        }
        if(e.type=="SPLINE"){
            CadContour c=entityToContour(e,nextId++,layers);
            result<<c;
            continue;
        }
        // LINE et ARC → chaînage
        unchained<<i;
    }

    // 2. Chaîner LINE/ARC par calque
    // Grouper par calque
    QMap<QString, QVector<int>> byLayer;
    for(int idx:unchained) byLayer[entities[idx].layer]<<idx;

    for(auto it=byLayer.begin();it!=byLayer.end();++it){
        QVector<int> pool=it.value();
        QVector<bool> used(pool.size(),false);

        while(true){
            // Trouver le premier non-utilisé
            int startIdx=-1;
            for(int i=0;i<pool.size();++i) if(!used[i]){startIdx=i;break;}
            if(startIdx<0) break;

            // Construire un contour en chaînant
            CadContour c;
            c.id=nextId++;
            c.layer=it.key();
            // Couleur depuis calque
            for(const CadLayer&l:layers) if(l.name==c.layer){c.color=l.color;break;}

            used[startIdx]=true;
            const DxfEntity& first=entities[pool[startIdx]];
            c.segments<<[&]()->CadSegment{
                CadSegment s;
                if(first.type=="LINE"){
                    s.type=CadSegment::Line;
                    s.x1=first.x1;s.y1=first.y1;s.x2=first.x2;s.y2=first.y2;
                } else { // ARC
                    s.type=CadSegment::Arc;
                    s.x1=first.x1;s.y1=first.y1;
                    s.radius=first.radius;
                    s.angle1=first.angle1;s.angle2=first.angle2;
                    s.ccw=true;
                }
                return s;
            }();

            // Chercher continuellement le segment suivant
            bool extended=true;
            while(extended){
                extended=false;
                QPointF ep=c.segments.last().endPt();
                for(int i=0;i<pool.size();++i){
                    if(used[i]) continue;
                    const DxfEntity& e=entities[pool[i]];
                    CadSegment s;
                    if(e.type=="LINE"){
                        s.type=CadSegment::Line;
                        s.x1=e.x1;s.y1=e.y1;s.x2=e.x2;s.y2=e.y2;
                    } else {
                        s.type=CadSegment::Arc;
                        s.x1=e.x1;s.y1=e.y1;
                        s.radius=e.radius;
                        s.angle1=e.angle1;s.angle2=e.angle2;
                        s.ccw=true;
                    }
                    QPointF sp=s.startPt(), ep2=s.endPt();
                    double d1=(ep-sp).manhattanLength();
                    double d2=(ep-ep2).manhattanLength();
                    if(d1<tolerance){
                        c.segments<<s;
                        used[i]=true; extended=true; break;
                    } else if(d2<tolerance){
                        // Inverser le segment
                        if(s.type==CadSegment::Line){
                            std::swap(s.x1,s.x2); std::swap(s.y1,s.y2);
                        } else {
                            std::swap(s.angle1,s.angle2);
                            s.ccw=!s.ccw;
                        }
                        c.segments<<s;
                        used[i]=true; extended=true; break;
                    }
                }
            }

            // Vérifier fermeture
            if(!c.segments.isEmpty()){
                QPointF sp=c.segments.first().startPt();
                QPointF ep=c.segments.last().endPt();
                c.closed=(ep-sp).manhattanLength()<tolerance;
            }

            if(!c.segments.isEmpty()) result<<c;
        }
    }

    return result;
}

CadContour CadImporter::entityToContour(
    const DxfEntity& e, int id, const QVector<CadLayer>& layers)
{
    CadContour c;
    c.id=id;
    c.layer=e.layer;
    for(const CadLayer&l:layers) if(l.name==e.layer){c.color=l.color;break;}

    if(e.type=="CIRCLE"){
        CadSegment s;
        s.type=CadSegment::Circle;
        s.x1=e.x1; s.y1=e.y1;
        s.radius=e.radius;
        s.angle1=0; s.angle2=360; s.ccw=true;
        c.segments<<s;
        c.closed=true;
        return c;
    }

    if(e.type=="LWPOLYLINE"){
        bool closed=(e.flags&1);
        int n=e.vertices.size();
        for(int i=0;i<n;++i){
            int j=(i+1)%n;
            if(j==0&&!closed) break;
            const auto& v=e.vertices[i];
            const auto& vn=e.vertices[j];
            double bulge=v.bulge;
            if(qAbs(bulge)<1e-10){
                // Segment droit
                CadSegment s;
                s.type=CadSegment::Line;
                s.x1=v.x; s.y1=v.y; s.x2=vn.x; s.y2=vn.y;
                c.segments<<s;
            } else {
                // Arc depuis le bulge
                // bulge = tan(angle_subtended/4)
                double d=std::sqrt((vn.x-v.x)*(vn.x-v.x)+(vn.y-v.y)*(vn.y-v.y));
                double r=d*(bulge*bulge+1.0)/(4.0*qAbs(bulge));
                double mx=(v.x+vn.x)/2.0, my=(v.y+vn.y)/2.0;
                double dx=vn.x-v.x, dy=vn.y-v.y;
                double nx=-dy, ny=dx; // perpendiculaire
                double nl=std::sqrt(nx*nx+ny*ny);
                if(nl>1e-10){ nx/=nl; ny/=nl; }
                double sagitta=(r-std::sqrt(r*r-d*d/4.0));
                if(bulge<0) sagitta=-sagitta;
                double cx=mx+nx*(r-sagitta)/r; // approx centre
                double cy=my+ny*(r-sagitta)/r; // approx
                // Recalcul exact du centre
                double t=(1.0/bulge - bulge)/2.0;
                cx=mx - t*dy/2.0*(d/d);
                cy=my + t*dx/2.0*(d/d);
                // angles
                double a1=qRadiansToDegrees(std::atan2(v.y-cy,v.x-cx));
                double a2=qRadiansToDegrees(std::atan2(vn.y-cy,vn.x-cx));
                CadSegment s;
                s.type=CadSegment::Arc;
                s.x1=cx; s.y1=cy;
                s.radius=r;
                s.angle1=a1; s.angle2=a2;
                s.ccw=(bulge>0);
                c.segments<<s;
            }
        }
        c.closed=closed;
        return c;
    }

    if(e.type=="POLYLINE"){
        int n=e.vertices.size();
        bool closed=(e.flags&1);
        for(int i=0;i<n;++i){
            int j=(i+1)%n;
            if(j==0&&!closed) break;
            CadSegment s;
            s.type=CadSegment::Line;
            s.x1=e.vertices[i].x; s.y1=e.vertices[i].y;
            s.x2=e.vertices[j].x; s.y2=e.vertices[j].y;
            c.segments<<s;
        }
        c.closed=closed;
        return c;
    }

    if(e.type=="SPLINE"){
        // Approximation B-spline cubique par segments de Bézier
        const QVector<QPointF>& ctrl=e.splineCtrl;
        if(ctrl.size()<2){
            if(!ctrl.isEmpty()){
                CadSegment s; s.type=CadSegment::Line;
                s.x1=ctrl.first().x(); s.y1=ctrl.first().y();
                s.x2=ctrl.last().x();  s.y2=ctrl.last().y();
                c.segments<<s;
            }
            return c;
        }
        // Simple: traiter comme polyligne sur les points de contrôle
        for(int i=0;i<ctrl.size()-1;++i){
            CadSegment s; s.type=CadSegment::Line;
            s.x1=ctrl[i].x(); s.y1=ctrl[i].y();
            s.x2=ctrl[i+1].x(); s.y2=ctrl[i+1].y();
            c.segments<<s;
        }
        c.closed=((ctrl.first()-ctrl.last()).manhattanLength()<0.01);
        return c;
    }

    return c;
}

QRectF CadImporter::computeExtents(const QVector<CadContour>& contours)
{
    if(contours.isEmpty()) return {};
    double xMin=1e18,xMax=-1e18,yMin=1e18,yMax=-1e18;
    for(const CadContour&c:contours){
        QRectF b=c.bbox();
        if(b.isEmpty()) continue;
        xMin=qMin(xMin,b.left());   xMax=qMax(xMax,b.right());
        yMin=qMin(yMin,b.top());    yMax=qMax(yMax,b.bottom());
    }
    if(xMin>xMax) return {};
    return QRectF(xMin,yMin,xMax-xMin,yMax-yMin);
}

// ── loadDxf ─────────────────────────────────────────────────────
CadDocument CadImporter::loadDxf(const QString& filePath)
{
    CadDocument doc;
    doc.filePath=filePath;
    doc.format="DXF";

    if(!QFile::exists(filePath)){
        doc.error="Fichier introuvable : "+filePath;
        return doc;
    }

    QVector<CadLayer> layers;
    QVector<DxfEntity> entities=parseDxfEntities(filePath,layers);

    // Ajouter calques manquants référencés par les entités
    for(const DxfEntity&e:entities){
        bool found=false;
        for(const CadLayer&l:layers) if(l.name==e.layer){found=true;break;}
        if(!found){
            CadLayer nl; nl.name=e.layer;
            nl.color=CadLayer::colorFromIndex(e.color!=256?e.color:7);
            layers<<nl;
        }
    }

    doc.layers=layers;
    doc.contours=chainEntities(entities,layers);
    doc.extents=computeExtents(doc.contours);
    doc.valid=!doc.contours.isEmpty();

    if(doc.contours.isEmpty()&&!entities.isEmpty())
        doc.error="Aucun contour extrait ("+QString::number(entities.size())+" entités brutes)";

    qDebug()<<"[CAD] DXF chargé:"<<filePath
             <<"→"<<entities.size()<<"entités →"<<doc.contours.size()<<"contours";
    return doc;
}

// ═══════════════════════════════════════════════════════════════
//  STEP / IGES via OCC
// ═══════════════════════════════════════════════════════════════
#ifdef WITH_OCC
// Convertit un edge OCC en CadSegment(s)
static QVector<CadSegment> edgeToSegments(const TopoDS_Edge& edge, double tol=0.01)
{
    QVector<CadSegment> segs;
    BRepAdaptor_Curve curve(edge);
    double first=curve.FirstParameter(), last=curve.LastParameter();

    switch(curve.GetType()){
    case GeomAbs_Line: {
        gp_Pnt p1=curve.Value(first), p2=curve.Value(last);
        CadSegment s;
        s.type=CadSegment::Line;
        s.x1=p1.X(); s.y1=p1.Y();
        s.x2=p2.X(); s.y2=p2.Y();
        segs<<s;
        break;
    }
    case GeomAbs_Circle: {
        gp_Circ circ=curve.Circle();
        gp_Pnt ctr=circ.Location();
        double r=circ.Radius();
        CadSegment s;
        s.type=CadSegment::Arc;
        s.x1=ctr.X(); s.y1=ctr.Y();
        s.radius=r;
        // angles depuis les paramètres
        gp_Pnt p1=curve.Value(first), p2=curve.Value(last);
        s.angle1=qRadiansToDegrees(std::atan2(p1.Y()-ctr.Y(),p1.X()-ctr.X()));
        s.angle2=qRadiansToDegrees(std::atan2(p2.Y()-ctr.Y(),p2.X()-ctr.X()));
        s.ccw=(last>first);
        segs<<s;
        break;
    }
    default: {
        // Courbes complexes (ellipse, spline, etc.) → approximation par segments
        int n=qMax(4,int((last-first)/tol));
        gp_Pnt prev=curve.Value(first);
        for(int i=1;i<=n;++i){
            double t=first+i*(last-first)/n;
            gp_Pnt cur=curve.Value(t);
            CadSegment s;
            s.type=CadSegment::Line;
            s.x1=prev.X(); s.y1=prev.Y();
            s.x2=cur.X();  s.y2=cur.Y();
            if(s.length()>1e-6) segs<<s;
            prev=cur;
        }
        break;
    }
    }
    return segs;
}

CadDocument CadImporter::occLoad(const QString& filePath, const QString& format)
{
    CadDocument doc;
    doc.filePath=filePath;
    doc.format=format;

    TopoDS_Shape shape;
    if(format=="STEP"){
        STEPControl_Reader reader;
        if(reader.ReadFile(filePath.toLocal8Bit().constData())!=IFSelect_RetDone){
            doc.error="Erreur lecture STEP : "+filePath; return doc;
        }
        reader.TransferRoots();
        shape=reader.OneShape();
    } else {
        IGESControl_Reader reader;
        if(reader.ReadFile(filePath.toLocal8Bit().constData())!=IFSelect_RetDone){
            doc.error="Erreur lecture IGES : "+filePath; return doc;
        }
        reader.TransferRoots();
        shape=reader.OneShape();
    }

    if(shape.IsNull()){ doc.error="Shape nulle après lecture"; return doc; }

    // Ajouter calque par défaut
    CadLayer defLayer; defLayer.name="0"; defLayer.color=Qt::cyan;
    doc.layers<<defLayer;

    // Extraire toutes les edges
    int id=1;
    TopExp_Explorer expl(shape, TopAbs_EDGE);
    // Grouper les edges en wires (contours)
    // Pour simplifier : chaque wire = un contour
    TopExp_Explorer wireExp(shape, TopAbs_WIRE);
    while(wireExp.More()){
        TopoDS_Wire wire=TopoDS::Wire(wireExp.Current());
        CadContour c;
        c.id=id++;
        c.layer="0";
        c.color=Qt::cyan;

        TopExp_Explorer edgeExp(wire, TopAbs_EDGE);
        while(edgeExp.More()){
            auto segs=edgeToSegments(TopoDS::Edge(edgeExp.Current()));
            c.segments.append(segs);
            edgeExp.Next();
        }
        if(!c.segments.isEmpty()){
            // Vérifier fermeture
            QPointF sp=c.segments.first().startPt();
            QPointF ep=c.segments.last().endPt();
            c.closed=(ep-sp).manhattanLength()<0.1;
            doc.contours<<c;
        }
        wireExp.Next();
    }

    // Si pas de wires, essayer edges individuelles
    if(doc.contours.isEmpty()){
        while(expl.More()){
            auto segs=edgeToSegments(TopoDS::Edge(expl.Current()));
            if(!segs.isEmpty()){
                CadContour c;
                c.id=id++;
                c.layer="0";
                c.color=Qt::cyan;
                c.segments=segs;
                doc.contours<<c;
            }
            expl.Next();
        }
    }

    doc.extents=computeExtents(doc.contours);
    doc.valid=!doc.contours.isEmpty();
    qDebug()<<"[CAD]"<<format<<"chargé:"<<filePath<<"→"<<doc.contours.size()<<"contours";
    return doc;
}
#else
CadDocument CadImporter::occLoad(const QString& filePath, const QString& format)
{
    CadDocument doc;
    doc.error=format+" non supporté : compilez avec CONFIG+=WITH_OCC";
    return doc;
}
#endif

CadDocument CadImporter::loadStep(const QString& filePath)
{
    return occLoad(filePath,"STEP");
}

CadDocument CadImporter::loadIges(const QString& filePath)
{
    return occLoad(filePath,"IGES");
}
