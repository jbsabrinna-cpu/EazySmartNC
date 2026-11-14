// ═══════════════════════════════════════════════════════════════
//  Sim3DWidget v4
//  – GCodeParserFreeCADCompatible (classe locale)
//  – OpenCASCADE : STEP, buildToolShape, applyToolCut, mesh GPU
//  – Heightmap fallback si pas de STEP
//  – tessArcPlane G17/G18/G19
// ═══════════════════════════════════════════════════════════════
// ── OCC includes — confinés ici, n'entrent PAS dans le .h ──────
#ifdef WITH_OCC
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <TopoDS_Shape.hxx>
#  include <TopoDS_Face.hxx>
#  include <TopoDS.hxx>
#  include <TopLoc_Location.hxx>
#  include <TopExp_Explorer.hxx>
#  include <TopAbs_ShapeEnum.hxx>
#  include <BRep_Tool.hxx>
#  include <Poly_Triangulation.hxx>
#  include <BRepMesh_IncrementalMesh.hxx>
#  include <STEPControl_Reader.hxx>
#  include <IFSelect_ReturnStatus.hxx>
#  include <BRepPrimAPI_MakeCylinder.hxx>
#  include <BRepPrimAPI_MakeCone.hxx>
#  include <BRepPrimAPI_MakeSphere.hxx>
#  include <BRepPrimAPI_MakeBox.hxx>
#  include <BRepAlgoAPI_Cut.hxx>
#  include <BRepAlgoAPI_Fuse.hxx>
#  include <gp_Ax2.hxx>
#  include <gp_Pnt.hxx>
#  include <gp_Dir.hxx>
#  include <gp_Vec.hxx>
#  include <gp_Trsf.hxx>
#endif // WITH_OCC

#include "sim3dwidget.h"
#include <climits>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QWheelEvent>
#include <QRegularExpression>
#include <QThreadPool>
#include <QDebug>
#include <QPainter>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QStyle>
#include <cmath>
#include <algorithm>
#include <array>
#ifdef Q_OS_WIN
#  include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glu.h>
#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <QVector4D>

// Constantes OpenGL 3 absentes du gl.h Windows (OpenGL 1.1)
#ifndef GL_R32F
#  define GL_R32F 0x822E
#endif
#ifndef GL_CLAMP_TO_EDGE
#  define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#  define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_R32I
#  define GL_R32I 0x8235
#endif
#ifndef GL_RED_INTEGER
#  define GL_RED_INTEGER 0x8D94
#endif

// ── OccData — pImpl ───────────────────────────────────────────
struct OccData {
#ifdef WITH_OCC
    TopoDS_Shape shape;
    TopoDS_Shape shapeOriginal;
#endif
};
#ifdef WITH_OCC
#  define OCC_SHAPE          (m_occ->shape)
#  define OCC_SHAPE_ORIGINAL (m_occ->shapeOriginal)
#else
#  define OCC_SHAPE_ORIGINAL (*(int*)nullptr)
#  define OCC_SHAPE          (*(int*)nullptr)
#endif

// ═══════════════════════════════════════════════════════════════
//  GCodeParserFreeCADCompatible
//  Classe locale (visible dans ce .cpp uniquement).
//  Inspirée du moteur FreeCAD Path (PathScripts, PathGeom.py).
//
//  Supporte :
//    G0/G1/G2/G3  – mouvements + arcs (I,J,K ou R)
//    G4           – temporisation (ignorée)
//    G17/18/19    – plan de travail pour arcs 3D
//    G20/G21      – pouces / millimètres
//    G28/G30      – retour machine (position 0,0,0)
//    G54-G59      – sélection WCS (offsets = 0 dans EazyNC)
//    G80/G81/G83  – fin cycle / perçage simple / débourrage
//    G90/G91      – absolu / incrémental
//    G93/G94      – avance inverse-temps / mm·min⁻¹
//    F/T          – avance et outil courant
//    G40/G41/G42  – compensation rayon CRC
// ═══════════════════════════════════════════════════════════════
class GCodeParserFreeCADCompatible
{
public:
    // ── ParsedMove ────────────────────────────────────────────
    struct ParsedMove {
        enum Kind { Rapid, Cut, ArcCW, ArcCCW };
        Kind   kind   = Cut;
        double x=0,y=0,z=0;      // destination absolue (mm)
        double i=0,j=0,k=0;      // offsets centre arc (relatifs)
        double r=0;               // rayon arc signé
        bool   hasR   = false;
        double f      = 0;        // avance effective (mm/min)
        int    toolT  = 0;
        int    lineIdx= -1;
        int    plane  = 17;       // 17=XY 18=ZX 19=YZ
        int    compMode   = 0;    // 0=off 41=G41 42=G42
        double compR  = 0;        // rayon CRC actif (mm)
        bool   isTransition = false;
        PassColor passColor = PassColor::Normal;
    };

    void parse(const QVector<GCodeLine> &lines, double rDefault)
    {
        m_rDefault = rDefault;
        m_moves.clear();
        prescanPassColors(lines);
        reset();

        static const QRegularExpression
            reG00(R"(\bG0*0\b)"), reG01(R"(\bG0*1\b)"),
            reG02(R"(\bG0*2\b)"), reG03(R"(\bG0*3\b)"),
            reG04(R"(\bG0*4\b)"),
            reG17(R"(\bG17\b)"),  reG18(R"(\bG18\b)"),  reG19(R"(\bG19\b)"),
            reG20(R"(\bG20\b)"),  reG21(R"(\bG21\b)"),
            reG28(R"(\bG28\b)"),  reG30(R"(\bG30\b)"),
            reG54(R"(\bG5[4-9]\b)"),
            reG80(R"(\bG80\b)"),  reG81(R"(\bG81\b)"),  reG83(R"(\bG83\b)"),
            reG90(R"(\bG90\b)"),  reG91(R"(\bG91\b)"),
            reG93(R"(\bG93\b)"),  reG94(R"(\bG94\b)"),
            reG40(R"(\bG40\b)"),  reG41(R"(\bG41\b)"),  reG42(R"(\bG42\b)"),
            reG68(R"(\bG68\b)"),  reG69(R"(\bG69\b)"),
            reDval(R"(D\s*=\s*Routil\s*=\s*([\d.]+))",
                   QRegularExpression::CaseInsensitiveOption),
            reDiam(R"(D\s*=\s*([\d.]+)\s*mm)",
                   QRegularExpression::CaseInsensitiveOption),
            reDnum(R"(\bD(\d+)\b)"),
            reTool(R"(\bT\d)"),
            reCfgOnly(R"(\bG(?:2[01]|1[7-9]|8[0-9])\b)");

        for (int li = 0; li < lines.size(); ++li) {
            const GCodeLine &gl = lines[li];

            // Commentaires : extraire D=Routil
            if (gl.type == GCodeLine::Comment || gl.type == GCodeLine::Blank) {
                QString raw = gl.code + ' ' + gl.comment;
                auto m = reDval.match(raw);
                if (m.hasMatch()) m_lastCommentR = m.captured(1).toDouble();
                else { auto m2=reDiam.match(raw); if(m2.hasMatch()) m_lastCommentR=m2.captured(1).toDouble()/2.0; }
                continue;
            }
            if (gl.type==GCodeLine::Spindle||gl.type==GCodeLine::MFunction) continue;

            const QString &code = gl.code;
            QString up = code.toUpper().simplified();

            // Outil
            if (up.contains(reTool)) { double tv; if(parseParam(code,'T',tv)) m_T=int(tv); }
            // Plans
            if (up.contains(reG17)) m_plane=17;
            if (up.contains(reG18)) m_plane=18;
            if (up.contains(reG19)) m_plane=19;
            // Unités
            if (up.contains(reG20)) m_inch=true;
            if (up.contains(reG21)) m_inch=false;
            // Mode avance
            if (up.contains(reG93)) m_g93=true;
            if (up.contains(reG94)) m_g93=false;
            // Absolu/incrémental
            if (up.contains(reG90)) m_g91=false;
            if (up.contains(reG91)) m_g91=true;
            // Rotation G68/G69
            if (up.contains(reG68)){ double rv=0; parseParam(code,'R',rv); m_rotAngle=rv*M_PI/180.0; }
            if (up.contains(reG69)){ m_rotAngle=0; }
            // WCS (offsets = 0 dans EazyNC)
            if (up.contains(reG54)) { /* offsets nuls */ }
            // G80 fin cycle
            if (up.contains(reG80)) m_inCycle=false;
            // CRC
            bool isTrans=false;
            if (up.contains(reG40)) { m_comp=0;  isTrans=true; }
            if (up.contains(reG41)) { m_comp=41; isTrans=true; }
            if (up.contains(reG42)) { m_comp=42; isTrans=true; }
            if (m_comp!=0) resolveCompR(gl,up,reDval,reDiam,reDnum);
            // Avance
            double fv; if(parseParam(code,'F',fv)) m_f=fv;

            // Config pure sans mouvement → skip
            if (up.contains(reCfgOnly)
                && !up.contains(QRegularExpression(R"(\bG0*[0-4]\b)"))
                && !up.contains(QChar('X')) && !up.contains(QChar('Y')) && !up.contains(QChar('Z')))
                continue;
            if (up.contains(reG04)) continue; // dwell
            if (up.contains(reG28)||up.contains(reG30)) { m_x=m_y=m_z=0; continue; }

            // Destination
            double px,py,pz;
            bool hx=false,hy=false,hz=false;
            if(parseParam(code,'X',px)){ m_nx=m_g91?m_x+au(px):au(px); hx=true; } else m_nx=m_x;
            if(parseParam(code,'Y',py)){ m_ny=m_g91?m_y+au(py):au(py); hy=true; } else m_ny=m_y;
            if(parseParam(code,'Z',pz)){ m_nz=m_g91?m_z+au(pz):au(pz); hz=true; } else m_nz=m_z;
            // Appliquer rotation G68 autour de l'origine
            if(m_rotAngle!=0.0 && (hx||hy)){
                double cosA=cos(m_rotAngle), sinA=sin(m_rotAngle);
                double rx=m_nx*cosA - m_ny*sinA;
                double ry=m_nx*sinA + m_ny*cosA;
                m_nx=rx; m_ny=ry;
            }

            // Cycles perçage G81/G83
            if (up.contains(reG81)||up.contains(reG83)) {
                m_inCycle=true; m_cycleIsPeck=up.contains(reG83);
                double rl,zc,qv=2.0;
                parseParam(code,'R',rl); m_cycleR=au(rl);
                parseParam(code,'Z',zc); m_cycleZ=au(zc);
                parseParam(code,'Q',qv); m_cycleQ=au(qv);
                if(hx||hy) addCycle(m_nx,m_ny,li);
                m_x=m_nx; m_y=m_ny; continue;
            }
            if(m_inCycle&&(hx||hy)){ addCycle(m_nx,m_ny,li); m_x=m_nx; m_y=m_ny; continue; }
            if(!hx&&!hy&&!hz) continue;

            // Type de mouvement
            bool isG00=up.contains(reG00), isG01=up.contains(reG01);
            bool isG02=up.contains(reG02), isG03=up.contains(reG03);
            if(isG00) m_modalG1=false;
            if(isG01) m_modalG1=true;
            bool isArc  =(isG02||isG03);
            bool isRapid=isG00||(gl.type==GCodeLine::Rapid&&!isArc&&!isG01&&!m_modalG1);

            ParsedMove mv;
            mv.kind      = isRapid?ParsedMove::Rapid:isG02?ParsedMove::ArcCW:isG03?ParsedMove::ArcCCW:ParsedMove::Cut;
            mv.x=m_nx; mv.y=m_ny; mv.z=m_nz;
            mv.toolT=m_T; mv.lineIdx=li; mv.plane=m_plane;
            mv.compMode=m_comp; mv.compR=m_compR; mv.isTransition=isTrans;
            mv.passColor=(li<m_lpc.size())?m_lpc[li]:PassColor::Normal;
            // Avance G93
            if(m_g93&&m_f>1e-9){ double dl=hypot(hypot(m_nx-m_x,m_ny-m_y),m_nz-m_z); mv.f=dl*m_f; }
            else mv.f=m_f;
            // Arc I,J,K,R
            if(isArc){ double ii=0,jj=0,kk=0,rr=0; bool hr=parseParam(code,'R',rr);
                parseParam(code,'I',ii); parseParam(code,'J',jj); parseParam(code,'K',kk);
                mv.i=au(ii); mv.j=au(jj); mv.k=au(kk); mv.r=rr; mv.hasR=hr; }

            // Mouvements initiaux G0 : skip jusqu'au premier G0 XY inclus
            // Évite le mouvement parasite (0,0,0)→(0,0,Z) visible en simulation
            if (m_firstMove && isRapid) {
                m_x=m_nx; m_y=m_ny; m_z=m_nz;
                if (hx || hy) {
                    m_firstMove = false;
                    // Sauvegarder la position initiale après tous les skips
                    m_initX=m_nx; m_initY=m_ny; m_initZ=m_nz;
                }
                continue;  // pas de mouvement affiché
            }
            if (!isRapid) m_firstMove = false;  // premier G1 → fin du mode init

            m_moves.append(mv);
            m_x=m_nx; m_y=m_ny; m_z=m_nz;
        }
    }

    const QVector<ParsedMove> &moves() const { return m_moves; }
    double initialX() const { return m_initX; }
    double initialY() const { return m_initY; }
    double initialZ() const { return m_initZ; }

private:
    double m_x=0,m_y=0,m_z=0, m_nx=0,m_ny=0,m_nz=0;
    double m_initX=0,m_initY=0,m_initZ=0;  // position après les G0 initiaux skippés
    bool   m_firstMove=true;   // vrai jusqu'au premier G0 XY → initialise position sans mouvement
    double m_f=0, m_rDefault=3, m_lastCommentR=-1;
    int    m_T=0, m_plane=17, m_comp=0;
    double m_compR=0;
    bool   m_g91=false, m_inch=false, m_g93=false, m_modalG1=false;
    bool   m_inCycle=false, m_cycleIsPeck=false;
    double m_rotAngle=0.0;  // angle G68 en radians
    double m_cycleZ=0, m_cycleR=2, m_cycleQ=2;
    QVector<ParsedMove> m_moves;
    QVector<PassColor>  m_lpc;

    void reset(){ m_x=m_y=m_z=0; m_f=0; m_T=0; m_plane=17; m_comp=0; m_compR=0; m_firstMove=true;
        m_g91=m_inch=m_g93=m_modalG1=m_inCycle=false; m_lastCommentR=-1; m_rotAngle=0.0; }
    double au(double v) const { return m_inch?v*25.4:v; }

    void prescanPassColors(const QVector<GCodeLine> &lines){
        m_lpc.assign(lines.size(),PassColor::Normal);
        static const QRegularExpression reG41(R"(\bG41\b)"),reG40(R"(\bG40\b)"),
            reFin(R"(finition|finishing)",QRegularExpression::CaseInsensitiveOption);
        struct Bloc{int s,e;bool fin;};
        QVector<Bloc> blocs; int gs=-1; bool inG41=false;
        for(int li=0;li<lines.size();++li){
            QString up=lines[li].code.toUpper();
            bool isCmt=(lines[li].type==GCodeLine::Comment);
            if(!isCmt&&up.contains(reG41)&&!inG41){inG41=true;gs=li;}
            if(!isCmt&&up.contains(reG40)&&inG41){
                inG41=false; bool f=false;
                for(int k=qMax(0,gs-15);k<=gs;++k){ if(lines[k].type!=GCodeLine::Comment) continue;
                    if(reFin.match(lines[k].code+' '+lines[k].comment).hasMatch()){f=true;break;} }
                blocs.append({gs,li,f}); gs=-1;
            }
        }
        QVector<PassColor> bc(blocs.size(),PassColor::Normal);
        for(int i=0;i<blocs.size();++i) if(blocs[i].fin) bc[i]=PassColor::FinitionFinale;
        for(int i=0;i<blocs.size();){
            if(!blocs[i].fin){ int e=i; while(e+1<blocs.size()&&!blocs[e+1].fin)e++; bc[e]=PassColor::EbaucheFinale; i=e+1; }
            else i++;
        }
        for(int i=0;i<blocs.size();++i) for(int li=blocs[i].s;li<=blocs[i].e;++li) m_lpc[li]=bc[i];
    }

    void resolveCompR(const GCodeLine &gl,const QString &up,
                      const QRegularExpression &reDv,const QRegularExpression &reDm,
                      const QRegularExpression &reDn){
        auto m=reDv.match(gl.comment); auto m2=reDm.match(gl.comment);
        if(m.hasMatch()) m_compR=m.captured(1).toDouble();
        else if(m2.hasMatch()) m_compR=m2.captured(1).toDouble()/2.0;
        if(m_compR<0.1&&m_lastCommentR>0.05) m_compR=m_lastCommentR;
        if(m_compR<0.1){ auto md=reDn.match(up); if(md.hasMatch()){
                int d=md.captured(1).toInt(); if(d>0){ const ToolData *td=ToolDatabase::instance().toolByT(d);
                    if(td&&td->Routil>0.1) m_compR=td->Routil; }}}
        if(m_compR<0.1) m_compR=m_rDefault;
    }

    void addCycle(double nx,double ny,int li){
        auto addMv=[&](ParsedMove::Kind k,double x,double y,double z){
            ParsedMove mv; mv.kind=k; mv.x=x; mv.y=y; mv.z=z;
            mv.toolT=m_T; mv.lineIdx=li; mv.plane=m_plane; m_moves.append(mv); };
        addMv(ParsedMove::Rapid,nx,ny,m_cycleR);
        if(!m_cycleIsPeck){ addMv(ParsedMove::Cut,nx,ny,m_cycleZ); addMv(ParsedMove::Rapid,nx,ny,m_cycleR); }
        else { double q=qMax(0.1,m_cycleQ), zc=m_cycleR-q;
            while(zc>m_cycleZ){ addMv(ParsedMove::Cut,nx,ny,zc); addMv(ParsedMove::Rapid,nx,ny,m_cycleR);
                addMv(ParsedMove::Rapid,nx,ny,zc+0.5); zc-=q; }
            addMv(ParsedMove::Cut,nx,ny,m_cycleZ); addMv(ParsedMove::Rapid,nx,ny,m_cycleR); }
    }

    static bool parseParam(const QString &code,QChar name,double &out){
        QString stripped = code;
        // Supprimer commentaires ( ... )
        { int s=stripped.indexOf('(');
            while(s>=0){ int e=stripped.indexOf(')',s);
                if(e<0) { stripped=stripped.left(s); break; }
                stripped.remove(s,e-s+1); s=stripped.indexOf('('); } }
        // Supprimer commentaires ; ... (style Fanuc/BOSS)
        { int s=stripped.indexOf(';'); if(s>=0) stripped=stripped.left(s); }
        QString up = stripped.toUpper();
        QChar un = name.toUpper();
        int idx = -1;
        for (int i = 0; i < up.size(); i++) if (up[i] == un) { idx = i; break; }
        if (idx < 0) return false;
        int j = idx + 1;
        while (j < up.size() && up[j] == ' ') ++j;
        int st = j;
        if (j < up.size() && (up[j] == '-' || up[j] == '+')) ++j;
        while (j < up.size() && (up[j].isDigit() || up[j] == '.')) ++j;
        if (j == st + (st < up.size() && (up[st] == '-' || up[st] == '+') ? 1 : 0)) return false;
        if (st >= stripped.size() || j > stripped.size() || j < st) return false;
        bool ok; out = stripped.mid(st, j - st).toDouble(&ok); return ok;
    }
};

// ═══════════════════════════════════════════════════════════════
//  Heightmap
// ═══════════════════════════════════════════════════════════════
void Heightmap::init(float x0,float x1,float y0,float y1,float zt,float zb)
{ xMin=x0;xMax=x1;yMin=y0;yMax=y1;zTop=zt;zBot=zb;
    data.assign(res*res,zTop); toolMap.assign(res*res,0); markAllDirty(); }

// ── Profil d'outil : de combien la matière remonte sous l'outil ──
//  à la distance d de son axe (0 au centre pour une fraise plate).
//  Un seul code pour tous les outils.
static inline float toolProfileZ(const ToolInfo &ti, float d)
{
    const float r = ti.r;
    if (ti.r2 < -0.5f) {                       // pointe conique (foret, chanfrein)
        if (d <= ti.tipR) return 0.f;
        return (d - ti.tipR) / qMax(0.05f, ti.coneTan);
    }
    if (ti.r2 >= r * 0.85f) {                  // fraise boule
        float s = r*r - d*d;
        return r - sqrtf(s > 0.f ? s : 0.f);
    }
    if (ti.r2 > 0.01f) {                       // fraise torique (rayon de bec)
        const float plat = r - ti.r2;
        if (d <= plat) return 0.f;
        const float e = d - plat;
        float s = ti.r2*ti.r2 - e*e;
        return ti.r2 - sqrtf(s > 0.f ? s : 0.f);
    }
    return 0.f;                                // fraise plate
}

// ── cutSegment — coupe d'un déplacement en une seule passe ───────
//  Méthode z-map (CAMotics, simulateur LinuxCNC, VerdierCAM) :
//  chaque case de la boîte englobante du segment (élargie du rayon)
//  est visitée UNE fois. On cherche sur le segment la position de
//  l'outil qui descend le plus bas sur cette case, en tenant compte
//  de la pente (rampes, hélices) et du profil de l'outil.
bool Heightmap::cutSegment(const QVector3D &a,const QVector3D &b,const ToolInfo &ti,int toolT)
{
    if (data.empty() || res < 2) return false;
    const float R = ti.r;
    if (R <= 0.f) return false;
    const float cw = (xMax-xMin)/(res-1), ch = (yMax-yMin)/(res-1);
    if (cw <= 0.f || ch <= 0.f) return false;

    // Bord anti-crénelé : une case que le bord de l'outil ne couvre qu'en
    // partie est creusée en proportion. Le bord de la coupe tombe ainsi
    // au bon endroit à une fraction de case près (plus d'escalier).
    const float hEdge = 0.5f * qMax(cw, ch);
    const float Rx    = R + hEdge;                 // rayon de recherche élargi
    const float Rx2   = Rx*Rx;
    const float minX = qMin(a.x(),b.x()) - Rx, maxX = qMax(a.x(),b.x()) + Rx;
    const float minY = qMin(a.y(),b.y()) - Rx, maxY = qMax(a.y(),b.y()) + Rx;
    const int i0 = qMax(0,     (int)floorf((minX - xMin)/cw));
    const int i1 = qMin(res-1, (int)ceilf ((maxX - xMin)/cw));
    const int j0 = qMax(0,     (int)floorf((minY - yMin)/ch));
    const int j1 = qMin(res-1, (int)ceilf ((maxY - yMin)/ch));
    if (i0 > i1 || j0 > j1) return false;

    const float ax = a.x(), ay = a.y(), az = a.z();
    const float dx = b.x()-ax, dy = b.y()-ay, dz = b.z()-az;
    const float L2 = dx*dx + dy*dy;
    const bool  plunge = (L2 < 1e-10f);           // déplacement vertical pur
    const float invL = plunge ? 0.f : 1.f/sqrtf(L2);
    const float zLow = qMin(az, b.z());

    // Taux de couverture de la case selon sa distance d à l'axe de l'outil
    auto coverage = [&](float d)->float {
        if (d <= R - hEdge) return 1.f;
        const float w = (Rx - d) / (2.f*hEdge);
        return w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
    };

    // Hauteur « avant coupe » d'une case de bord : la plus haute des cases
    // voisines. Une case couverte à 40 % vaut toujours 40 % de la coupe,
    // même si 20 petits segments de G1 passent dessus (avant, les coupes
    // partielles s'additionnaient -> bord rongé en dents de scie).
    auto refH = [&](int i, int j, float cur)->float {
        float m = cur;
        for (int di = -1; di <= 1; ++di) {
            const int ii = i + di; if (ii < 0 || ii >= res) continue;
            const float *r2 = &data[size_t(ii)*res];
            for (int dj = -1; dj <= 1; ++dj) {
                const int jj = j + dj; if (jj < 0 || jj >= res) continue;
                if (r2[jj] > m) m = r2[jj];
            }
        }
        return m;
    };

    int ci0 = res, ci1 = -1, cj0 = res, cj1 = -1;
    for (int i = i0; i <= i1; ++i) {
        const float px = xMin + cw*i;
        const float rx = px - ax;
        float *row = &data[size_t(i)*res];
        for (int j = j0; j <= j1; ++j) {
            const float py = yMin + ch*j;
            const float ry = py - ay;
            float &c = row[j];
            const float cur = c;
            float best = 1e30f;
            float zRef = -1e30f;
            if (plunge) {
                const float d2 = rx*rx + ry*ry;
                if (d2 > Rx2) continue;
                const float d = sqrtf(d2);
                const float zc = zLow + toolProfileZ(ti, qMin(d, R));
                if (zc < cur) {
                    const float cv = coverage(d);
                    if (cv >= 1.f) best = zc;
                    else { if (zRef < -1e29f) zRef = refH(i, j, cur);
                           best = zc + (1.f - cv) * (zRef - zc); }
                }
            } else {
                // Position du point le plus proche sur la droite, et
                // plage [t0,t1] où la case est sous le diamètre de l'outil
                const float tc = (rx*dx + ry*dy) / L2;
                float h2 = rx*rx + ry*ry - tc*tc*L2;
                if (h2 < 0.f) h2 = 0.f;
                if (h2 > Rx2) continue;
                const float dt = sqrtf(Rx2 - h2) * invL;
                const float t0 = qMax(0.f, tc - dt);
                const float t1 = qMin(1.f, tc + dt);
                if (t0 > t1) continue;
                // Le Z varie linéairement : le point le plus bas est à une
                // extrémité de la plage ou au plus près de l'axe.
                const float ts[3] = { t0, t1, qBound(t0, tc, t1) };
                for (float t : ts) {
                    const float ex = px - (ax + t*dx);
                    const float ey = py - (ay + t*dy);
                    const float d2 = ex*ex + ey*ey;
                    if (d2 > Rx2 * 1.0001f) continue;
                    const float d  = sqrtf(d2);
                    const float zc = az + t*dz + toolProfileZ(ti, qMin(d, R));
                    if (zc >= cur + 0.02f) continue;   // 0.02 : laisse aussi passer les contacts (couleur outil)
                    const float cv = coverage(d);
                    float z;
                    if (cv >= 1.f) z = zc;
                    else { if (zRef < -1e29f) zRef = refH(i, j, cur);
                           z = zc + (1.f - cv) * (zRef - zc); }
                    if (z < best) best = z;
                }
            }
            // Le dernier outil qui touche la surface (meme sans enlever de matiere,
            // a 0.02 mm pres) en prend la couleur : une finition reprend toute la paroi.
            if (best >= c && best < c + 0.02f && !toolMap.empty()
                && toolMap[size_t(i)*res + j] != toolT) {
                toolMap[size_t(i)*res + j] = toolT;
                if (i < ci0) ci0 = i;
                if (i > ci1) ci1 = i;
                if (j < cj0) cj0 = j;
                if (j > cj1) cj1 = j;
            }
            if (best < c) {
                c = best;
                if (!toolMap.empty()) toolMap[size_t(i)*res + j] = toolT;
                if (i < ci0) ci0 = i;
                if (i > ci1) ci1 = i;
                if (j < cj0) cj0 = j;
                if (j > cj1) cj1 = j;
            }
        }
    }
    if (ci1 >= ci0) {                             // agrandir la zone à renvoyer
        if (!hasDirty()) { dI0=ci0; dI1=ci1; dJ0=cj0; dJ1=cj1; }
        else {
            dI0 = qMin(dI0,ci0); dI1 = qMax(dI1,ci1);
            dJ0 = qMin(dJ0,cj0); dJ1 = qMax(dJ1,cj1);
        }
        return true;                              // de la matière a été enlevée
    }
    return false;
}

void Heightmap::applyTool(float cx,float cy,float r,float z,int toolT)
{
    if(data.empty()||res<2) return;
    float cw=(xMax-xMin)/(res-1),ch=(yMax-yMin)/(res-1);
    int rc=(int)ceilf(r/qMin(cw,ch))+1;
    int ic=qBound(0,(int)((cx-xMin)/(xMax-xMin)*(res-1)),res-1);
    int jc=qBound(0,(int)((cy-yMin)/(yMax-yMin)*(res-1)),res-1);
    for(int i=qMax(0,ic-rc);i<=qMin(res-1,ic+rc);i++){
        float wx=worldX(i),dx2=(wx-cx)*(wx-cx); if(dx2>r*r) continue;
        for(int j=qMax(0,jc-rc);j<=qMin(res-1,jc+rc);j++){
            float wy=worldY(j);
            if(dx2+(wy-cy)*(wy-cy)<=r*r){
                float &c=data[i*res+j];
                if(z<c){ c=z; if(!toolMap.empty()) toolMap[i*res+j]=toolT; }
                else if(z<c+0.02f && !toolMap.empty()) toolMap[i*res+j]=toolT;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  Ctor / Dtor
// ═══════════════════════════════════════════════════════════════
Sim3DWidget::Sim3DWidget(QWidget *parent):QOpenGLWidget(parent)
{
    setMinimumSize(300,300);
    m_occ = new OccData();
    m_animTimer=new QTimer(this); m_animTimer->setInterval(33); // 30fps — adapté i5-4570T
    connect(m_animTimer,&QTimer::timeout,this,&Sim3DWidget::onAnimTick);
    m_hmapWatcher=new QFutureWatcher<void>(this);
    connect(m_hmapWatcher,&QFutureWatcher<void>::finished,this,[this]{
        m_hmapPending=false; m_progressTimer->stop(); emit progressChanged(100); update();
        if (m_buildPending) { m_buildPending=false; const int r=m_pendingRes;
            QTimer::singleShot(0, this, [this, r]{ requestBuildCurrent(r); }); }
    });
    m_progressTimer=new QTimer(this); m_progressTimer->setInterval(80);
    connect(m_progressTimer,&QTimer::timeout,this,[this]{ emit progressChanged(m_hmapProgress.load()); });
    // ── Timer de rebuild périodique DÉSACTIVÉ ────────────────────
    //    L'ancienne approche rejouait TOUTE l'historique d'usinage à
    //    chaque rafraîchissement (très coûteux quand on est avancé
    //    dans le programme). Maintenant la matière est mise à jour
    //    incrémentalement dans advanceAnimation() → applyToolIncremental,
    //    donc plus besoin de rebuild périodique pendant la simu.
    //    Le timer reste créé pour compatibilité (start/stop dans play/pause)
    //    mais son callback ne fait plus rien.
    m_hmapRefreshTimer=new QTimer(this); m_hmapRefreshTimer->setInterval(120);
    // (callback retiré : plus de rebuild rétro-actif)
    buildTransportBar();
    m_hoverLbl = new QLabel(this);
    m_hoverLbl->setTextFormat(Qt::RichText);
    m_hoverLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hoverLbl->hide();
    m_lpTimer = new QTimer(this); m_lpTimer->setSingleShot(true); m_lpTimer->setInterval(350);
    connect(m_lpTimer, &QTimer::timeout, this, &Sim3DWidget::computeLoupe);
}
Sim3DWidget::~Sim3DWidget()
{
    m_animTimer->stop();
    stopBuild();
    invalidateCheckpoints();
    cancelLoupe();
    makeCurrent();
    if (m_lpTex)     { glDeleteTextures(1, &m_lpTex);     m_lpTex = 0; }
    if (m_lpToolTex) { glDeleteTextures(1, &m_lpToolTex); m_lpToolTex = 0; }
    if (m_lpVBO)     { glDeleteBuffers(1, &m_lpVBO);      m_lpVBO = 0; }
    if (m_lpIBO)     { glDeleteBuffers(1, &m_lpIBO);      m_lpIBO = 0; }
    if(m_hmapVBO){ glDeleteBuffers(1,&m_hmapVBO); m_hmapVBO=0; }
    if(m_hmTex)    { glDeleteTextures(1,&m_hmTex);    m_hmTex=0; }
    if(m_hmToolTex){ glDeleteTextures(1,&m_hmToolTex); m_hmToolTex=0; }
    if(m_hmMeshVBO){ glDeleteBuffers(1,&m_hmMeshVBO); m_hmMeshVBO=0; }
    if(m_hmIdxFull){ glDeleteBuffers(1,&m_hmIdxFull); m_hmIdxFull=0; }
    if(m_hmIdxLod) { glDeleteBuffers(1,&m_hmIdxLod);  m_hmIdxLod=0; }
    delete m_hmProg; m_hmProg=nullptr;
    doneCurrent();
    delete m_occ;
}

// ═══════════════════════════════════════════════════════════════
//  API publique — stock / G-code
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::setStock(const StockData &s)
{
    m_stock=s; m_stock.valid=true;
    if(m_autoRes){
        // Résolution selon la taille du brut : ~0,06 mm par case,
        // bornée entre 512 et 3072 cases de côté (= grille 3D max).
        const float maxDim = qMax(s.xMax-s.xMin, s.yMax-s.yMin);
        m_hmapRes = qBound(512, int(ceilf(maxDim/0.06f))+1, 3072);
    }
    {
        const float maxDim = qMax(s.xMax-s.xMin, s.yMax-s.yMin);
        m_animRes = qMin(m_hmapRes, qBound(256, int(ceilf(maxDim/0.25f))+1, 1024));
    }
    m_hmap.res=m_hmapRes;
    m_hmap.init(s.xMin,s.xMax,s.yMin,s.yMax,s.zMax,s.zMin);
    m_pivot={(s.xMin+s.xMax)/2.f,(s.yMin+s.yMax)/2.f,(s.zMin+s.zMax)/2.f};
    float d=sqrtf(powf(s.xMax-s.xMin,2)+powf(s.yMax-s.yMin,2)+powf(s.zMax-s.zMin,2));
    m_dist=d*1.1f;
    // ── Plus de bascule OCC en mode plein ─────────────────────
    // Heightmap utilisée dans tous les cas (cf setStockOpaque)
    m_useOcc = false;
    { QMutexLocker lk(&m_occMeshMutex); m_occMesh.clear(); }
    update();
    if (!m_moves.isEmpty()) startCheckpoints(); else invalidateCheckpoints();
}
void Sim3DWidget::setLines(const QVector<GCodeLine> &lines,double rDefault)
{
    m_rDefault=rDefault;
    buildMoves(lines,rDefault);
    // ── Heightmap = brut intact au chargement ───────────────────
    // Plus de buildHeightmapAsync() ici : c'était le bug "matière en
    // avance". Cette fonction calculait par défaut TOUS les moves
    // (upTo=-1 quand m_playing=false) → la pièce apparaissait déjà
    // entièrement usinée avant le clic Play. La simulation progressive
    // se fait maintenant via applyToolIncremental dans advanceAnimation.
    if (m_stock.valid) {
        QMutexLocker lk(&m_hmapMutex);
        m_hmap.res = m_hmapRes;
        m_hmap.init(m_stock.xMin, m_stock.xMax,
                    m_stock.yMin, m_stock.yMax,
                    m_stock.zMax, m_stock.zMin);
        m_hmapNormalsDirty = true;
        m_hmapVBODirty     = true;
    }
    // Reset état incrémental → prochain creusement partira de la
    // position courante de l'outil sans rejouer d'historique
    m_lastIncrValid = false;
    m_lastIncrPos   = QVector3D();
    update();
    startCheckpoints();
}
void Sim3DWidget::buildMovesOnly(const QVector<GCodeLine> &lines,double rDefault)
{
    stopBuild();
    m_rDefault=rDefault; buildMoves(lines,rDefault); resetSim(); update();
    startCheckpoints();
}
void Sim3DWidget::clear()
{
    pause();
    stopBuild();
    invalidateCheckpoints();
    m_moves.clear(); m_ebaucheContour.clear(); m_finitionContour.clear();
    m_curMove=0; m_curSeg=0; m_curSegT=0.f;
    m_lastIncrValid = false;
    m_lastIncrPos   = QVector3D();
    m_hmap.res=m_hmapRes;
    if(m_stock.valid) m_hmap.init(m_stock.xMin,m_stock.xMax,m_stock.yMin,m_stock.yMax,m_stock.zMax,m_stock.zMin);
    update();
}

// ═══════════════════════════════════════════════════════════════
//  OCC — brut STEP
// ═══════════════════════════════════════════════════════════════
// ── setStockFromBox — crée boîte OCC depuis StockData ─────────
bool Sim3DWidget::setStockFromBox(const StockData &s)
{
#ifdef WITH_OCC
    if(!s.valid) return false;
    // BRepPrimAPI_MakeBox : coin min → dimensions
    gp_Pnt pMin(s.xMin, s.yMin, s.zMin);
    BRepPrimAPI_MakeBox box(pMin,
                            double(s.xMax-s.xMin),
                            double(s.yMax-s.yMin),
                            double(s.zMax-s.zMin));
    TopoDS_Shape sh = box.Shape();
    if(sh.IsNull()) return false;
    OCC_SHAPE = sh;
    OCC_SHAPE_ORIGINAL = sh;
    m_useOcc = true;
    triangulateOccShape(0);  // brut intact → toolT=0 = gris aluminium
    qDebug()<<"[OCC] setStockFromBox:"<<m_occMesh.size()/3<<"triangles";
    update(); return true;
#else
    Q_UNUSED(s)
    qWarning()<<"[OCC] setStockFromBox: OCC non compilé";
    return false;
#endif
}

bool Sim3DWidget::setStockFromStep(const QString &path)
{
#ifdef WITH_OCC
    STEPControl_Reader reader;
    if(reader.ReadFile(path.toLocal8Bit().constData())!=IFSelect_RetDone){
        qWarning()<<"[OCC] setStockFromStep: lecture échouée:"<<path; return false;
    }
    if(reader.NbRootsForTransfer()==0){
        qWarning()<<"[OCC] setStockFromStep: aucune racine STEP"; return false;
    }
    reader.TransferRoots();
    TopoDS_Shape sh=reader.OneShape();
    if(sh.IsNull()){ qWarning()<<"[OCC] setStockFromStep: shape nulle"; return false; }
    OCC_SHAPE=sh; OCC_SHAPE_ORIGINAL=sh; m_useOcc=true;
    triangulateOccShape(0);
    qDebug()<<"[OCC] STEP chargé:"<<path<<"  triangles:"<<m_occMesh.size()/3;
    update(); return true;
#else
    Q_UNUSED(path)
    qWarning()<<"[OCC] setStockFromStep: OCC non compilé (CONFIG+=WITH_OCC)";
    return false;
#endif
}

void Sim3DWidget::clearOccShape()
{
#ifdef WITH_OCC
    OCC_SHAPE=OCC_SHAPE_ORIGINAL;
    m_useOcc=!OCC_SHAPE.IsNull();
    triangulateOccShape(0);
#else
    m_useOcc=false; QMutexLocker lk(&m_occMeshMutex); m_occMesh.clear();
#endif
    update();
}

// ── triangulateOccShape ────────────────────────────────────────
// BRepMesh_IncrementalMesh → parcours TopExp → m_occMesh (flat).
// Chaque triangle = 3 MeshVertex consécutifs dans le vecteur.
void Sim3DWidget::triangulateOccShape(int toolT)
{
#ifdef WITH_OCC
    QVector<MeshVertex> newMesh;  // déclaration locale — FIX bug newMesh non déclarée
    if(!OCC_SHAPE.IsNull()){
        BRepMesh_IncrementalMesh mesher(OCC_SHAPE,0.05);
        mesher.Perform();
        for(TopExp_Explorer ex(OCC_SHAPE,TopAbs_FACE);ex.More();ex.Next()){
            TopoDS_Face face=TopoDS::Face(ex.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri=BRep_Tool::Triangulation(face,loc);
            if(tri.IsNull()) continue;
            bool rev=(face.Orientation()==TopAbs_REVERSED);
            const gp_Trsf trsf=loc.IsIdentity()?gp_Trsf():loc.Transformation();
            for(int t=1;t<=tri->NbTriangles();++t){
                int n1,n2,n3; tri->Triangle(t).Get(n1,n2,n3);
                if(rev) std::swap(n1,n3);
                gp_Pnt p1=tri->Node(n1).Transformed(trsf);
                gp_Pnt p2=tri->Node(n2).Transformed(trsf);
                gp_Pnt p3=tri->Node(n3).Transformed(trsf);
                gp_Vec v12(p1,p2),v13(p1,p3),n=v12.Crossed(v13);
                double mag=n.Magnitude(); if(mag<1e-14) continue;
                n/=mag;
                auto push=[&](const gp_Pnt &p){
                    MeshVertex mv;
                    mv.x=float(p.X());mv.y=float(p.Y());mv.z=float(p.Z());
                    mv.nx=float(n.X());mv.ny=float(n.Y());mv.nz=float(n.Z());
                    mv.toolT=toolT;
                    newMesh.append(mv);
                };
                push(p1); push(p2); push(p3);
            }
        }
    }
    { QMutexLocker lk(&m_occMeshMutex); m_occMesh=std::move(newMesh); }
    qDebug()<<"[OCC] triangulateOccShape:"<<m_occMesh.size()/3<<"triangles"
             <<m_occMesh.size()*int(sizeof(MeshVertex))/1024<<"KB";
#else
    QMutexLocker lk(&m_occMeshMutex); m_occMesh.clear(); Q_UNUSED(toolT)
#endif
}

// ── buildToolShape ─────────────────────────────────────────────
// Fraise OCC positionnée pointe en (cx,cy,z), queue vers +Z.
//   ToolData::Foret : cône 118° + cylindre
//   r2 ≈ r          : ball-nose (sphère + cylindre)
//   r2 > 0          : bull-nose (cylindre, approx.)
//   sinon           : cylindre classique
#ifdef WITH_OCC
static TopoDS_Shape buildToolShapeImpl(const ToolInfo &tool,
                                       float cx,float cy,float z)
{
    double r  =qMax(0.01,double(tool.r));
    double len=qMax(r,double(tool.len));
    double r2 =double(tool.r2);
    gp_Ax2 ax(gp_Pnt(cx,cy,z),gp_Dir(0,0,1));

    ToolData::ToolType ttype=ToolData::Fraise4T;
    if(tool.T>0){ const ToolData *td=ToolDatabase::instance().toolByT(tool.T); if(td) ttype=td->toolType; }

    if(ttype==ToolData::Foret){
        double ha=59.0*M_PI/180.0, ch=r/std::tan(ha);
        BRepPrimAPI_MakeCone cone(ax,0.0,r,ch);
        if(len>ch){
            BRepPrimAPI_MakeCylinder cyl(gp_Ax2(gp_Pnt(cx,cy,z+ch),gp_Dir(0,0,1)),r,len-ch);
            return BRepAlgoAPI_Fuse(cone.Shape(),cyl.Shape()).Shape();
        }
        return cone.Shape();
    }
    if(r2>=r-0.01){
        BRepPrimAPI_MakeSphere sph(gp_Ax2(gp_Pnt(cx,cy,z+r2),gp_Dir(0,0,1)),r2);
        if(len>r2){
            BRepPrimAPI_MakeCylinder cyl(gp_Ax2(gp_Pnt(cx,cy,z+r2),gp_Dir(0,0,1)),r,len-r2);
            return BRepAlgoAPI_Fuse(sph.Shape(),cyl.Shape()).Shape();
        }
        return sph.Shape();
    }
    return BRepPrimAPI_MakeCylinder(ax,r,len).Shape();
}
#endif

// ── applyToolCut ──────────────────────────────────────────────
void Sim3DWidget::applyToolCut(float cx,float cy,float r,float z)
{
#ifdef WITH_OCC
    if(OCC_SHAPE.IsNull()||r<0.01f) return;
    ToolInfo ti; ti.r=r; ti.len=r*5.f; ti.r2=0.f; ti.T=0;
    TopoDS_Shape tsh=buildToolShapeImpl(ti,cx,cy,z);
    if(tsh.IsNull()) return;
    BRepAlgoAPI_Cut cut(OCC_SHAPE,tsh);
    cut.SetRunParallel(Standard_False);
    cut.Build();
    if(!cut.IsDone()||cut.Shape().IsNull()) return;
    OCC_SHAPE=cut.Shape();
#else
    Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(r) Q_UNUSED(z)
#endif
}

// ── applyAllOccCuts ───────────────────────────────────────────
// Rejoue tous les moves Cut sur le brut OCC.
// Sous-échantillonnage : 1 coupe tous les 2×rayon (CSG n'a pas besoin de plus).
void Sim3DWidget::applyAllOccCuts()
{
#ifdef WITH_OCC
    if(!m_useOcc||OCC_SHAPE_ORIGINAL.IsNull()){
        qWarning()<<"[OCC] applyAllOccCuts: pas de brut OCC"; return; }
    emit occProgressChanged(0);
    OCC_SHAPE=OCC_SHAPE_ORIGINAL;  // reset
    int total=0;
    for(const SimMove &mv:std::as_const(m_moves)) if(mv.kind==SimMove::Cut) total+=mv.pts.size()-1;
    int done=0;
    for(const SimMove &mv:std::as_const(m_moves)){
        if(mv.kind!=SimMove::Cut) continue;
        ToolInfo ti=resolveToolInfo(mv.toolT);
        for(int i=1;i<mv.pts.size();i++){
            const QVector3D &a=mv.pts[i-1],&b=mv.pts[i];
            float sl=a.distanceToPoint(b);
            int ns=qMax(1,int(sl/(ti.r*2.0f)));
            for(int k=0;k<=ns;k++){
                float t=float(k)/float(ns);
                applyToolCut(a.x()+(b.x()-a.x())*t, a.y()+(b.y()-a.y())*t,
                             ti.r, a.z()+(b.z()-a.z())*t);
            }
            ++done; if(total>0) emit occProgressChanged(done*95/total);
        }
    }
    // Trianguler avec toolT = dernier outil utilisé
    int lastToolT = m_moves.isEmpty() ? 0 : m_moves.last().toolT;
    triangulateOccShape(lastToolT);
    emit occProgressChanged(100);
    qDebug()<<"[OCC] applyAllOccCuts terminé:"<<m_occMesh.size()/3<<"triangles";
    update();
#else
    qWarning()<<"[OCC] applyAllOccCuts: OCC non compilé";
#endif
}

QString Sim3DWidget::occInfo() const
{
    if(!m_useOcc||m_occMesh.isEmpty()) return "OCC désactivé (heightmap active)";
    return QString("OCC mesh : %1 triangles — %2 KB")
        .arg(m_occMesh.size()/3)
        .arg(m_occMesh.size()*int(sizeof(MeshVertex))/1024);
}

// ═══════════════════════════════════════════════════════════════
//  Animation
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::play()
{
    if(m_moves.isEmpty()) return;
    // Grille légère pendant l'animation : on y repasse en rejouant (vite,
    // elle est petite) les déplacements déjà faits.
    if (m_rwMul > 0) { m_rwMul = 0; m_rwTimer->stop(); }
    m_fineTimer->stop();
    if (m_stock.valid && m_animRes > 1 && (m_hmap.res != m_animRes || m_needSyncRebuild)) {
        stopBuild();
        m_buildPending = false; m_needSyncRebuild = false;
        m_buildRes = m_animRes;
        m_bpMove = (m_curSeg > 0 || m_curSegT > 0.f) ? m_curMove : -1;
        m_bpSeg = m_curSeg; m_bpPos = m_toolPos;
        buildHeightmapSync(m_curMove > 0 ? m_curMove - 1 : -3);   // moves terminés + morceau en cours
    }
    m_playing=true; m_animTimer->start(); m_hmapRefreshTimer->start(); emit playingChanged(true);
    updateTransportUi();
}
void Sim3DWidget::pause()
{
    m_playing=false; m_animTimer->stop(); m_hmapRefreshTimer->stop(); emit playingChanged(false);
    if (m_ffMul != 1) { m_ffMul = 1; setSpeed(m_speed); }
    updateTransportUi();
    // Arrêt en cours de programme : la pièce passe sur la grille fine,
    // calculée en tâche de fond jusqu'au déplacement en cours.
    // (En fin de programme, advanceAnimation lance déjà le calcul final.)
    if (m_stock.valid && m_curMove > 0 && m_curMove < m_moves.size()
        && m_hmap.res != m_hmapRes)
    {
        m_bpMove = (m_curSeg > 0 || m_curSegT > 0.f) ? m_curMove : -1;
        m_bpSeg = m_curSeg; m_bpPos = m_toolPos;
        buildHeightmapAsync(m_curMove - 1, m_hmapRes);
    }
}
void Sim3DWidget::resetSim()
{
    m_curMove=0;            // avant pause() : pas de calcul fin inutile
    pause();
    stopBuild();
    m_curMove=0; m_curSeg=0; m_curSegT=0.f; m_lastEmittedLine=-1;
    m_dlDirty=true; m_hmapVBODirty=true; m_buildPending=false; m_needSyncRebuild=false;
    ++m_matVer;
    if (m_rwMul > 0) { m_rwMul = 0; m_rwTimer->stop(); }
    { QMutexLocker lk(&m_hmapMutex);
        if(m_stock.valid){ m_hmap.res=m_hmapRes; m_hmap.init(m_stock.xMin,m_stock.xMax,m_stock.yMin,m_stock.yMax,m_stock.zMax,m_stock.zMin); }
        m_hmapNormalsDirty=true; m_hmapVBODirty=true; }
    // ── Reset état incrémental ────────────────────────────────
    // La heightmap est vide → prochain applyToolIncremental partira
    // de la position courante de l'outil sans rejouer d'historique.
    m_lastIncrValid = false;
    m_lastIncrPos   = QVector3D();
    // Remettre l'outil au-dessus du brut après reset
    {
        float zTop = m_stock.valid ? m_stock.zMax : 0.f;
        ToolInfo ti = (!m_moves.isEmpty()) ? resolveToolInfo(m_moves.front().toolT) : resolveToolInfo(0);
        m_toolPos = QVector3D(0.f, 0.f, zTop + ti.len + 20.f);
    }
    updateTransportUi();
    update();
}
void Sim3DWidget::setSpeed(int v)
{
    m_speed = qBound(1, v, 300);
    // Courbe de vitesse CONTINUE et progressive (aucun saut) :
    //   pas d'avance exponentiel. Lent et fin en bas, très rapide en haut.
    double tnorm = double(m_speed - 1) / 299.0;          // 0..1
    m_stepMM = float(0.02 * std::pow(7500.0, tnorm));     // 0.02 → 150 mm
    m_animTimer->setInterval(16);                         // 60 fps affichage
    m_hmapRealtime = false;
    m_stepMM = qMax(0.01f, m_stepMM);
    m_stepBase = m_stepMM;
    m_stepMM *= float(qMax(1, m_ffMul));       // avance rapide (barre de lecture)
}
void Sim3DWidget::applyResolution()
{
    if(!m_stock.valid) return;
    stopBuild();
    m_hmap.res=m_hmapRes; m_hmap.init(m_stock.xMin,m_stock.xMax,m_stock.yMin,m_stock.yMax,m_stock.zMax,m_stock.zMin); m_hmapVBODirty=true;
    // Heightmap reinit → reset état incrémental
    m_lastIncrValid = false;
    m_lastIncrPos   = QVector3D();
    // Si on est avancé dans le programme, rejouer l'historique pour
    // que la nouvelle heightmap reflète l'état actuel
    if (m_curMove > 0) buildHeightmapAsync();
    update();
}
QString Sim3DWidget::resolutionInfo() const
{ int r=m_hmapRes; return QString("%1×%2  —  %3 MB").arg(r).arg(r).arg(float(r)*float(r)*4.f/1024.f/1024.f,0,'f',1); }
void Sim3DWidget::fitAll()
{
    if(!m_stock.valid) return;
    float d=sqrtf(powf(m_stock.xMax-m_stock.xMin,2)+powf(m_stock.yMax-m_stock.yMin,2)+powf(m_stock.zMax-m_stock.zMin,2));
    m_dist=d*(m_ortho ? 1.1f : 2.0f); m_azimuth=225.f; m_elevation=35.f;
    m_pivot={(m_stock.xMin+m_stock.xMax)/2.f,(m_stock.yMin+m_stock.yMax)/2.f,(m_stock.zMin+m_stock.zMax)/2.f};
    rebuildCamera(); update();
}
void Sim3DWidget::viewPreset(ViewPreset v)
{
    switch(v){ case ViewTop:m_azimuth=270.f;m_elevation=89.9f;break;
    case ViewFront:m_azimuth=270.f;m_elevation=0.f;break;
    case ViewLeft:m_azimuth=180.f;m_elevation=0.f;break;
    case ViewRight:m_azimuth=0.f;m_elevation=0.f;break;
    default:m_azimuth=225.f;m_elevation=35.f;break; }
    rebuildCamera(); update();
}
void Sim3DWidget::onAnimTick()
{
    if(m_moves.isEmpty()){pause();return;}
    QElapsedTimer tickT; tickT.start();
    m_rapidCollision = false;
    if (m_speed <= 60) {
        advanceAnimation();
    } else {
        QElapsedTimer t; t.start();
        do { advanceAnimation(); if(!m_playing) break; } while(t.elapsed() < 12);
    }
    m_perfTickNs += tickT.nsecsElapsed(); ++m_perfTicks;
    m_rapidCollisionShown = m_rapidCollision;
    emitAnimStatus(false);
    update();
}
// La fraise est-elle sous la surface de la matière (déplacement rapide
// dans la matière = collision) ? Test au centre et sur 4 points du bord.
bool Sim3DWidget::toolBelowSurface(const QVector3D &p, int toolT)
{
    if (m_hmap.data.empty() || m_hmap.res < 2) return false;
    const ToolInfo ti = resolveToolInfo(toolT);
    const Heightmap &h = m_hmap;
    auto hAt = [&](float x, float y)->float {
        if (x < h.xMin || x > h.xMax || y < h.yMin || y > h.yMax) return -1e30f;
        int i = qBound(0, int((x-h.xMin)/(h.xMax-h.xMin)*(h.res-1)+0.5f), h.res-1);
        int j = qBound(0, int((y-h.yMin)/(h.yMax-h.yMin)*(h.res-1)+0.5f), h.res-1);
        return h.data[size_t(i)*h.res + j];
    };
    const float r = ti.r * 0.7f;
    const float pts[5][2] = {{0,0},{r,0},{-r,0},{0,r},{0,-r}};
    for (auto &q : pts)
        if (p.z() < hAt(p.x()+q[0], p.y()+q[1]) - 0.05f) return true;
    return false;
}

void Sim3DWidget::advanceAnimation()
{
    float rem=m_stepMM;
    while(rem>0.f){
        if(m_curMove>=m_moves.size()){
            emitAnimStatus(true);
            pause(); emit animProgressChanged(100);
            if(!m_hmapRealtime){
                if(m_useOcc && m_stockOpaque)
                    QThreadPool::globalInstance()->start([this]{ applyAllOccCuts(); });
                else
                    buildHeightmapAsync();  // heightmap finale pleine résolution
            } else {
                // Même si realtime : rebuild final à la résolution complète
                // (pendant l'animation on était peut-être en LOD réduit)
                if(m_hmap.res != m_hmapRes) buildHeightmapAsync();
            }
            return;
        }
        const SimMove &mv=m_moves[m_curMove];
        if(mv.pts.size()<2){m_curMove++;m_curSeg=0;m_curSegT=0.f;continue;}
        int sm=mv.pts.size()-2;
        if(m_curSeg>sm){m_curMove++;m_curSeg=0;m_curSegT=0.f;continue;}
        const QVector3D &a=mv.pts[m_curSeg],&b=mv.pts[m_curSeg+1];
        float sl=a.distanceToPoint(b);
        if(sl<1e-6f){m_curSeg++;if(m_curSeg>sm){m_curMove++;m_curSeg=0;}m_curSegT=0.f;continue;}
        float newT=m_curSegT+rem/sl;
        if(newT>=1.f){
            rem-=(1.f-m_curSegT)*sl; m_curSegT=0.f; m_toolPos=b;
            // ── Heightmap incrémentale : creuse pile entre l'ancienne
            //    position et la nouvelle (synchro parfaite outil/matière).
            //    Seulement pour les moves de coupe (Cut), pas les G0.
            if (mv.kind == SimMove::Cut)
                applyToolIncremental(b, mv.toolT);
            else {
                // Mouvement rapide (G0) : on ne creuse pas, mais on
                // met à jour la position de référence pour le prochain
                // segment de coupe.
                m_lastIncrPos = b;
                m_lastIncrValid = true;
                if (toolBelowSurface(b, mv.toolT)) m_rapidCollision = true;
            }
            m_curSeg++; if(m_curSeg>sm){m_curMove++;m_curSeg=0; m_dlDirty=true;}
        } else {
            m_curSegT=newT; m_toolPos=a+(b-a)*newT; rem=0.f;
            // Position intermédiaire : creuse aussi pour suivre l'outil
            // en temps réel pendant les longs segments.
            if (mv.kind == SimMove::Cut)
                applyToolIncremental(m_toolPos, mv.toolT);
            else {
                m_lastIncrPos = m_toolPos;
                m_lastIncrValid = true;
                if (toolBelowSurface(m_toolPos, mv.toolT)) m_rapidCollision = true;
            }
        }
    }
    // (ligne courante et progression : signalées une fois par image
    //  dans onAnimTick, pas à chaque petit pas — voir emitAnimStatus)
}

// Signale la ligne G-code courante et la progression à l'interface,
// au plus ~12 fois par seconde : surligner une ligne dans la liste
// coûte cher, inutile de le faire à chaque pas de calcul.
void Sim3DWidget::emitAnimStatus(bool force)
{
    if(m_moves.isEmpty()) return;
    if(!force && m_uiEmitClock.isValid() && m_uiEmitClock.elapsed()<80) return;
    m_uiEmitClock.start();
    // En fin de programme : dernière ligne (sinon la liste restait figée)
    const int mi = qMin(m_curMove, (int)m_moves.size()-1);
    int li=m_moves[mi].lineIdx;
    // Programme terminé : on va jusqu'à la DERNIÈRE ligne (M30), pas
    // seulement au dernier déplacement (les M5/M30 ne bougent pas l'outil)
    if (m_curMove >= m_moves.size()) li = INT_MAX;
    if(li!=m_lastEmittedLine){m_lastEmittedLine=li;emit currentLineChanged(li);}
    if(m_moves.size()>0) emit animProgressChanged(m_curMove*100/m_moves.size());
    updateTransportUi();
}

// ═══════════════════════════════════════════════════════════════
//  OpenGL
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.07f,0.07f,0.10f,1.f);
    glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    GLfloat amb[]={0.3f,0.3f,0.3f,1.f},dif[]={0.85f,0.85f,0.85f,1.f},lp[]={1.f,1.5f,2.f,0.f};
    glLightfv(GL_LIGHT0,GL_AMBIENT,amb); glLightfv(GL_LIGHT0,GL_DIFFUSE,dif); glLightfv(GL_LIGHT0,GL_POSITION,lp);
}
void Sim3DWidget::resizeGL(int w,int h){ glViewport(0,0,w,h); rebuildProjection(); positionTransportBar(); }
void Sim3DWidget::rebuildProjection()
{
    int w=width(),h=qMax(height(),1); float asp=float(w)/float(h);
    m_proj.setToIdentity();
    if(m_ortho){ float hH=m_dist*0.35f,hW=hH*asp; m_proj.ortho(-hW,hW,-hH,hH,-50000.f,50000.f); }
    else m_proj.perspective(25.f,asp,qMax(0.5f,m_dist*0.01f),m_dist*20.f+10000.f);  // FOV 25° : naturel, sans effet trapèze inversé
}
void Sim3DWidget::rebuildCamera()
{
    float az=qDegreesToRadians(m_azimuth),el=qDegreesToRadians(m_elevation);
    QVector3D eye(m_pivot.x()+m_dist*cosf(el)*cosf(az),m_pivot.y()+m_dist*cosf(el)*sinf(az),m_pivot.z()+m_dist*sinf(el));
    m_view.setToIdentity(); m_view.lookAt(eye,m_pivot,QVector3D(0,0,1)); rebuildProjection();
}

// ── paintGL — bascule OCC/heightmap ───────────────────────────
void Sim3DWidget::paintGL()
{
    QElapsedTimer paintT; paintT.start();
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    rebuildCamera();
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(m_proj.constData());
    glMatrixMode(GL_MODELVIEW);  glLoadMatrixf(m_view.constData());
    drawAxes();
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);

    // ── Affichage brut (bloc + surface usinée) ─────────────────
    // Le bouton "Brut" (m_showStock) contrôle TOUT ce qui représente
    // la matière : le bloc enveloppe ET la heightmap de la surface usinée.
    // L'icône plein/transparent (m_stockOpaque) choisit le style.
    if(m_showStock){
        // ── Pipeline unique : brut d'abord, matière (heightmap) par-dessus ──
        // Brut transparent ou opaque selon m_stockAlpha (slider − Brut +).
        // Matière TOUJOURS opaque → on la voit nettement à toute opacité
        // du brut, et en mode 100% elle occupe l'espace au-dessus du brut.
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
        drawStock();
        if(!m_gpuTried) initHeightmapGPU();
        if(m_gpuOk){
            // Nouveau rendu : texture GPU + éclairage au pixel
            drawHeightmapGPU();
        } else {
            // Ancien rendu (secours si les shaders ne compilent pas)
            drawHeightmap();
            // Wireframe noir DERNIER : toujours visible par-dessus tout
            drawHeightmapWireframe();
        }
    }
    // Si m_showStock=false → ni bloc ni heightmap : on voit seulement
    // le parcours outil et la fraise en mouvement

    { QElapsedTimer pt; pt.start();
      if(m_showPath)  drawToolPath();
      m_perfPathNs += pt.nsecsElapsed(); }
    if(m_showTool)  drawTool();
    if(!m_faceRegion.isEmpty()) drawFaceHighlight();
    if(m_measureMode||!m_measurePts.isEmpty()) drawMeasurePoints();
    if(m_measureMode && m_hoverValid) drawHoverProbe();

    // ── Chronométrage : résumé une fois par seconde pendant la simu ──
    glFinish();                       // attendre la carte graphique (mesure réelle)
    m_perfPaintNs += paintT.nsecsElapsed();
    ++m_perfFrames;
    if(!m_perfClock.isValid()) m_perfClock.start();
    if(m_perfClock.elapsed() >= 1000){
        if(m_playing)
            qDebug().noquote() << QString("[SIM] %1 img/s | image totale %2 ms/s | calcul anim %3 ms/s (%4 ticks) | dont coupe %5 | parcours %6 | envoi GPU %7 | matiere %8 | grille %9")
                                  .arg(m_perfFrames)
                                  .arg(m_perfPaintNs/1e6,0,'f',1)
                                  .arg(m_perfTickNs/1e6,0,'f',1)
                                  .arg(m_perfTicks)
                                  .arg(m_perfCutNs/1e6,0,'f',1)
                                  .arg(m_perfPathNs/1e6,0,'f',1)
                                  .arg(m_perfUploadNs/1e6,0,'f',1)
                                  .arg(m_perfDrawNs/1e6,0,'f',1)
                                  .arg(m_hmap.res);
        m_perfFrames=0; m_perfCutNs=0; m_perfUploadNs=0; m_perfDrawNs=0;
        m_perfPaintNs=0; m_perfTickNs=0; m_perfPathNs=0; m_perfTicks=0;
        m_perfClock.restart();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Rendu GPU de la heightmap
// ═══════════════════════════════════════════════════════════════
//  Grille : (G+2)×(G+2) sommets. Les indices 0 et G+1 forment une
//  « jupe » qui descend jusqu'au fond du brut → parois latérales.
//  Chaque sommet ne porte que ses indices (gi,gj) ; sa hauteur est
//  lue dans la texture par le vertex shader.
static const char *kHmVert = R"GLSL(
#version 130
in vec2 aGrid;
uniform mat4  uMVP;
uniform mat4  uView;
uniform vec4  uBounds;   // xMin, xMax, yMin, yMax
uniform float uZBot;
uniform float uG;        // nb de nœuds de la grille par côté
uniform float uRes;      // nb de cases de la texture par côté
uniform vec2  uCell;     // taille d'une case en X, Y (mm)
uniform sampler2D uHeight;
uniform float uNoSkirt;  // 1 = loupe : pas de parois de bord
out vec3  vWorld;
out vec3  vViewPos;
out vec2  vTC;
out float vSkirt;
void main(){
    bool skirt = (uNoSkirt < 0.5) &&
                 (aGrid.x < 0.5 || aGrid.x > uG + 0.5 ||
                  aGrid.y < 0.5 || aGrid.y > uG + 0.5);
    float fi = clamp(aGrid.x - 1.0, 0.0, uG - 1.0) / (uG - 1.0);   // 0..1 en X
    float fj = clamp(aGrid.y - 1.0, 0.0, uG - 1.0) / (uG - 1.0);   // 0..1 en Y
    // Texture : s = Y (j), t = X (i) — centre des cases
    vec2 tc = vec2((fj*(uRes-1.0)+0.5)/uRes, (fi*(uRes-1.0)+0.5)/uRes);
    float h = textureLod(uHeight, tc, 0.0).r;
    vec3 w = vec3(mix(uBounds.x, uBounds.y, fi),
                  mix(uBounds.z, uBounds.w, fj),
                  skirt ? uZBot : h);

    // ── Parois nettes (méthode VerdierCAM) ──────────────────────
    //  Un sommet à moitié coupé au bord d'une paroi raide n'est pas
    //  descendu à mi-hauteur (ça fait des dents sur le bord) : il reste
    //  en haut ou en bas, et glisse à l'HORIZONTALE jusqu'au vrai bord.
    if (!skirt) {
        float t = 1.0 / uRes;
        float hxp = textureLod(uHeight, tc + vec2(0.0,  t), 0.0).r;
        float hxm = textureLod(uHeight, tc + vec2(0.0, -t), 0.0).r;
        float hyp = textureLod(uHeight, tc + vec2( t, 0.0), 0.0).r;
        float hym = textureLod(uHeight, tc + vec2(-t, 0.0), 0.0).r;
        float d1  = textureLod(uHeight, tc + vec2( t,  t), 0.0).r;
        float d2  = textureLod(uHeight, tc + vec2(-t,  t), 0.0).r;
        float d3  = textureLod(uHeight, tc + vec2( t, -t), 0.0).r;
        float d4  = textureLod(uHeight, tc + vec2(-t, -t), 0.0).r;
        float hmax = max(max(max(hxp, hxm), max(hyp, hym)), max(max(d1, d2), max(d3, d4)));
        float hmin = min(min(min(hxp, hxm), min(hyp, hym)), min(min(d1, d2), min(d3, d4)));
        hmax = max(hmax, h);  hmin = min(hmin, h);
        float span = hmax - hmin;
        float cellLen = 0.5 * (uCell.x + uCell.y);
        if (span > 3.0 * cellLen) {
            float f = (hmax - h) / span;            // part coupée de la case
            if (f > 0.02 && f < 0.98) {
                // Direction de glissement : gradient lissé (Sobel sur 2 cases)
                // au lieu de la différence d'une case, qui hésite entre X et
                // Y sur les parois courbes -> paroi en dents de scie
                // (effet crémaillère vertical).
                float u2 = 2.0 * t;
                float b00 = textureLod(uHeight, tc + vec2(-u2, -u2), 0.0).r;
                float b10 = textureLod(uHeight, tc + vec2(-u2, 0.0), 0.0).r;
                float b20 = textureLod(uHeight, tc + vec2(-u2,  u2), 0.0).r;
                float b01 = textureLod(uHeight, tc + vec2(0.0, -u2), 0.0).r;
                float b21 = textureLod(uHeight, tc + vec2(0.0,  u2), 0.0).r;
                float b02 = textureLod(uHeight, tc + vec2( u2, -u2), 0.0).r;
                float b12 = textureLod(uHeight, tc + vec2( u2, 0.0), 0.0).r;
                float b22 = textureLod(uHeight, tc + vec2( u2,  u2), 0.0).r;
                vec2 g = vec2(((b20 + 2.0*b21 + b22) - (b00 + 2.0*b01 + b02)) / uCell.x,
                              ((b02 + 2.0*b12 + b22) - (b00 + 2.0*b10 + b20)) / uCell.y);
                g += 4.0 * vec2((hxp - hxm) / uCell.x, (hyp - hym) / uCell.y);
                float gl = length(g);
                if (gl > 1e-6) {
                    vec2 up = g / gl;                // vers le haut de la paroi
                    w.xy += up * (f - 0.5) * cellLen;
                    w.z   = (f < 0.5) ? hmax : hmin;
                }
            }
        }
    }
    vWorld   = w;
    vViewPos = (uView * vec4(w, 1.0)).xyz;
    vTC      = tc;
    vSkirt   = skirt ? 1.0 : 0.0;
    gl_Position = uMVP * vec4(w, 1.0);
}
)GLSL";

static const char *kHmFrag = R"GLSL(
#version 130
in vec3  vWorld;
in vec3  vViewPos;
in vec2  vTC;
in float vSkirt;
uniform sampler2D uHeight;
uniform mat4  uView;
uniform float uRes;
uniform vec2  uCell;     // taille d'une case en X, Y (mm)
uniform float uZTop;
uniform vec3  uColor;
uniform float uAlpha;
uniform float uOrtho;
uniform isampler2D uTool;   // numéro d'outil qui a usiné chaque case
uniform vec4  uHole;        // zone XY dessinée par la loupe (à ne pas dessiner ici)
out vec4 fragColor;

// Teinte par outil, mélangée à l'aluminium (T1 bleu, T2 vert, T3 orange...)
vec3 toolTint(int t) {
    int k = (t - 1) - 8 * ((t - 1) / 8);
    if (k == 0) return vec3(0.35, 0.55, 0.90);
    if (k == 1) return vec3(0.40, 0.78, 0.45);
    if (k == 2) return vec3(0.95, 0.60, 0.25);
    if (k == 3) return vec3(0.68, 0.45, 0.88);
    if (k == 4) return vec3(0.30, 0.78, 0.82);
    if (k == 5) return vec3(0.92, 0.82, 0.30);
    if (k == 6) return vec3(0.88, 0.40, 0.45);
    return vec3(0.50, 0.68, 0.55);
}
// Gradient de hauteur (Sobel) avec un pas d (en coordonnées texture)
vec2 sobelDir(vec2 tc, float d) {
    float a00 = texture(uHeight, tc + vec2(-d, -d)).r;
    float a10 = texture(uHeight, tc + vec2(-d, 0.0)).r;
    float a20 = texture(uHeight, tc + vec2(-d,  d)).r;
    float a01 = texture(uHeight, tc + vec2(0.0, -d)).r;
    float a21 = texture(uHeight, tc + vec2(0.0,  d)).r;
    float a02 = texture(uHeight, tc + vec2( d, -d)).r;
    float a12 = texture(uHeight, tc + vec2( d, 0.0)).r;
    float a22 = texture(uHeight, tc + vec2( d,  d)).r;
    // même convention que H(ix,iy) = tc + (iy*d, ix*d)
    vec2 g = vec2((a20 + 2.0*a21 + a22) - (a00 + 2.0*a01 + a02),
                  (a02 + 2.0*a12 + a22) - (a00 + 2.0*a10 + a20));
    return g / uCell;
}
void main(){
    if (vWorld.x > uHole.x && vWorld.x < uHole.y && vWorld.y > uHole.z && vWorld.y < uHole.w) discard;
    vec3 nW;
    bool top = (vSkirt < 0.0001);
    ivec2 cell = ivec2(clamp(vTC * uRes, vec2(0.0), vec2(uRes - 1.0)));
    ivec2 maxC = ivec2(int(uRes) - 1);
    // Hauteurs min / max des cases autour du point (3x3, sans filtrage).
    // C'est la HAUTEUR du point qui dit s'il est sur une paroi (entre le
    // haut et le bas) ou sur une face à plat (dessus, fond) — pas l'angle
    // des triangles, qui alterne d'une colonne à l'autre sur une paroi
    // courbe et donnait l'effet crémaillère.
    float lo3 = 1e30, hi3 = -1e30;
    if (top) {
        // 5x5 : le point d'une paroi peut tomber sur une case du bord dont
        // les voisines immédiates n'atteignent pas encore le fond (case à
        // moitié coupée) -> il était pris pour un fond et peint en noir
        // (fines rayures verticales). Rayon 2 = sûr d'encadrer la paroi.
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                float hc = texelFetch(uHeight, clamp(cell + ivec2(dx, dy), ivec2(0), maxC), 0).r;
                lo3 = min(lo3, hc); hi3 = max(hi3, hc);
            }
    }
    bool nearWall = top && (hi3 - lo3 > 0.08);
    bool isWall   = nearWall && vWorld.z < hi3 - 0.02 && vWorld.z > lo3 + 0.02;
    bool steepWall = false;   // paroi quasi verticale (calculé plus bas)
    if (top) {
        // Normale au pixel : filtre de Sobel 3x3 sur la carte de hauteurs
        const float k = 1.5;
        float d = k / uRes;
        #define H(ix,iy) texture(uHeight, vTC + vec2(float(iy)*d, float(ix)*d)).r
        float h00 = H(-1,-1), h10 = H(0,-1), h20 = H(1,-1);
        float h01 = H(-1, 0),                h21 = H(1, 0);
        float h02 = H(-1, 1), h12 = H(0, 1), h22 = H(1, 1);
        #undef H
        float gx = (h20 + 2.0*h21 + h22) - (h00 + 2.0*h01 + h02);   // le long de X
        float gy = (h02 + 2.0*h12 + h22) - (h00 + 2.0*h10 + h20);   // le long de Y
        float dzdx = gx / (8.0 * k * uCell.x);
        float dzdy = gy / (8.0 * k * uCell.y);
        nW = normalize(vec3(-dzdx, -dzdy, 1.0));
        // Pente locale : une paroi d'usinage (fraise droite) est presque
        // verticale ; une forme d'outil (boule, chanfrein) donne des pentes
        // douces qui doivent garder leur vraie normale (sinon le fond
        // arrondi d'une gorge paraît plat / en marches).
        float slopeG = length(vec2(dzdx, dzdy));
        steepWall = isWall && slopeG > 2.5;
        if (isWall) {
            vec3 sobelN = nW;
            // Paroi : verticale, direction moyennée sur 3 échelles (4, 8,
            // 12 cases = 0,25 à 0,7 mm) -> lisse sur les parois courbes
            vec2 acc = vec2(0.0);
            // Astuce VerdierCAM : quand une case devient plus petite qu'un
            // pixel, le filtre s'élargit avec le pixel (fwidth) -> le détail
            // se fond en moyenne au lieu de faire du moiré (rayures de loin)
            vec2  fw   = fwidth(vTC);
            float pxTC = max(fw.x, fw.y);
            // Centre « déphasé » : on moyenne aussi 4 points décalés d'une
            // demi-case, pour que la direction ne dépende plus de la place
            // du pixel dans sa case (sinon elle ondule de colonne en colonne)
            float hcell = 0.5 / uRes;
            vec2 s4  = sobelDir(vTC, max( 4.0 / uRes, 1.5 * pxTC));
            vec2 s8  = sobelDir(vTC, max( 8.0 / uRes, 3.0 * pxTC));
            vec2 s12 = sobelDir(vTC, max(12.0 / uRes, 4.5 * pxTC));
            s8 += sobelDir(vTC + vec2( hcell,  hcell), max(8.0 / uRes, 3.0 * pxTC))
                + sobelDir(vTC + vec2(-hcell,  hcell), max(8.0 / uRes, 3.0 * pxTC))
                + sobelDir(vTC + vec2( hcell, -hcell), max(8.0 / uRes, 3.0 * pxTC))
                + sobelDir(vTC + vec2(-hcell, -hcell), max(8.0 / uRes, 3.0 * pxTC));
            if (length(s4)  > 1e-6) acc += normalize(s4);
            if (length(s8)  > 1e-6) acc += normalize(s8);
            if (length(s12) > 1e-6) acc += normalize(s12);
            if (length(acc) < 1e-6) acc = vec2(dzdx, dzdy);
            if (length(acc) > 1e-6)
                nW = normalize(mix(sobelN, vec3(-normalize(acc), 0.0),
                                   smoothstep(1.8, 3.5, slopeG)));
        } else if (nearWall) {
            // Dessus ou fond juste au bord d'une paroi : face à plat
            nW = vec3(0.0, 0.0, 1.0);
        }
    } else {
        // Parois extérieures : normale géométrique
        nW = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    }
    vec3 n = normalize(mat3(uView) * nW);
    vec3 V = (uOrtho > 0.5) ? vec3(0.0, 0.0, 1.0) : normalize(-vViewPos);
    if (dot(n, V) < 0.0) n = -n;                 // éclairage deux faces

    // Lumières attachées à la caméra : principale en biais + débouchage
    vec3 L1 = normalize(vec3(-0.35, 0.55, 0.75));
    vec3 L2 = normalize(vec3( 0.60,-0.30, 0.40));
    float dif  = 0.72 * max(dot(n, L1), 0.0) + 0.25 * max(dot(n, L2), 0.0);
    float hemi = 0.5 + 0.5 * abs(nW.z);
    float amb  = mix(0.16, 0.30, hemi);
    vec3  H    = normalize(L1 + V);
    float spec = pow(max(dot(n, H), 0.0), 48.0) * 0.30;
    // Paroi : reflet très doux. Un reflet serré (puissance 48) grossit le
    // moindre écart de normale et dessine des rayures verticales.
    if (steepWall) spec = pow(max(dot(n, H), 0.0), 8.0) * 0.06;

    vec3 base = uColor;
    // Outil : parmi les 4 cases autour du point, celle dont la hauteur est
    // la plus proche de celle du point. Le bord de couleur suit alors la
    // forme réelle (cercle, chanfrein) au lieu de faire des marches de case.
    int tNum = texelFetch(uTool, cell, 0).r;
    if (top) {
        ivec2 c0 = ivec2(floor(vTC * uRes - 0.5));
        float best = 1e30; int tBest = tNum; bool anyRaw = false;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                ivec2 c = clamp(c0 + ivec2(i, j), ivec2(0), maxC);
                int   t = texelFetch(uTool, c, 0).r;
                float d = abs(texelFetch(uHeight, c, 0).r - vWorld.z);
                if (t == 0) anyRaw = true;
                if (d < best) { best = d; tBest = t; }
            }
        tNum = tBest;
        // Bord du dessus brut : c'est la hauteur qui décide (trait net)
        if (anyRaw && vWorld.z >= uZTop - 0.004) tNum = 0;
    }
    // Parois des usinages : le pixel tombe souvent sur la case NON usinée
    // du bord (la paroi relie le haut au fond). S'il est sous le dessus du
    // brut, on prend l'outil de la case voisine la plus basse (rayon 2).
    // Paroi : la couleur est celle de l'outil qui a creusé JUSTE SOUS ce
    // point (la case voisine la plus haute parmi celles plus basses que
    // lui). Avant, on prenait l'outil de la case du bord : quand une
    // gorge passe sur le bord d'une poche, toute la paroi de la poche
    // en dessous prenait la couleur de la gorge.
    if (top && (isWall || (tNum == 0 && vWorld.z < uZTop - 0.01))) {
        float hBelow = -1e30; int tBelow = 0;
        float hLow = 1e30;    int tLow = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                ivec2 c = clamp(cell + ivec2(dx, dy), ivec2(0), maxC);
                int t = texelFetch(uTool, c, 0).r;
                if (t > 0) {
                    float hc = texelFetch(uHeight, c, 0).r;
                    if (hc < vWorld.z - 0.01 && hc > hBelow) { hBelow = hc; tBelow = t; }
                    if (hc < hLow) { hLow = hc; tLow = t; }
                }
            }
        if (tBelow > 0)      tNum = tBelow;
        else if (tNum == 0)  tNum = tLow;
    }
    // surface usinée (en mode transparent : couleur plus franche, sinon
    // elle se noie dans le gris du brut -> effet de fumée)
    if (tNum > 0) base = mix(uColor, toolTint(tNum), (uAlpha < 0.999) ? 0.85 : 0.55);
    if (top) {
        // Dessus brut non usiné : plus mat. On teste la hauteur RÉELLE du
        // point (vWorld.z) et pas la texture : sur une paroi, la texture
        // lissée donnait « brut » une colonne sur deux -> crémaillère.
        if (vWorld.z >= uZTop - 0.001 && tNum == 0) { base *= 0.93; spec *= 0.5; }
    }
    vec3 col = base * (amb + dif) + vec3(spec);
    // ── Arêtes noires (haut et bas des parois usinées) ──────────────
    // On regarde la hauteur à ~1,5 pixel écran autour du point : si le
    // point est le plus haut (ou le plus bas) et que ça chute (monte)
    // brusquement à côté, il est sur une arête -> trait foncé d'épaisseur
    // constante à l'écran, quel que soit le zoom. Les pixels de la paroi
    // elle-même (entre haut et bas) ne sont pas touchés.
    float edge = 0.0;
    if (top && !isWall) {
        // Face à plat : on regarde la hauteur à ~1 pixel écran autour ;
        // si le point est le plus haut (ou le plus bas) et que ça chute
        // (monte) brusquement à côté -> arête, trait d'épaisseur constante
        // Rayon d'analyse : 1 pixel, mais jamais moins de 0,6 case -> en zoom
        // serré le trait reste continu (avant : pointillé entre les cases)
        vec2  px = max(fwidth(vTC), vec2(0.6 / uRes));
        float h0 = vWorld.z;
        float n1 = texture(uHeight, vTC + vec2( px.x, 0.0)).r;
        float n2 = texture(uHeight, vTC + vec2(-px.x, 0.0)).r;
        float n3 = texture(uHeight, vTC + vec2(0.0,  px.y)).r;
        float n4 = texture(uHeight, vTC + vec2(0.0, -px.y)).r;
        float n5 = texture(uHeight, vTC + px).r;
        float n6 = texture(uHeight, vTC - px).r;
        float n7 = texture(uHeight, vTC + vec2(px.x, -px.y)).r;
        float n8 = texture(uHeight, vTC + vec2(-px.x, px.y)).r;
        float lo = min(min(min(n1, n2), min(n3, n4)), min(min(n5, n6), min(n7, n8)));
        float hi = max(max(max(n1, n2), max(n3, n4)), max(max(n5, n6), max(n7, n8)));
        float wMM = max(px.x * uCell.x * uRes, px.y * uCell.y * uRes);
        float thr = max(0.08, 2.2 * wMM);
        const float eps = 0.04;
        if (h0 >= hi - eps && h0 - lo > thr) edge = 1.0;          // arête du haut
        else if (h0 <= lo + eps && hi - h0 > thr) edge = 1.0;     // arête du bas
        // Cassure de pente sans marche (dessus -> chanfrein 45°, chanfrein
        // -> fond...) : la pente change d'un coup -> trait aussi.
        // Sur une forme courbe (boule, congé) la pente varie doucement : pas de trait.
        if (edge < 0.5) {
            float hc0 = texture(uHeight, vTC).r;
            float d2  = max(abs(n1 + n2 - 2.0 * hc0), abs(n3 + n4 - 2.0 * hc0));
            float dd  = max(abs(n5 + n6 - 2.0 * hc0), abs(n7 + n8 - 2.0 * hc0)) * 0.5;
            if (max(d2, dd) > max(0.55 * wMM, 0.003)) edge = 1.0;
        }
    }
    if (steepWall) {
        // Sur la paroi : trait en tête et au pied (~1 pixel de haut)
        float zw = min(max(fwidth(vWorld.z), 1e-4), 0.15 * (hi3 - lo3));
        if (vWorld.z - lo3 < zw || hi3 - vWorld.z < zw) edge = 1.0;
    }
    col = mix(col, vec3(0.06, 0.06, 0.07), edge * 0.75);
    // Mode transparent : le brut s'efface, les usinages restent bien visibles
    float aOut = uAlpha;
    if (uAlpha < 0.999) aOut = (tNum > 0) ? max(uAlpha, 0.88) : min(uAlpha, 0.30);
    fragColor = vec4(col, max(aOut, edge * 0.9));
}
)GLSL";

bool Sim3DWidget::initHeightmapGPU()
{
    m_gpuTried = true;
    m_gpuOk = false;
    delete m_hmProg;
    m_hmProg = new QOpenGLShaderProgram();
    bool ok = m_hmProg->addShaderFromSourceCode(QOpenGLShader::Vertex,   kHmVert)
           && m_hmProg->addShaderFromSourceCode(QOpenGLShader::Fragment, kHmFrag);
    if (ok) {
        m_hmProg->bindAttributeLocation("aGrid", 0);
        ok = m_hmProg->link();
    }
    if (!ok) {
        qWarning() << "[SIM] Shaders heightmap indisponibles, ancien rendu utilisé :"
                   << m_hmProg->log();
        delete m_hmProg; m_hmProg = nullptr;
        return false;
    }
    m_gpuOk = true;
    qDebug() << "[SIM] Rendu GPU de la heightmap actif";
    return true;
}

// Appelé sous m_hmapMutex : envoie à la carte graphique uniquement la
// zone modifiée depuis la dernière image (ou tout, si la taille a changé).
void Sim3DWidget::uploadHeightmapTexture()
{
    const int R = m_hmap.res;
    if (!m_hmTex) glGenTextures(1, &m_hmTex);
    if (!m_hmToolTex) glGenTextures(1, &m_hmToolTex);
    const bool toolOk = (int(m_hmap.toolMap.size()) >= R*R);
    // Texture des numéros d'outil (entiers, sans filtrage)
    glBindTexture(GL_TEXTURE_2D, m_hmToolTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (m_hmTexRes != R) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, R, R, 0, GL_RED_INTEGER, GL_INT,
                     toolOk ? (const void*)m_hmap.toolMap.data() : nullptr);
    } else if (m_hmap.hasDirty() && toolOk) {
        const int i0 = m_hmap.dI0, i1 = m_hmap.dI1, j0 = m_hmap.dJ0, j1 = m_hmap.dJ1;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, R);
        glTexSubImage2D(GL_TEXTURE_2D, 0, j0, i0, j1-j0+1, i1-i0+1,
                        GL_RED_INTEGER, GL_INT, &m_hmap.toolMap[size_t(i0)*R + j0]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    glBindTexture(GL_TEXTURE_2D, m_hmTex);
    if (m_hmTexRes != R) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, R, R, 0, GL_RED, GL_FLOAT, m_hmap.data.data());
        m_hmTexRes = R;
        m_hmap.clearDirty();
    } else if (m_hmap.hasDirty()) {
        const int i0 = m_hmap.dI0, i1 = m_hmap.dI1, j0 = m_hmap.dJ0, j1 = m_hmap.dJ1;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, R);
        glTexSubImage2D(GL_TEXTURE_2D, 0, j0, i0, j1-j0+1, i1-i0+1,
                        GL_RED, GL_FLOAT, &m_hmap.data[size_t(i0)*R + j0]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        m_hmap.clearDirty();
    }
}

// Grille fixe (G+2)² + deux jeux d'indices : complet, et allégé (1 nœud
// sur 2) utilisé pendant la rotation à la souris.
void Sim3DWidget::buildHeightmapMesh(int G)
{
    const int M = G + 2;
    std::vector<float> v; v.reserve(size_t(M)*M*2);
    for (int a = 0; a < M; ++a)
        for (int b = 0; b < M; ++b) { v.push_back(float(a)); v.push_back(float(b)); }

    auto buildIdx = [&](int step, std::vector<GLuint> &idx){
        std::vector<int> L;
        L.push_back(0);
        for (int k = 1; k <= G; k += step) L.push_back(k);
        if (L.back() != G) L.push_back(G);
        L.push_back(G + 1);
        const int n = int(L.size());
        idx.clear(); idx.reserve(size_t(n-1)*(n-1)*6);
        for (int ia = 0; ia < n-1; ++ia)
            for (int ib = 0; ib < n-1; ++ib) {
                const GLuint p00 = GLuint(L[ia]*M   + L[ib]);
                const GLuint p10 = GLuint(L[ia+1]*M + L[ib]);
                const GLuint p01 = GLuint(L[ia]*M   + L[ib+1]);
                const GLuint p11 = GLuint(L[ia+1]*M + L[ib+1]);
                idx.push_back(p00); idx.push_back(p10); idx.push_back(p11);
                idx.push_back(p00); idx.push_back(p11); idx.push_back(p01);
            }
    };
    std::vector<GLuint> full, lod, lod4;
    buildIdx(1, full);
    buildIdx(2, lod);
    buildIdx(4, lod4);

    if (!m_hmMeshVBO) glGenBuffers(1, &m_hmMeshVBO);
    if (!m_hmIdxFull) glGenBuffers(1, &m_hmIdxFull);
    if (!m_hmIdxLod)  glGenBuffers(1, &m_hmIdxLod);
    if (!m_hmIdxLod4) glGenBuffers(1, &m_hmIdxLod4);
    glBindBuffer(GL_ARRAY_BUFFER, m_hmMeshVBO);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size()*sizeof(float)), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hmIdxFull);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(full.size()*sizeof(GLuint)), full.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hmIdxLod);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(lod.size()*sizeof(GLuint)), lod.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hmIdxLod4);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(lod4.size()*sizeof(GLuint)), lod4.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    m_hmIdxLod4Count = int(lod4.size());
    m_hmIdxFullCount = int(full.size());
    m_hmIdxLodCount  = int(lod.size());
    m_hmMeshG = G;
}

void Sim3DWidget::drawHeightmapGPU()
{
    if (!m_hmProg) return;
    int R; float xMin,xMax,yMin,yMax,zTop,zBot;
    {
        QMutexLocker lk(&m_hmapMutex);
        R = m_hmap.res;
        if (R < 2 || int(m_hmap.data.size()) < R*R) return;
        QElapsedTimer up; up.start();
        uploadHeightmapTexture();
        m_perfUploadNs += up.nsecsElapsed();
        xMin=m_hmap.xMin; xMax=m_hmap.xMax; yMin=m_hmap.yMin; yMax=m_hmap.yMax;
        zTop=m_hmap.zTop; zBot=m_hmap.zBot;
    }
    QElapsedTimer dt; dt.start();

    // Grille de sommets aussi fine que la carte de hauteurs (jusqu'à 1600
    // nœuds par côté) : chaque sommet tombe pile sur une case, pas entre
    // deux (sinon facettes et stries sur les parois)
    // Maillage toujours à la résolution fine (pas de reconstruction quand
    // on bascule entre grille d'animation et grille fine)
    const int G = qMin(qMax(R, m_hmapRes), 3072);
    if (G != m_hmMeshG) buildHeightmapMesh(G);
    // Densité de la grille à l'écran : si plusieurs cases tombent dans un
    // même pixel, la grille fine « crénelle » (stries verticales sur les
    // parois vues de loin). On prend alors 1 nœud sur 2 ou sur 4.
    const float cellMM   = (xMax - xMin) / float(qMax(1, G - 1));
    const float mmPerPx  = (m_ortho ? 0.70f : 0.443f) * m_dist / float(qMax(1, height()));
    const float cellsPx  = mmPerPx / qMax(1e-6f, cellMM);   // cases par pixel
    // (essai : maillage allégé de loin -> parois cassées, abandonné)
    Q_UNUSED(cellsPx);
    int step = (m_orbiting || m_panning) ? 2 : 1;
    const GLuint ibo = (step == 4) ? m_hmIdxLod4 : (step == 2) ? m_hmIdxLod : m_hmIdxFull;
    const int    cnt = (step == 4) ? m_hmIdxLod4Count : (step == 2) ? m_hmIdxLodCount : m_hmIdxFullCount;

    // Même règle d'opacité que l'ancien rendu (curseur « Brut »)
    const float aEff  = (m_stockAlpha >= 0.974f) ? 1.0f : qMax(0.05f, m_stockAlpha);
    const bool  blend = (aEff < 0.999f);
    glDisable(GL_LIGHTING);
    if (blend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
    else       { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }

    m_hmProg->bind();
    m_hmProg->setUniformValue("uMVP",   m_proj * m_view);
    m_hmProg->setUniformValue("uView",  m_view);
    m_hmProg->setUniformValue("uBounds", QVector4D(xMin, xMax, yMin, yMax));
    m_hmProg->setUniformValue("uZBot",  zBot);
    m_hmProg->setUniformValue("uZTop",  zTop);
    m_hmProg->setUniformValue("uG",     float(G));
    m_hmProg->setUniformValue("uRes",   float(R));
    m_hmProg->setUniformValue("uCell",  QVector2D((xMax-xMin)/(R-1), (yMax-yMin)/(R-1)));
    m_hmProg->setUniformValue("uColor", QVector3D(0.72f, 0.74f, 0.77f));   // aluminium
    m_hmProg->setUniformValue("uAlpha", aEff);
    m_hmProg->setUniformValue("uOrtho", m_ortho ? 1.0f : 0.0f);
    m_hmProg->setUniformValue("uHeight", 0);
    m_hmProg->setUniformValue("uTool", 1);
    m_hmProg->setUniformValue("uNoSkirt", 0.0f);
    // Loupe active : la pièce globale laisse un trou que la loupe remplit
    const bool loupeOn = m_lpValid && m_lpVerBuilt == m_matVer.load()
                         && !m_playing && m_rwMul == 0 && !m_sliderDrag;
    if (loupeOn)
        m_hmProg->setUniformValue("uHole", QVector4D(float(m_lpRegion.left()), float(m_lpRegion.right()),
                                                     float(m_lpRegion.top()),  float(m_lpRegion.bottom())));
    else
        m_hmProg->setUniformValue("uHole", QVector4D(1.f, -1.f, 1.f, -1.f));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_hmToolTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hmTex);
    glBindBuffer(GL_ARRAY_BUFFER, m_hmMeshVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glDrawElements(GL_TRIANGLES, cnt, GL_UNSIGNED_INT, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // ── Loupe : la zone visible en grille fine ────────────────────
    if (loupeOn) {
        int LR = 0; float lx0=0, lx1=0, ly0=0, ly1=0;
        {
            QMutexLocker lk(&m_hmapMutex);
            LR = m_loupe.res;
            if (LR >= 2 && int(m_loupe.data.size()) >= LR*LR) {
                if (!m_lpTex)     glGenTextures(1, &m_lpTex);
                if (!m_lpToolTex) glGenTextures(1, &m_lpToolTex);
                if (!m_lpUploaded) {
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    glBindTexture(GL_TEXTURE_2D, m_lpToolTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, LR, LR, 0, GL_RED_INTEGER, GL_INT,
                                 (int(m_loupe.toolMap.size()) >= LR*LR) ? (const void*)m_loupe.toolMap.data() : nullptr);
                    glBindTexture(GL_TEXTURE_2D, m_lpTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, LR, LR, 0, GL_RED, GL_FLOAT, m_loupe.data.data());
                    m_lpUploaded = true;
                }
                lx0 = m_loupe.xMin; lx1 = m_loupe.xMax; ly0 = m_loupe.yMin; ly1 = m_loupe.yMax;
            } else LR = 0;
        }
        if (LR >= 2) {
            if (m_lpMeshG != LR) {              // maillage : 1 sommet par case de la loupe
                const int M = LR + 2;
                std::vector<float> v; v.reserve(size_t(M)*M*2);
                for (int a = 0; a < M; ++a) for (int b = 0; b < M; ++b) { v.push_back(float(a)); v.push_back(float(b)); }
                std::vector<GLuint> idx; idx.reserve(size_t(M-1)*(M-1)*6);
                for (int a = 0; a < M-1; ++a)
                    for (int b = 0; b < M-1; ++b) {
                        const GLuint p00 = GLuint(a*M + b), p10 = GLuint((a+1)*M + b);
                        const GLuint p01 = GLuint(a*M + b + 1), p11 = GLuint((a+1)*M + b + 1);
                        idx.push_back(p00); idx.push_back(p10); idx.push_back(p11);
                        idx.push_back(p00); idx.push_back(p11); idx.push_back(p01);
                    }
                if (!m_lpVBO) glGenBuffers(1, &m_lpVBO);
                if (!m_lpIBO) glGenBuffers(1, &m_lpIBO);
                glBindBuffer(GL_ARRAY_BUFFER, m_lpVBO);
                glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size()*sizeof(float)), v.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lpIBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size()*sizeof(GLuint)), idx.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                m_lpMeshG = LR; m_lpIdxCount = int(idx.size());
            }
            m_hmProg->setUniformValue("uBounds", QVector4D(lx0, lx1, ly0, ly1));
            m_hmProg->setUniformValue("uG",     float(LR));
            m_hmProg->setUniformValue("uRes",   float(LR));
            m_hmProg->setUniformValue("uCell",  QVector2D((lx1-lx0)/(LR-1), (ly1-ly0)/(LR-1)));
            m_hmProg->setUniformValue("uNoSkirt", 1.0f);
            m_hmProg->setUniformValue("uHole", QVector4D(1.f, -1.f, 1.f, -1.f));
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_lpToolTex);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_lpTex);
            glBindBuffer(GL_ARRAY_BUFFER, m_lpVBO);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lpIBO);
            glDrawElements(GL_TRIANGLES, m_lpIdxCount, GL_UNSIGNED_INT, nullptr);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glDisableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_hmProg->release();

    // ── Arêtes noires du brut (contour extérieur) ──────────────────
    // Le haut suit la carte de hauteurs (si on usine le bord, le trait
    // descend avec la matière). Tracé légèrement devant la surface.
    {
        QMutexLocker lk(&m_hmapMutex);
        const int Rr = m_hmap.res;
        if (Rr >= 2 && int(m_hmap.data.size()) >= Rr*Rr) {
            auto hz = [&](int i, int j){ return m_hmap.data[size_t(i)*Rr + j]; };
            auto px = [&](int i){ return xMin + (xMax-xMin)*float(i)/float(Rr-1); };
            auto py = [&](int j){ return yMin + (yMax-yMin)*float(j)/float(Rr-1); };
            const int step = qMax(1, Rr / 512);
            glDepthRange(0.0, 0.99995);
            glDepthMask(GL_FALSE);
            glEnable(GL_LINE_SMOOTH);
            glLineWidth(1.4f);
            glColor4f(0.03f, 0.03f, 0.04f, 0.95f);
            // 4 bords du dessus
            glBegin(GL_LINE_STRIP);                       // Y min
            for (int i = 0; i < Rr; i += step) glVertex3f(px(i), yMin, hz(i, 0));
            glVertex3f(xMax, yMin, hz(Rr-1, 0)); glEnd();
            glBegin(GL_LINE_STRIP);                       // Y max
            for (int i = 0; i < Rr; i += step) glVertex3f(px(i), yMax, hz(i, Rr-1));
            glVertex3f(xMax, yMax, hz(Rr-1, Rr-1)); glEnd();
            glBegin(GL_LINE_STRIP);                       // X min
            for (int j = 0; j < Rr; j += step) glVertex3f(xMin, py(j), hz(0, j));
            glVertex3f(xMin, yMax, hz(0, Rr-1)); glEnd();
            glBegin(GL_LINE_STRIP);                       // X max
            for (int j = 0; j < Rr; j += step) glVertex3f(xMax, py(j), hz(Rr-1, j));
            glVertex3f(xMax, yMax, hz(Rr-1, Rr-1)); glEnd();
            // Fond + 4 arêtes verticales
            glBegin(GL_LINE_LOOP);
            glVertex3f(xMin, yMin, zBot); glVertex3f(xMax, yMin, zBot);
            glVertex3f(xMax, yMax, zBot); glVertex3f(xMin, yMax, zBot);
            glEnd();
            glBegin(GL_LINES);
            glVertex3f(xMin, yMin, zBot); glVertex3f(xMin, yMin, hz(0, 0));
            glVertex3f(xMax, yMin, zBot); glVertex3f(xMax, yMin, hz(Rr-1, 0));
            glVertex3f(xMax, yMax, zBot); glVertex3f(xMax, yMax, hz(Rr-1, Rr-1));
            glVertex3f(xMin, yMax, zBot); glVertex3f(xMin, yMax, hz(0, Rr-1));
            glEnd();
            glLineWidth(1.f);
            glDepthRange(0.0, 1.0);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_perfDrawNs += dt.nsecsElapsed();

    // Loupe : recalcul ~0,35 s après le dernier mouvement de caméra
    if (m_lpTimer && !m_playing && m_rwMul == 0 && !m_sliderDrag) {
        const QVector<float> k = camKey();
        if (k != m_lpLastCam) { m_lpLastCam = k; m_lpTimer->start(); }
        else if ((k != m_lpBuiltCam || m_lpVerBuilt != m_matVer.load())
                 && !m_lpFuture.isRunning() && !m_lpTimer->isActive()) m_lpTimer->start();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Dessin
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::drawAxes()
{
    glDisable(GL_LIGHTING);
    float len=m_stock.valid?qMax(m_stock.xMax-m_stock.xMin,m_stock.yMax-m_stock.yMin)*0.12f:20.f;
    glLineWidth(2.f); glBegin(GL_LINES);
    glColor3f(.90f,.20f,.20f);glVertex3f(0,0,0);glVertex3f(len,0,0);
    glColor3f(.20f,.75f,.20f);glVertex3f(0,0,0);glVertex3f(0,len,0);
    glColor3f(.30f,.55f,.90f);glVertex3f(0,0,0);glVertex3f(0,0,len);
    glEnd(); glLineWidth(1.f); glEnable(GL_LIGHTING);
}
void Sim3DWidget::drawStock()
{
    if(!m_stock.valid) return;
    float x0=m_stock.xMin,x1=m_stock.xMax,y0=m_stock.yMin,y1=m_stock.yMax,z0=m_stock.zMin,z1=m_stock.zMax;
    glDisable(GL_CULL_FACE);

    // ── Rendu unique avec opacité variable m_stockAlpha ────────
    // Slider plage 0..0.975 (97.5% max). À 97.5%, on snappe à 1.0
    // pour avoir un rendu opaque sans artefacts (z-fighting fin).
    const float aEff = (m_stockAlpha >= 0.974f) ? 1.0f : m_stockAlpha;
    const float A   = qBound(0.05f, aEff,         1.0f);
    const float Ai  = qBound(0.05f, aEff * 0.55f, 1.0f);
    // Dessous du brut : même aluminium que la matière (plus de vert)
    const float R=0.50f, G=0.52f, B=0.55f;
    const float Ri=0.45f, Gi=0.47f, Bi=0.50f;
    const bool  blend = (A < 0.999f);

    glDisable(GL_LIGHTING);
    if(blend){
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glBegin(GL_QUADS);
    // ── Parois latérales NON dessinées ici ─────────────────────
    //  Les 4 parois étaient des quads pleins de zMin à zMax. Elles
    //  masquaient la matière érodée par l'outil au bord du brut
    //  (heightmap modifiée mais paroi fixe → on ne voyait pas le creux).
    //  Désormais les parois latérales sont dessinées par la heightmap
    //  elle-même (PASS 3 dans rebuildHeightmapVBO), avec hauteurs
    //  réelles cellule par cellule → s'érodent quand l'outil passe juste
    //  à côté. Voir aussi rebuildHeightmapVBO::PASS 3.
    // Dessous (couleur foncée)
    glColor4f(R,G,B,A * 0.8f);
    glVertex3f(x0,y0,z0);glVertex3f(x0,y1,z0);glVertex3f(x1,y1,z0);glVertex3f(x1,y0,z0);
    // Face inférieure interne (vue par dessous en transparent)
    glColor4f(Ri,Gi,Bi,Ai);
    glVertex3f(x1,y0,z0);glVertex3f(x1,y1,z0);glVertex3f(x0,y1,z0);glVertex3f(x0,y0,z0);
    glEnd();
    if(blend) glDepthMask(GL_TRUE);


    // Arêtes noires du brut — uniquement les arêtes du bas (le haut suit
    // la heightmap qui a son propre wireframe Marching Squares).
    glDisable(GL_LIGHTING);
    glColor4f(0.05f,0.05f,0.05f,0.90f); glLineWidth(1.5f);
    glBegin(GL_LINES);
    // 4 arêtes du fond uniquement
    glVertex3f(x0,y0,z0);glVertex3f(x1,y0,z0);glVertex3f(x1,y0,z0);glVertex3f(x1,y1,z0);
    glVertex3f(x1,y1,z0);glVertex3f(x0,y1,z0);glVertex3f(x0,y1,z0);glVertex3f(x0,y0,z0);
    glEnd();
    glLineWidth(1.f); glEnable(GL_LIGHTING);
}

// Forward declarations — définies plus bas
static void toolPastelColor(int T, float &r, float &g, float &b);
static void toolFaceColor(int T, float &r, float &g, float &b, float &a, bool opaque);

// ── drawMeshGPU ────────────────────────────────────────────────
// Rendu du mesh OCC triangulé. Palette colorForZ identique à
// drawHeightmap pour continuité visuelle.
// Appelé depuis paintGL sous m_occMeshMutex déjà verrouillé.
void Sim3DWidget::drawMeshGPU()
{
    if(m_occMesh.isEmpty()) return;
    bool opaque = m_stockOpaque;
    glEnable(GL_LIGHTING); glShadeModel(GL_SMOOTH);
    GLfloat sp[]={0.4f,0.4f,0.4f,1.f};
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,sp);
    glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,40.f);
    glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
    if(opaque){
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    } else {
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    glBegin(GL_TRIANGLES);
    for(const MeshVertex &v:m_occMesh){
        float r,g,b,a;
        toolFaceColor(v.toolT,r,g,b,a,opaque);
        glColor4f(r,g,b,a);
        glNormal3f(v.nx,v.ny,v.nz);
        glVertex3f(v.x,v.y,v.z);
    }
    glEnd();
    if(!opaque) glDepthMask(GL_TRUE);
}

void Sim3DWidget::rebuildNormalCache()
{
    // Appelé depuis rebuildHeightmapVBO (déjà sous mutex) — pas de double lock
    const int R=m_hmap.res;
    if(R<2||m_hmap.data.empty()){ m_hmapNormalsDirty=false; return; }
    const float dx=2.f*(m_hmap.xMax-m_hmap.xMin)/(R-1);
    const float dy=2.f*(m_hmap.yMax-m_hmap.yMin)/(R-1);
    m_normalCache.resize(R*R);
    const Heightmap &h=m_hmap;

    // ── Pass 1 : gradient Sobel brut ──────────────────────────
    std::vector<QVector3D> raw(R*R);
    for(int i=0;i<R;i++) for(int j=0;j<R;j++){
            float gx=(h.safe(i+1,j-1)+2.f*h.safe(i+1,j)+h.safe(i+1,j+1))
            -(h.safe(i-1,j-1)+2.f*h.safe(i-1,j)+h.safe(i-1,j+1));
            float gy=(h.safe(i-1,j+1)+2.f*h.safe(i,j+1)+h.safe(i+1,j+1))
                       -(h.safe(i-1,j-1)+2.f*h.safe(i,j-1)+h.safe(i+1,j-1));
            gx/=(4.f*dx); gy/=(4.f*dy);
            float l=sqrtf(gx*gx+gy*gy+1.f);
            raw[i*R+j]={-gx/l,-gy/l,1.f/l};
        }

    // ── Pass 2 : lissage des normales par moyenne 3x3 ─────────
    // Gomme les micro-marches crénelées sans toucher aux bords francs
    auto safeN=[&](int ii,int jj)->const QVector3D&{
        int i2=ii<0?0:(ii>=R?R-1:ii);
        int j2=jj<0?0:(jj>=R?R-1:jj);
        return raw[i2*R+j2];
    };
    for(int i=0;i<R;i++) for(int j=0;j<R;j++){
            QVector3D sum(0,0,0);
            for(int di=-1;di<=1;di++)
                for(int dj=-1;dj<=1;dj++)
                    sum+=safeN(i+di,j+dj);
            sum/=9.f;
            float l=sqrtf(sum.x()*sum.x()+sum.y()*sum.y()+sum.z()*sum.z());
            if(l>1e-6f) sum/=l;
            m_normalCache[i*R+j]=sum;
        }
    m_hmapNormalsDirty=false;
}


// ═══════════════════════════════════════════════════════════════
//  rebuildHeightmapVBO — construit/met à jour le VBO heightmap
//  Appelé 1x/changement de données, pas à chaque frame.
//  Format vertex interleaved : pos(3f)+normal(3f)+color(4f) = 40 bytes
//  Strips : (R-1) x R*2 vertices → glDrawArrays GL_TRIANGLE_STRIP
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::rebuildHeightmapVBO()
{
    QMutexLocker lk(&m_hmapMutex);
    const int R = m_hmap.res;
    if (R < 2 || m_hmap.data.empty()) { m_hmapVBODirty=false; return; }

    // ── Lissage avec préservation des arêtes ─────────────────────
    // Le bruit de discrétisation crée des variations < 1mm sur les
    // bords des passes (silhouette crénelée des parois). On les
    // moyenne avec un noyau 3x3, MAIS on préserve les arêtes franches
    // (variations > 1mm) pour ne pas arrondir les rebords de poche.
    //
    // La heightmap d'origine est swappée temporairement ; restaurée
    // à la fin → le lissage n'est que pour le rendu, pas pour les
    // calculs incrémentaux suivants.
    std::vector<float> smoothed(R * R);
    {
        const float edgeThresh = 1.0f;  // mm — arêtes franches au-dessus
        const std::vector<float> &src = m_hmap.data;
        for (int i = 0; i < R; i++) {
            for (int j = 0; j < R; j++) {
                float c = src[i*R + j];
                // Déterminer si la cellule est près d'une arête franche
                float zMin = c, zMax = c;
                for (int di = -1; di <= 1; di++) {
                    for (int dj = -1; dj <= 1; dj++) {
                        int ii = qBound(0, i+di, R-1);
                        int jj = qBound(0, j+dj, R-1);
                        float v = src[ii*R + jj];
                        if (v < zMin) zMin = v;
                        if (v > zMax) zMax = v;
                    }
                }
                if (zMax - zMin > edgeThresh) {
                    // Près d'une arête franche → ne pas lisser
                    smoothed[i*R + j] = c;
                } else {
                    // Variations modérées → moyenne 3x3 (lisse les passes)
                    float sum = 0.f; int n = 0;
                    for (int di = -1; di <= 1; di++) {
                        for (int dj = -1; dj <= 1; dj++) {
                            int ii = qBound(0, i+di, R-1);
                            int jj = qBound(0, j+dj, R-1);
                            sum += src[ii*R + jj];
                            n++;
                        }
                    }
                    smoothed[i*R + j] = sum / float(n);
                }
            }
        }
    }
    // Swap temporaire : m_hmap.data ← smoothed pour la durée du rebuild
    std::vector<float> origData;
    origData.swap(m_hmap.data);
    m_hmap.data.swap(smoothed);
    // Forcer le recalcul des normales sur la version lissée
    m_hmapNormalsDirty = true;
    if (m_hmapNormalsDirty) rebuildNormalCache();

    const float dx      = (R>1) ? (m_hmap.xMax-m_hmap.xMin)/(R-1) : 1.f;
    const float dy      = (R>1) ? (m_hmap.yMax-m_hmap.yMin)/(R-1) : 1.f;

    // Format vertex : pos(3f) + normal(3f) + color(4f) = 40 bytes
    struct Vtx { float x,y,z, nx,ny,nz, r,g,b,a; };
    std::vector<Vtx> verts;
    verts.reserve(R*R*18);  // surface + murs (borne large)

    // ── Helper : couleur d'un point ──────────────────────────
    // Mêmes couleurs en opaque et transparent — seul l'alpha diffère
    auto getColor = [&](int i, int j, float &cr, float &cg, float &cb, float &ca) {
        // ── Détection zone usinée par Z (pas par toolMap) ──
        // Le toolMap n'est pas lissé par le filtre 3x3 du dessus,
        // donc les cellules à Z intermédiaire (lissé entre zone usinée
        // et brut) peuvent rester à T=0 → couleur brut sur mi-paroi.
        // On regarde plutôt si le Z de la cellule est descendu sous
        // zTop : si oui, c'est de la matière retirée.
        const float z    = m_hmap.safe(i,j);
        const float zTop = m_stock.zMax;
        const bool  usine = (z < zTop - 0.05f);
        // Récupère l'outil dominant : celui de la cellule, ou si T=0
        // (cellule pas marquée à cause du lissage), celui d'un voisin
        // usiné — pour avoir une couleur cohérente au lieu du vert brut.
        int T = m_hmap.toolMap.empty() ? 0 : m_hmap.toolMap[i*R+j];
        if (usine && T <= 0) {
            // Cherche un voisin avec un toolT valide
            for (int di=-1; di<=1 && T<=0; di++)
                for (int dj=-1; dj<=1 && T<=0; dj++) {
                    int ii=qBound(0,i+di,R-1), jj=qBound(0,j+dj,R-1);
                    int Tn = m_hmap.toolMap[ii*R+jj];
                    if (Tn > 0) T = Tn;
                }
        }
        // Alpha effectif : à 97.5% (= max du slider), on force 1.0 dans
        // le VBO pour avoir un rendu vraiment opaque sans blend bizarre.
        const float aEff = (m_stockAlpha >= 0.974f) ? 1.0f : m_stockAlpha;
        if (usine) {
            // Zone usinée : couleur outil, alpha suit m_stockAlpha
            toolFaceColor(T,cr,cg,cb,ca,false);
            ca = aEff;
        } else {
            // Zone brute non usinée
            cr=0.38f; cg=0.55f; cb=0.38f;
            ca = aEff;
        }
    };
    auto addV = [&](float x,float y,float z,
                    float nx,float ny,float nz,
                    float r,float g,float b,float a){
        verts.push_back({x,y,z,nx,ny,nz,r,g,b,a});
    };

    // ════════════════════════════════════════════════════════
    //  PASS 1A — Cellules BRUTES (4 coins non usinés)
    //  → constituent le "top du brut" visible au-dessus de la pièce.
    //  Stockées EN PREMIER dans le VBO. Plage [0..m_hmapBrutCount[
    //  dessinée séparément avec opacité = m_stockAlpha.
    //
    //  Critère "brute" : tous les Z des 4 coins sont >= zTop - 0.05
    //  (= aucune cellule du quad n'a été creusée par l'outil).
    // ════════════════════════════════════════════════════════
    const float zTopBrut = m_stock.zMax;
    auto cellEstBrute = [&](int i, int j) -> bool {
        const float thr = zTopBrut - 0.05f;
        return (m_hmap.safe(i,  j  ) >= thr) &&
               (m_hmap.safe(i+1,j  ) >= thr) &&
               (m_hmap.safe(i,  j+1) >= thr) &&
               (m_hmap.safe(i+1,j+1) >= thr);
    };

    for (int i = 0; i < R-1; i++) {
        for (int j = 0; j < R-1; j++) {
            if (!cellEstBrute(i,j)) continue;
            float x0=m_hmap.worldX(i),   x1=m_hmap.worldX(i+1);
            float y0=m_hmap.worldY(j),   y1=m_hmap.worldY(j+1);
            float z00=m_hmap.safe(i,j),  z10=m_hmap.safe(i+1,j);
            float z01=m_hmap.safe(i,j+1),z11=m_hmap.safe(i+1,j+1);
            const QVector3D &n00=m_normalCache[i*R+j];
            const QVector3D &n10=m_normalCache[(i+1)*R+j];
            const QVector3D &n01=m_normalCache[i*R+j+1];
            const QVector3D &n11=m_normalCache[(i+1)*R+j+1];
            float r00,g00,b00,a00; getColor(i,  j,  r00,g00,b00,a00);
            float r10,g10,b10,a10; getColor(i+1,j,  r10,g10,b10,a10);
            float r01,g01,b01,a01; getColor(i,  j+1,r01,g01,b01,a01);
            float r11,g11,b11,a11; getColor(i+1,j+1,r11,g11,b11,a11);
            addV(x0,y0,z00, n00.x(),n00.y(),n00.z(), r00,g00,b00,a00);
            addV(x1,y0,z10, n10.x(),n10.y(),n10.z(), r10,g10,b10,a10);
            addV(x1,y1,z11, n11.x(),n11.y(),n11.z(), r11,g11,b11,a11);
            addV(x0,y0,z00, n00.x(),n00.y(),n00.z(), r00,g00,b00,a00);
            addV(x1,y1,z11, n11.x(),n11.y(),n11.z(), r11,g11,b11,a11);
            addV(x0,y1,z01, n01.x(),n01.y(),n01.z(), r01,g01,b01,a01);
        }
    }
    // Marqueur : nb de vertices "brut" dans le VBO
    m_hmapBrutCount = (int)verts.size();

    // ════════════════════════════════════════════════════════
    //  PASS 1B — Cellules USINÉES (au moins 1 coin creusé)
    //  Stockées APRÈS les cellules brutes. Toujours opaques.
    // ════════════════════════════════════════════════════════
    for (int i = 0; i < R-1; i++) {
        for (int j = 0; j < R-1; j++) {
            if (cellEstBrute(i,j)) continue;
            float x0=m_hmap.worldX(i),   x1=m_hmap.worldX(i+1);
            float y0=m_hmap.worldY(j),   y1=m_hmap.worldY(j+1);
            float z00=m_hmap.safe(i,j),  z10=m_hmap.safe(i+1,j);
            float z01=m_hmap.safe(i,j+1),z11=m_hmap.safe(i+1,j+1);
            const QVector3D &n00=m_normalCache[i*R+j];
            const QVector3D &n10=m_normalCache[(i+1)*R+j];
            const QVector3D &n01=m_normalCache[i*R+j+1];
            const QVector3D &n11=m_normalCache[(i+1)*R+j+1];
            float r00,g00,b00,a00; getColor(i,  j,  r00,g00,b00,a00);
            float r10,g10,b10,a10; getColor(i+1,j,  r10,g10,b10,a10);
            float r01,g01,b01,a01; getColor(i,  j+1,r01,g01,b01,a01);
            float r11,g11,b11,a11; getColor(i+1,j+1,r11,g11,b11,a11);
            addV(x0,y0,z00, n00.x(),n00.y(),n00.z(), r00,g00,b00,a00);
            addV(x1,y0,z10, n10.x(),n10.y(),n10.z(), r10,g10,b10,a10);
            addV(x1,y1,z11, n11.x(),n11.y(),n11.z(), r11,g11,b11,a11);
            addV(x0,y0,z00, n00.x(),n00.y(),n00.z(), r00,g00,b00,a00);
            addV(x1,y1,z11, n11.x(),n11.y(),n11.z(), r11,g11,b11,a11);
            addV(x0,y1,z01, n01.x(),n01.y(),n01.z(), r01,g01,b01,a01);
        }
    }

    // ════════════════════════════════════════════════════════
    //  PASS 2 — Murs verticaux lissés (Marching Squares 2D)
    //
    //  Au lieu de générer un quad par arête de cellule (escalier),
    //  on extrait l'iso-contour Marching Squares pour chaque
    //  transition entre paliers Z. Chaque segment de la polyligne
    //  est ensuite extrudé verticalement de zBas à zHaut.
    //
    //  Résultat : les murs suivent la VRAIE courbe de la pièce,
    //  plus l'escalier de la grille. Les segments voisins se
    //  rejoignent en formant une polyligne continue (proche d'une
    //  spline) → plus de dents, plus de picots.
    //
    //  Couleur : option A — une couleur par palier (couleur de
    //  l'outil dominant sur le palier inférieur).
    // ════════════════════════════════════════════════════════

    // Détecter les paliers Z (clusters de hauteur stables)
    QVector<float> paliers;
    {
        const float zMinBrut = m_stock.zMin, zMaxBrut = m_stock.zMax;
        const float histRange = zMaxBrut - zMinBrut;
        if (histRange >= 0.1f) {
            const int histN = 200;
            std::vector<int> hist(histN, 0);
            for (float z : m_hmap.data) {
                int b = int((z - zMinBrut) / histRange * (histN - 1) + 0.5f);
                b = qBound(0, b, histN - 1);
                hist[b]++;
            }
            const int totalCells = R * R;
            const int threshold  = totalCells / 80;   // palier = >1.25% des cellules
            for (int b = 0; b < histN; b++) {
                if (hist[b] >= threshold) {
                    float z = zMinBrut + float(b) / (histN - 1) * histRange;
                    // Écart minimum entre paliers relevé à 1.0mm (au lieu de
                    // 0.5) : pendant l'usinage en cours, les cellules sont à
                    // des Z variés et créaient trop de paliers proches → murs
                    // qui se chevauchent en pics verticaux (crénelure). En
                    // regroupant, les parois sont plus lisses.
                    if (!paliers.isEmpty() && qAbs(z - paliers.last()) < 1.0f) continue;
                    paliers.append(z);
                }
            }
        }
    }

    // Pour chaque palier, déterminer l'outil dominant sur ce palier
    // (= couleur du palier). On parcourt les cellules dont Z est dans
    // [palier - 0.25, palier + 0.25] et on fait un mode des toolT.
    auto paletteForLevel = [&](float zLevel, float &cr, float &cg, float &cb) {
        // Couleur de la paroi = couleur de l'outil qui a usiné à ce niveau.
        // On compte les outils des cellules dont le Z est proche de zLevel
        // ET qui sont réellement usinées (Z sous le brut). Fenêtre élargie
        // (0.6) pour bien capter les cellules de la paroi malgré le lissage.
        const float zTopB = m_stock.zMax;
        std::array<int, 16> count = {};
        for (int k = 0; k < (int)m_hmap.data.size(); k++) {
            if (m_hmap.data[k] >= zTopB - 0.05f) continue;      // ignore brut
            if (qAbs(m_hmap.data[k] - zLevel) < 0.6f) {
                int T = m_hmap.toolMap[k];
                if (T >= 0 && T < (int)count.size()) count[T]++;
            }
        }
        int bestT = 0, bestC = 0;
        for (int t = 1; t < (int)count.size(); t++)   // t=1 : ignore T=0 (brut)
            if (count[t] > bestC) { bestC = count[t]; bestT = t; }
        float a;
        toolFaceColor(bestT, cr, cg, cb, a, false);
    };

    // Extraction Marching Squares — polylignes XY pour chaque transition.
    // Helper local : interp linéaire sur arête.
    auto interp2 = [](float zA, float zB, float a, float b, float zSeuil) -> float {
        float dz = zB - zA;
        if (qAbs(dz) < 1e-6f) return (a + b) * 0.5f;
        float t = (zSeuil - zA) / dz;
        t = qBound(0.f, t, 1.f);
        return a + (b - a) * t;
    };

    // Parcours par paire de paliers (zBas, zHaut)
    for (int p = 0; p < paliers.size() - 1; p++) {
        const float zLow  = paliers[p];
        const float zHigh = paliers[p + 1];
        const float zSeuil = (zLow + zHigh) * 0.5f;

        // Couleur du mur = couleur du palier inférieur (cellule usinée)
        float wr, wg, wb;
        paletteForLevel(zLow, wr, wg, wb);
        // Alpha effectif identique au reste : 97.5%+ → 1.0
        const float wa = (m_stockAlpha >= 0.974f) ? 1.0f : m_stockAlpha;

        for (int i = 0; i < R - 1; i++) {
            for (int j = 0; j < R - 1; j++) {
                float z00 = m_hmap.data[i*R + j];
                float z10 = m_hmap.data[(i+1)*R + j];
                float z11 = m_hmap.data[(i+1)*R + j+1];
                float z01 = m_hmap.data[i*R + j+1];

                int idx = 0;
                if (z00 >= zSeuil) idx |= 1;
                if (z10 >= zSeuil) idx |= 2;
                if (z11 >= zSeuil) idx |= 4;
                if (z01 >= zSeuil) idx |= 8;
                if (idx == 0 || idx == 15) continue;

                float x0 = m_hmap.xMin + i * dx;
                float x1 = x0 + dx;
                float y0 = m_hmap.yMin + j * dy;
                float y1 = y0 + dy;

                float xBas  = interp2(z00, z10, x0, x1, zSeuil);
                float yDr   = interp2(z10, z11, y0, y1, zSeuil);
                float xHaut = interp2(z01, z11, x0, x1, zSeuil);
                float yGa   = interp2(z00, z01, y0, y1, zSeuil);

                // Helper : extruder un segment 2D en quad vertical
                // de zLow à zHigh, avec normale = perpendiculaire 2D
                auto pushWall = [&](float ax, float ay, float bx, float by) {
                    float ex = bx - ax, ey = by - ay;
                    float l = sqrtf(ex*ex + ey*ey);
                    if (l < 1e-6f) return;
                    // Normale = perpendiculaire à droite (cohérent avec
                    // sens Marching Squares : matière à droite du segment)
                    float nx = ey / l, ny = -ex / l;
                    addV(ax,ay,zLow,  nx,ny,0, wr,wg,wb,wa);
                    addV(bx,by,zLow,  nx,ny,0, wr,wg,wb,wa);
                    addV(bx,by,zHigh, nx,ny,0, wr,wg,wb,wa);
                    addV(ax,ay,zLow,  nx,ny,0, wr,wg,wb,wa);
                    addV(bx,by,zHigh, nx,ny,0, wr,wg,wb,wa);
                    addV(ax,ay,zHigh, nx,ny,0, wr,wg,wb,wa);
                };

                switch (idx) {
                case 1:  case 14: pushWall(xBas,y0, x0,yGa); break;
                case 2:  case 13: pushWall(xBas,y0, x1,yDr); break;
                case 3:  case 12: pushWall(x0,yGa,  x1,yDr); break;
                case 4:  case 11: pushWall(x1,yDr,  xHaut,y1); break;
                case 6:  case 9:  pushWall(xBas,y0, xHaut,y1); break;
                case 7:  case 8:  pushWall(x0,yGa,  xHaut,y1); break;
                case 5:
                    pushWall(xBas,y0, x1,yDr);
                    pushWall(x0,yGa,  xHaut,y1);
                    break;
                case 10:
                    pushWall(xBas,y0, x0,yGa);
                    pushWall(x1,yDr,  xHaut,y1);
                    break;
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════
    //  PASS 3 — Murs latéraux du brut (4 bords de la heightmap)
    //
    //  Ces murs remplacent les 4 parois fixes de drawStock(). Pour
    //  chaque cellule du bord (i=0, i=R-1, j=0, j=R-1), on dessine
    //  un quad vertical de zMinBrut à la hauteur réelle de la cellule.
    //  Quand l'outil grignote le bord, la cellule baisse → le mur
    //  s'érode visuellement.
    //
    //  Couleur : vert brut (comme l'ancien drawStock).
    //  Normale : sortante (vers l'extérieur du brut).
    // ════════════════════════════════════════════════════════
    {
        const float zBot = m_stock.zMin;
        // Couleur brut (identique à drawStock)
        const float wr = 0.42f, wg = 0.62f, wb = 0.42f;
        const float wa = (m_stockAlpha >= 0.974f) ? 1.0f : m_stockAlpha;

        auto pushBordMur = [&](float ax, float ay, float zA,
                               float bx, float by, float zB,
                               float nx, float ny) {
            // Quad : (ax,ay,zBot)-(bx,by,zBot)-(bx,by,zB)-(ax,ay,zA)
            // Si zA ou zB <= zBot+eps, ne pas dessiner ce mur (rien à montrer)
            if (zA <= zBot + 0.01f && zB <= zBot + 0.01f) return;
            addV(ax,ay,zBot, nx,ny,0, wr,wg,wb,wa);
            addV(bx,by,zBot, nx,ny,0, wr,wg,wb,wa);
            addV(bx,by,zB,   nx,ny,0, wr,wg,wb,wa);
            addV(ax,ay,zBot, nx,ny,0, wr,wg,wb,wa);
            addV(bx,by,zB,   nx,ny,0, wr,wg,wb,wa);
            addV(ax,ay,zA,   nx,ny,0, wr,wg,wb,wa);
        };

        // ── Bord Y- (j=0) : normale (0,-1,0) ──────────────
        for (int i = 0; i < R - 1; i++) {
            float x0 = m_hmap.worldX(i),   x1 = m_hmap.worldX(i+1);
            float y  = m_hmap.worldY(0);
            float zA = m_hmap.data[i*R + 0];
            float zB = m_hmap.data[(i+1)*R + 0];
            pushBordMur(x0,y,zA, x1,y,zB, 0.f,-1.f);
        }
        // ── Bord Y+ (j=R-1) : normale (0,+1,0) ────────────
        for (int i = 0; i < R - 1; i++) {
            float x0 = m_hmap.worldX(i),   x1 = m_hmap.worldX(i+1);
            float y  = m_hmap.worldY(R-1);
            float zA = m_hmap.data[i*R + (R-1)];
            float zB = m_hmap.data[(i+1)*R + (R-1)];
            // Inverser ordre pour normale sortante
            pushBordMur(x1,y,zB, x0,y,zA, 0.f,+1.f);
        }
        // ── Bord X- (i=0) : normale (-1,0,0) ──────────────
        for (int j = 0; j < R - 1; j++) {
            float x  = m_hmap.worldX(0);
            float y0 = m_hmap.worldY(j),   y1 = m_hmap.worldY(j+1);
            float zA = m_hmap.data[0*R + j];
            float zB = m_hmap.data[0*R + (j+1)];
            // Inverser pour normale -X
            pushBordMur(x,y1,zB, x,y0,zA, -1.f,0.f);
        }
        // ── Bord X+ (i=R-1) : normale (+1,0,0) ────────────
        for (int j = 0; j < R - 1; j++) {
            float x  = m_hmap.worldX(R-1);
            float y0 = m_hmap.worldY(j),   y1 = m_hmap.worldY(j+1);
            float zA = m_hmap.data[(R-1)*R + j];
            float zB = m_hmap.data[(R-1)*R + (j+1)];
            pushBordMur(x,y0,zA, x,y1,zB, +1.f,0.f);
        }
    }

    // ── Upload GPU ───────────────────────────────────────────
    if (m_hmapVBO==0) glGenBuffers(1,&m_hmapVBO);
    glBindBuffer(GL_ARRAY_BUFFER,m_hmapVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size()*sizeof(Vtx)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,0);

    m_hmapTotalCount = (int)verts.size();
    m_hmapVBODirty   = false;
    m_hmapVBORes     = R;

    // ── Restaurer la heightmap d'origine (non lissée) ────────────
    // Le lissage n'était que pour le mesh visuel ; les calculs
    // incrémentaux suivants (applyToolIncremental, applyTool, etc.)
    // doivent travailler sur les vraies valeurs de profondeur.
    m_hmap.data.swap(origData);
}

// ── drawHeightmapWireframe — contours noirs sur toutes les transitions ──
// Pour chaque frontière entre deux cellules de hauteurs différentes (dz
// significatif), trace un trait noir HORIZONTAL au niveau de la cellule
// la plus haute. Comme le segment est dans un plan horizontal, il ne
// peut jamais "zigzaguer" le long d'une paroi : il fait toujours le
// pourtour des plateaux à chaque palier de hauteur (top du brut, top
// d'une passe, top d'un fond, top des trous, etc.).
//
// Effet visuel : style TNC640 — contour net autour de chaque marche,
// chaque trou, chaque épaulement.
// ── extractIsoContour — Marching Squares 2D ─────────────────────
// Pour chaque carré 2x2 de cellules de la heightmap, calcule où la
// ligne de niveau Z=zSeuil le coupe par interpolation linéaire.
// Retourne un tableau plat de paires de QPointF : pts[2k], pts[2k+1]
// = un segment du contour. Les segments ne sont pas chaînés (on les
// laisse en GL_LINES pour rester simple et rapide).
//
// Note : pas de protection mutex ici — appelée depuis des fonctions
// qui ont déjà pris m_hmapMutex.
QVector<QPointF> Sim3DWidget::extractIsoContour(float zSeuil) const
{
    QVector<QPointF> segs;
    const int R = m_hmap.res;
    if (R < 2 || (int)m_hmap.data.size() < R * R) return segs;

    const float cw = (m_stock.xMax - m_stock.xMin) / float(R - 1);
    const float ch = (m_stock.yMax - m_stock.yMin) / float(R - 1);

    // Estimation : ~2*R segments par contour (borne large)
    segs.reserve(R * 4);

    // Helper : interpolation linéaire — où se trouve zSeuil entre zA et zB ?
    auto interp = [zSeuil](float zA, float zB, float a, float b) -> float {
        float dz = zB - zA;
        if (qAbs(dz) < 1e-6f) return (a + b) * 0.5f;
        float t = (zSeuil - zA) / dz;
        t = qBound(0.f, t, 1.f);
        return a + (b - a) * t;
    };

    for (int i = 0; i < R - 1; i++) {
        for (int j = 0; j < R - 1; j++) {
            float z00 = m_hmap.data[i*R + j];
            float z10 = m_hmap.data[(i+1)*R + j];
            float z11 = m_hmap.data[(i+1)*R + j+1];
            float z01 = m_hmap.data[i*R + j+1];

            int idx = 0;
            if (z00 >= zSeuil) idx |= 1;
            if (z10 >= zSeuil) idx |= 2;
            if (z11 >= zSeuil) idx |= 4;
            if (z01 >= zSeuil) idx |= 8;
            if (idx == 0 || idx == 15) continue;

            float x0 = m_stock.xMin + i * cw;
            float x1 = x0 + cw;
            float y0 = m_stock.yMin + j * ch;
            float y1 = y0 + ch;

            // 4 points d'intersection possibles sur les 4 arêtes
            float xBas  = interp(z00, z10, x0, x1);   // arête y=y0, entre i,j et i+1,j
            float yDr   = interp(z10, z11, y0, y1);   // arête x=x1, entre i+1,j et i+1,j+1
            float xHaut = interp(z01, z11, x0, x1);   // arête y=y1, entre i,j+1 et i+1,j+1
            float yGa   = interp(z00, z01, y0, y1);   // arête x=x0, entre i,j et i,j+1

            // Table Marching Squares : pour chaque cas, segments à émettre
            switch (idx) {
            case 1:  case 14: segs.append({xBas,y0}); segs.append({x0,yGa}); break;
            case 2:  case 13: segs.append({xBas,y0}); segs.append({x1,yDr}); break;
            case 3:  case 12: segs.append({x0,yGa});  segs.append({x1,yDr}); break;
            case 4:  case 11: segs.append({x1,yDr});  segs.append({xHaut,y1}); break;
            case 6:  case 9:  segs.append({xBas,y0}); segs.append({xHaut,y1}); break;
            case 7:  case 8:  segs.append({x0,yGa});  segs.append({xHaut,y1}); break;
            case 5:  // ambiguïté
                segs.append({xBas,y0}); segs.append({x1,yDr});
                segs.append({x0,yGa}); segs.append({xHaut,y1});
                break;
            case 10: // ambiguïté
                segs.append({xBas,y0}); segs.append({x0,yGa});
                segs.append({x1,yDr}); segs.append({xHaut,y1});
                break;
            }
        }
    }
    return segs;
}

// ── drawHeightmapWireframe — contours noirs lisses Marching Squares ──
//
// Au lieu de tracer un trait par frontière entre cellules (escalier),
// on extrait l'iso-contour Marching Squares pour chaque transition
// entre paliers Z. Chaque transition donne DEUX traits :
//   - un en haut, posé sur le palier supérieur (zHaut + 0.05)
//   - un en bas, posé sur le palier inférieur (zBas + 0.05)
// → ressemble au style TNC640 : rebord du haut ET pied de paroi soulignés.
void Sim3DWidget::drawHeightmapWireframe()
{
    QMutexLocker lk(&m_hmapMutex);
    const int R = m_hmap.res;
    if (R < 2 || (int)m_hmap.data.size() < R * R) return;

    // ── Détection des paliers Z (clusters de hauteur stables) ──
    const float zMinBrut = m_stock.zMin, zMaxBrut = m_stock.zMax;
    const float histRange = zMaxBrut - zMinBrut;
    if (histRange < 0.1f) return;
    const int histN = 200;
    std::vector<int> hist(histN, 0);
    for (float z : m_hmap.data) {
        int b = int((z - zMinBrut) / histRange * (histN - 1) + 0.5f);
        b = qBound(0, b, histN - 1);
        hist[b]++;
    }
    const int totalCells = R * R;
    const int threshold  = totalCells / 100;  // >1% des cellules
    QVector<float> paliers;
    for (int b = 0; b < histN; b++) {
        if (hist[b] >= threshold) {
            float z = zMinBrut + float(b) / (histN - 1) * histRange;
            if (!paliers.isEmpty() && qAbs(z - paliers.last()) < 0.5f) continue;
            paliers.append(z);
        }
    }
    if (paliers.size() < 2) return;  // brut intact

    // ── Tracé de tous les iso-contours ──
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    // ── glPolygonOffset : traits dessinés "devant" les surfaces ──
    // Sans ça, en mode 100% opaque le trait du fond de poche était
    // mangé par le maillage du fond (z-fighting). Avec un offset
    // négatif, OpenGL pousse les lignes vers la caméra dans le
    // depth buffer → elles passent toujours par-dessus.
    glEnable(GL_POLYGON_OFFSET_LINE);
    // Offset renforcé : en mode 97.5%+ (opaque), le maillage du fond
    // mangeait le trait noir du fond de poche malgré l'offset -1.
    // On pousse plus fort vers la caméra pour que les contours tiennent
    // dans tous les modes (transparent ET opaque).
    glPolygonOffset(-3.0f, -3.0f);
    glColor3f(0.05f, 0.05f, 0.05f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);

    for (int p = 0; p < paliers.size() - 1; p++) {
        const float zBas  = paliers[p];
        const float zHaut = paliers[p + 1];
        const float zSeuil = (zBas + zHaut) * 0.5f;
        QVector<QPointF> segs = extractIsoContour(zSeuil);

        // Trait HAUT — posé sur le palier supérieur (rebord)
        const float zTop = zHaut + 0.05f;
        for (int k = 0; k < segs.size(); k += 2) {
            const QPointF &a = segs[k];
            const QPointF &b = segs[k+1];
            glVertex3f((float)a.x(), (float)a.y(), zTop);
            glVertex3f((float)b.x(), (float)b.y(), zTop);
        }
        // Trait BAS — posé sur le palier inférieur (fond de poche)
        // Décalé horizontalement de 0.06 mm vers l'intérieur de la poche
        // (= côté de la cellule basse) pour ne pas être masqué par la
        // paroi vue de dessus.
        const float zDown   = zBas + 0.05f;
        const float inset   = 0.06f;  // mm
        for (int k = 0; k < segs.size(); k += 2) {
            const QPointF &a = segs[k];
            const QPointF &b = segs[k+1];
            // Direction le long du segment, normale 2D perpendiculaire
            float ex = (float)(b.x() - a.x());
            float ey = (float)(b.y() - a.y());
            float l  = sqrtf(ex*ex + ey*ey);
            if (l < 1e-6f) continue;
            // Normale gauche (perpendiculaire) : (-ey, ex) / l
            // On test à mi-segment dans les 2 sens, on prend celui où
            // Z est PLUS BAS (= intérieur de la poche).
            float mx = (float)(a.x() + b.x()) * 0.5f;
            float my = (float)(a.y() + b.y()) * 0.5f;
            float nx = -ey / l, ny = ex / l;
            // Échantillonner Z dans la heightmap aux 2 points perp
            auto sampleZ = [&](float wx, float wy) -> float {
                float fx = (wx - m_hmap.xMin) / (m_hmap.xMax - m_hmap.xMin) * (R - 1);
                float fy = (wy - m_hmap.yMin) / (m_hmap.yMax - m_hmap.yMin) * (R - 1);
                int ix = qBound(0, int(fx), R - 1);
                int iy = qBound(0, int(fy), R - 1);
                return m_hmap.data[ix*R + iy];
            };
            const float probeDist = qMax(0.5f, inset * 4.f);
            float zPlus  = sampleZ(mx + nx*probeDist, my + ny*probeDist);
            float zMinus = sampleZ(mx - nx*probeDist, my - ny*probeDist);
            // Si zPlus < zMinus → +n est vers la poche, sinon −n
            float sx = (zPlus < zMinus) ? +nx : -nx;
            float sy = (zPlus < zMinus) ? +ny : -ny;
            float ax = (float)a.x() + sx * inset;
            float ay = (float)a.y() + sy * inset;
            float bx = (float)b.x() + sx * inset;
            float by = (float)b.y() + sy * inset;
            glVertex3f(ax, ay, zDown);
            glVertex3f(bx, by, zDown);
        }
    }

    glEnd();
    glDisable(GL_POLYGON_OFFSET_LINE);
    glLineWidth(1.f);
    glEnable(GL_LIGHTING);
}

void Sim3DWidget::drawHeightmap()
{
    const int R = m_hmap.res;
    if (R < 2 || m_hmap.data.empty()) return;

    // Throttle : pendant la simu, le VBO change à chaque tick (incrémental).
    // On rebuild au max toutes les 50ms pour préserver les FPS, sauf si la
    // structure du VBO a changé (résolution) → rebuild immédiat.
    bool structuralChange = (m_hmapVBORes!=R);
    bool dataChange       = (m_hmapNormalsDirty || m_hmapVBODirty);
    bool throttleOk = !m_playing
                      || !m_vboLastBuild.isValid()
                      || m_vboLastBuild.elapsed() >= kVboMinIntervalMs;
    if (structuralChange || (dataChange && throttleOk)) {
        rebuildHeightmapVBO();
        m_vboLastBuild.start();
    }
    if (!m_hmapVBO || m_hmapTotalCount==0) return;

    // ── Rendu unique : tout suit m_stockAlpha ──
    // Brut, matière et murs ont tous le même alpha = m_stockAlpha.
    // Toute la pièce s'affiche avec une opacité uniforme.
    glDisable(GL_LIGHTING);
    // Note : le slider va de 0% à 97.5% (max 0.975). On considère
    // "opaque" dès que l'alpha atteint 97.5% — pas de blend dans ce cas
    // (évite les artefacts de rendu vus à 100% strict).
    const bool blend = (m_stockAlpha < 0.974f);
    if (blend) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    const int stride = 10*sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, m_hmapVBO);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, (const void*)(0));
    glNormalPointer(   GL_FLOAT, stride, (const void*)(3*sizeof(float)));
    glColorPointer (4, GL_FLOAT, stride, (const void*)(6*sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, m_hmapTotalCount);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDepthMask(GL_TRUE);

    // Wireframe NON dessiné ici : appelé depuis paintGL après tout
}


// ── drawHeightmapSolid — mode opaque, réutilise le même VBO ──────
void Sim3DWidget::drawHeightmapSolid()
{
    const int R = m_hmap.res;
    if (R < 2 || m_hmap.data.empty()) return;

    // Throttle (cf drawHeightmap)
    bool structuralChange = (m_hmapVBORes!=R || m_hmapVBOOpaque!=m_stockOpaque);
    bool dataChange       = (m_hmapNormalsDirty || m_hmapVBODirty);
    bool throttleOk = !m_playing
                      || !m_vboLastBuild.isValid()
                      || m_vboLastBuild.elapsed() >= kVboMinIntervalMs;
    if (structuralChange || (dataChange && throttleOk)) {
        rebuildHeightmapVBO();
        m_vboLastBuild.start();
    }
    if (!m_hmapVBO || m_hmapTotalCount==0) return;

    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    // ── Pas d'éclairage : couleurs pures à l'écran ──
    // Identique au mode transparent → cohérence visuelle parfaite
    // entre les deux modes (juste l'alpha qui change).
    glDisable(GL_LIGHTING);

    const int stride = 10*sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, m_hmapVBO);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, (const void*)(0));
    glNormalPointer(   GL_FLOAT, stride, (const void*)(3*sizeof(float)));
    glColorPointer (4, GL_FLOAT, stride, (const void*)(6*sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, m_hmapTotalCount);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // ── Rebord noir sur le contour de la poche ──
    drawHeightmapWireframe();
}


// Chaque outil a une couleur distincte et saturée.
// T1=vert TNC, T2=brun/marron, T3=bleu, T4=orange, T5=violet...
static void toolFaceColor(int T, float &r, float &g, float &b, float &a, bool opaque)
{
    a = opaque ? 1.0f : 0.75f;
    if(T <= 0){
        // Brut non usiné : gris aluminium
        r=0.55f; g=0.58f; b=0.62f; return;
    }
    static const float pal[][3] = {
        {0.50f, 0.72f, 0.17f},  // T1 vert TNC (#A6CE39 assombri)
        {0.52f, 0.32f, 0.15f},  // T2 marron/brun
        {0.20f, 0.45f, 0.75f},  // T3 bleu acier
        {0.80f, 0.45f, 0.10f},  // T4 orange
        {0.55f, 0.20f, 0.65f},  // T5 violet
        {0.15f, 0.65f, 0.55f},  // T6 cyan vert
        {0.75f, 0.65f, 0.10f},  // T7 jaune doré
        {0.70f, 0.20f, 0.25f},  // T8 rouge brique
    };
    int idx = (T-1) % 8;
    r = pal[idx][0]; g = pal[idx][1]; b = pal[idx][2];
}
static void toolPastelColor(int T, float &r, float &g, float &b)
{
    // Palettes pastel : bleu poudre, vert sauge, rose, lavande,
    // pêche, menthe, lilas, sable — puis on boucle
    static const float pal[][3] = {
        {0.55f, 0.73f, 0.90f},  // T1 bleu poudre
        {0.60f, 0.82f, 0.68f},  // T2 vert sauge
        {0.92f, 0.68f, 0.72f},  // T3 rose poudré
        {0.76f, 0.70f, 0.90f},  // T4 lavande
        {0.95f, 0.78f, 0.58f},  // T5 pêche
        {0.58f, 0.88f, 0.80f},  // T6 menthe
        {0.82f, 0.70f, 0.88f},  // T7 lilas
        {0.88f, 0.82f, 0.65f},  // T8 sable doré
    };
    int idx = (T > 0) ? ((T-1) % 8) : 0;
    r = pal[idx][0]; g = pal[idx][1]; b = pal[idx][2];
}

// ── Display list : compile le parcours exécuté en un seul bloc GL ──
void Sim3DWidget::rebuildDoneList()
{
    if(m_dlDone==0) m_dlDone=glGenLists(1);
    const int done=qMin(m_curMove+1,(int)m_moves.size());
    glNewList(m_dlDone, GL_COMPILE);
    glDisable(GL_LIGHTING);
    for(int mi=0;mi<done;mi++){
        const SimMove &mv=m_moves[mi];
        const QVector<QVector3D>&p=mv.pts;
        if(p.size()<2) continue;
        if(mv.kind==SimMove::Rapid&&!m_showRapids) continue;
        int sl=(mi==m_curMove)?m_curSeg+2:(int)p.size();
        sl=qMin(sl,(int)p.size());
        if(mv.kind==SimMove::Rapid){
            glLineWidth(3.5f); glEnable(GL_LINE_STIPPLE); glLineStipple(1,0xF0F0);
            glColor4f(1.f,.05f,.05f,1.f);  // rouge vif
        } else {
            // ── Toutes les coupes G1 en BLANC, peu importe l'outil ──
            // (les couleurs ébauche/finition seront réactivées plus tard
            //  quand l'unit contournage générera l'offset Clipper2 et
            //  annotera correctement les passes)
            glDisable(GL_LINE_STIPPLE);
            glLineWidth(3.0f); glColor4f(1.f,1.f,1.f,1.f);
        }
        glBegin(GL_LINE_STRIP);
        for(int s=0;s<sl;s++) glVertex3f(p[s].x(),p[s].y(),p[s].z());
        glEnd();
    }
    glDisable(GL_LINE_STIPPLE); glLineWidth(1.f); glEnable(GL_LIGHTING);
    glEndList();
    m_dlDoneUpTo=m_curMove;
    m_dlDirty=false;
}

void Sim3DWidget::drawToolPath()
{
    if(m_moves.isEmpty()) return;
    glDisable(GL_LIGHTING);
    const int tot=(int)m_moves.size();

    // ── Passe 1 : fantôme — tout le programme visible
    const int ghostStart  = 0;
    const int ghostEnd    = tot;
    for(int mi=ghostStart;mi<ghostEnd;mi++){
        const SimMove &mv=m_moves[mi];
        const QVector<QVector3D>&p=mv.pts;
        if(p.size()<2) continue;
        if(mv.kind==SimMove::Rapid&&!m_showRapids) continue;
        if(mv.kind==SimMove::Rapid){
            glLineWidth(2.5f); glEnable(GL_LINE_STIPPLE); glLineStipple(1,0xF0F0);
            glColor4f(1.f,.05f,.05f,.60f);  // rouge vif semi-transparent
        } else {
            // ── Fantôme G1 en blanc semi-transparent (uniforme) ──
            glDisable(GL_LIGHTING);
            glDisable(GL_LINE_STIPPLE); glLineWidth(1.8f);
            glColor4f(1.f,1.f,1.f,.30f);
        }
        glBegin(GL_LINE_STRIP);
        for(const QVector3D &pt:std::as_const(p)) glVertex3f(pt.x(),pt.y(),pt.z());
        glEnd();
    }
    glDisable(GL_LINE_STIPPLE);

    // ── Passe 2 : parcours exécuté via display list ───────────
    if(m_dlDirty || m_dlDoneUpTo!=m_curMove) rebuildDoneList();
    if(m_dlDone) glCallList(m_dlDone);
}
void Sim3DWidget::drawContours()
{
    if(m_ebaucheContour.isEmpty()&&m_finitionContour.isEmpty()) return;
    glDisable(GL_LIGHTING); glDisable(GL_LINE_STIPPLE);
    auto dc=[&](const QVector<QVector3D>&pts){ if(pts.isEmpty()) return;
        glBegin(GL_LINE_STRIP); for(const QVector3D&p:pts){if(p.x()>1e8f){glEnd();glBegin(GL_LINE_STRIP);continue;} glVertex3f(p.x(),p.y(),p.z());} glEnd(); };
    if(!m_ebaucheContour.isEmpty()){glLineWidth(2.4f);glColor4f(.45f,.78f,.55f,1.f);dc(m_ebaucheContour);}
    if(!m_finitionContour.isEmpty()){glLineWidth(2.4f);glColor4f(.88f,.80f,.45f,1.f);dc(m_finitionContour);}
    glLineWidth(1.f); glEnable(GL_LIGHTING);
}
void Sim3DWidget::drawTool()
{
    if(m_moves.isEmpty()) return;
    // Outil toujours affiché, même hors brut XY
    // Fin de programme : on garde le dernier outil (avant, on retombait
    // sur l'outil 0 = rayon par défaut, d'où l'outil qui rapetissait)
    int mi = qMin(m_curMove, (int)m_moves.size()-1);
    // Les derniers déplacements (dégagement après la fin) peuvent ne plus
    // porter de numéro d'outil : on remonte jusqu'au dernier outil connu
    while (mi > 0 && m_moves[mi].toolT <= 0) --mi;
    int toolT = m_moves[mi].toolT;
    ToolInfo ti=resolveToolInfo(toolT);

    // ── Couleur outil 3D : gris uniforme, peu importe l'outil ──
    // Volonté UI : tous les outils s'affichent identiques (corps gris).
    // Seule la matière retirée change de couleur selon l'outil
    // (cf toolFaceColor dans getColor du heightmap).
    // Rouge : la fraise enlève de la matière (ou rapide en collision)
    // Vert  : la fraise est dans le vide
    // À l'arrêt (pause / fin de programme) : on teste la position réelle,
    // sinon l'état de la dernière image restait affiché (fraise rouge à Z50)
    const bool touching = m_playing
        ? (m_rapidCollisionShown ||
           (m_lastCutClock.isValid() && m_lastCutClock.elapsed() < 150))
        : toolBelowSurface(m_toolPos, toolT);
    const float pr = touching ? 0.90f : 0.25f;
    const float pg = touching ? 0.22f : 0.72f;
    const float pb = touching ? 0.20f : 0.32f;

    glPushMatrix(); glTranslatef(m_toolPos.x(),m_toolPos.y(),m_toolPos.z());
    glEnable(GL_LIGHTING);
    GLfloat sp[]={.9f,.9f,.9f,1.f};
    glMaterialfv(GL_FRONT,GL_SPECULAR,sp);
    glMaterialf(GL_FRONT,GL_SHININESS,55.f);

    // Géométrie selon type d'outil (depuis ToolDatabase via resolveToolInfo)
    // ToolInfo.r2 : 0=fraise plate, >0=bull-nose, ≈r=ball-nose, -1=foret
    const float r   = ti.r;
    const float len = ti.len;
    const float r2  = ti.r2;

    glColor4f(pr,pg,pb,.90f);

    if(r2 < -0.5f){
        // ── Pointe conique (foret 118°, fraise à chanfreiner 90°...) ──
        //    rayon ti.tipR à Z=0, rayon r à Z=coneH, puis cylindre
        const float tipR  = qMin(ti.tipR, r);
        const float coneH = (r - tipR) / qMax(0.05f, ti.coneTan);
        const int Nc=32;
        float nl=sqrtf(coneH*coneH+(r-tipR)*(r-tipR)); if(nl<1e-6f) nl=1.f;
        glBegin(GL_TRIANGLE_STRIP);
        for(int i=0;i<=Nc;i++){
            float a=float(i)/Nc*2.f*float(M_PI);
            float ca=cosf(a),sa=sinf(a);
            glNormal3f(coneH/nl*ca,coneH/nl*sa,-(r-tipR)/nl);
            glVertex3f(tipR*ca,tipR*sa,0.f);
            glVertex3f(r*ca,r*sa,coneH);
        }
        glEnd();
        if (tipR > 0.01f) { glColor4f(pr*.85f,pg*.85f,pb*.85f,.95f); drawDisk(tipR); glColor4f(pr,pg,pb,.90f); }
        // Cylindre de Z=coneH à Z=len
        if(len > coneH){
            glPushMatrix(); glTranslatef(0,0,coneH);
            drawCylinder(r, len - coneH);
            glPopMatrix();
        }
        // Disk de fermeture au sommet
        glPushMatrix(); glTranslatef(0,0,len);
        glColor4f(pr*.85f,pg*.85f,pb*.85f,.95f); drawDisk(r);
        glPopMatrix();
    } else if(r2 >= r * 0.85f){
        // ── Ball-nose : hémisphère (tip Z=0) + cylindre (Z=r2 → Z=len) ──
        // La POINTE de la bille est à Z=0 (point de contact pièce).
        // Le centre de la bille est à Z=r2.
        // L'équateur est à Z=r2 (transition bille→cylindre).
        const int Ns=20, Nc2=32;
        // Cylindre de Z=r2 à Z=len
        if(len > r2){
            glPushMatrix(); glTranslatef(0,0,r2);
            drawCylinder(r, len - r2);
            glPopMatrix();
        }
        // Disque de fermeture au sommet (Z=len)
        glPushMatrix(); glTranslatef(0,0,len);
        glColor4f(pr*.80f,pg*.80f,pb*.80f,.95f); drawDisk(r);
        glPopMatrix();
        glColor4f(pr,pg,pb,.90f);
        // Hémisphère : de phi=0 (tip Z=0) à phi=pi/2 (équateur Z=r2)
        // Centre à (0,0,r2), rayon=r2
        // Vertex : (r2*sin(phi)*cos(th), r2*sin(phi)*sin(th), r2 - r2*cos(phi))
        //   phi=0   → (0,0,0)   = tip ✓
        //   phi=pi/2 → (r2*cos,r2*sin, r2) = équateur ✓
        glBegin(GL_TRIANGLE_STRIP);
        for(int j=0;j<Ns;j++){
            float phi0 = float(j)  /Ns * float(M_PI/2.0);
            float phi1 = float(j+1)/Ns * float(M_PI/2.0);
            float s0=sinf(phi0), c0=cosf(phi0);
            float s1=sinf(phi1), c1=cosf(phi1);
            for(int i=0;i<=Nc2;i++){
                float th=float(i)/Nc2*2.f*float(M_PI);
                float ct=cosf(th),st=sinf(th);
                // Normale pointe vers l'extérieur de la sphère
                glNormal3f(s0*ct, s0*st, -c0);
                glVertex3f(r2*s0*ct, r2*s0*st, r2*(1.f-c0));
                glNormal3f(s1*ct, s1*st, -c1);
                glVertex3f(r2*s1*ct, r2*s1*st, r2*(1.f-c1));
            }
        }
        glEnd();
    } else {
        // ── Fraise plate : cylindre de Z=0 à Z=len, faces à Z=0 et Z=len ──
        drawCylinder(r, len);
        // Face de coupe (bas, Z=0)
        glColor4f(pr*.85f,pg*.85f,pb*.85f,.95f); drawDisk(r);
        // Fermeture sommet (Z=len)
        glPushMatrix(); glTranslatef(0,0,len);
        glColor4f(pr*.80f,pg*.80f,pb*.80f,.95f); drawDisk(r);
        glPopMatrix();
    }

    // Anneau de contact (cercle au niveau Z=0 — contact avec la pièce)
    glDisable(GL_LIGHTING);
    float ar=pr*.7f+.15f, ag=pg*.7f+.15f, ab=pb*.7f+.15f;
    glColor4f(ar,ag,ab,1.f); glLineWidth(1.8f);
    glBegin(GL_LINE_LOOP);
    for(int i=0;i<48;i++){
        float a=float(i)/48*2.f*float(M_PI);
        glVertex3f(r*cosf(a),r*sinf(a),0.f);
    }
    glEnd();
    glLineWidth(1.f); glEnable(GL_LIGHTING); glPopMatrix();
}
void Sim3DWidget::drawCylinder(float r,float len)
{
    const int N=24; glBegin(GL_TRIANGLE_STRIP);
    for(int i=0;i<=N;i++){float a=float(i)/N*2.f*float(M_PI),ca=cosf(a),sa=sinf(a);
        glNormal3f(ca,sa,0.f);glVertex3f(r*ca,r*sa,0.f);glVertex3f(r*ca,r*sa,len);}
    glEnd(); glNormal3f(0,0,1); glBegin(GL_TRIANGLE_FAN); glVertex3f(0,0,len);
    for(int i=0;i<=N;i++){float a=float(i)/N*2.f*float(M_PI);glVertex3f(r*cosf(a),r*sinf(a),len);} glEnd();
}
void Sim3DWidget::drawDisk(float r)
{
    const int N=24; glNormal3f(0,0,-1); glBegin(GL_TRIANGLE_FAN); glVertex3f(0,0,0);
    for(int i=0;i<=N;i++){float a=float(i)/N*2.f*float(M_PI);glVertex3f(r*cosf(a),r*sinf(a),0.f);} glEnd();
}
QVector3D Sim3DWidget::colorForZ(float z,float zMin,float zMax)
{
    // Gradient pastel doux : surface (blanc bleuté) → profondeur (bleu-vert pastel)
    // Complète la couleur de l'outil qui est mixée à 65%
    float t=qAbs(zMax-zMin)<0.001f?0.f:qBound(0.f,(zMax-z)/(zMax-zMin),1.f);
    float r = .92f*(1-t)*(1-t) + 2*.70f*t*(1-t) + .50f*t*t;
    float g = .95f*(1-t)*(1-t) + 2*.80f*t*(1-t) + .65f*t*t;
    float b = .98f*(1-t)*(1-t) + 2*.88f*t*(1-t) + .78f*t*t;
    return {r, g, b};
}
ToolInfo Sim3DWidget::resolveToolInfo(int T) const
{
    ToolInfo ti; ti.T=T;
    ti.r = float(m_rDefault); ti.len = ti.r * 10.f; ti.r2 = 0.f;
    const ToolData *td = ToolDatabase::instance().toolByT(T);
    if(td){
        if(td->Routil  > 0.01) ti.r   = float(td->Routil);
        if(td->Lu > 0.1) ti.len = float(td->Lu);
        else ti.len = ti.r * 10.f;
        // ── Géométrie de la pointe depuis toolType + Routil2 ──────────
        // Convention Routil2 dans le tableau outil EazyNC :
        //   Routil2 = 0            → fraise plane (fond plat)
        //   0 < Routil2 < Routil   → bull-nose (rayon de bec)
        //   Routil2 >= Routil      → ball-nose (Routil2 = rayon ou diamètre bille)
        //                        l'utilisateur peut entrer le diamètre outil
        //                        → on clamp à r (bille = demi-sphère complète)
        switch(td->toolType){
        case ToolData::Foret:
            ti.r2 = -1.f;                           // foret : cône 118°
            break;
        case ToolData::Conique: {                   // fraise à chanfreiner / pointe
            ti.r2 = -1.f;
            const double ang = (td->AnglePointe > 1.0 && td->AnglePointe < 179.0)
                                   ? td->AnglePointe : 90.0;
            ti.coneTan = float(std::tan(ang * 0.5 * M_PI / 180.0));
            ti.tipR    = float(qBound(0.0, td->rayonPointe, td->Routil));
            break;
        }
        case ToolData::Boule:
            ti.r2 = ti.r;                           // ball-nose : bille complète
            break;
        case ToolData::Fraise2T:
            ti.r2 = 0.f;                            // fraise 2 tailles : fond plat
            break;
        default:{
            float dr2 = float(td->Routil2);
            if(dr2 >= ti.r * 0.85f)
                ti.r2 = ti.r;                       // ball-nose (Routil2 ≥ r → bille complète)
            else if(dr2 > 0.01f)
                ti.r2 = dr2;                        // bull-nose (rayon de bec)
            else
                ti.r2 = 0.f;                        // fraise plane
            break;
        }
        }
    }
    return ti;
}

// ═══════════════════════════════════════════════════════════════
//  Souris
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::mousePressEvent(QMouseEvent *e)
{
    if(m_measureMode&&e->button()==Qt::LeftButton){
        QVector3D pt=pickSurface(e->pos());
        if(pt.isNull()){m_lastMouse=e->pos();return;}
        if(m_measurePts.size()>=2) m_measurePts.clear();
        m_measurePts.append(pt); emit measureResult(formatMeasure()); detectFace(pt); update(); return;
    }
    m_lastMouse=e->pos();
    if(e->button()==Qt::LeftButton)  m_orbiting=true;
    if(e->button()==Qt::RightButton) m_panning=true;
    // Pendant qu'on tourne la vue : la simu se met en pause pour laisser
    // toute la puissance à l'affichage, et repart au relâchement.
    if((m_orbiting||m_panning) && m_playing && m_animTimer->isActive()){
        m_animTimer->stop();
        m_mousePaused = true;
    }
}
void Sim3DWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_measureMode && !m_orbiting && !m_panning) updateHover(e->pos());
    QPoint d=e->pos()-m_lastMouse; m_lastMouse=e->pos();
    if(m_orbiting){m_azimuth-=d.x()*.5f;m_elevation+=d.y()*.5f;m_elevation=qBound(-89.f,m_elevation,89.f);update();}
    if(m_panning){
        float az=qDegreesToRadians(m_azimuth),el=qDegreesToRadians(m_elevation);
        QVector3D right(-sinf(az),cosf(az),0.f),up(-sinf(el)*cosf(az),-sinf(el)*sinf(az),cosf(el));
        float sc=m_dist/qMax(height(),1)*2.f;
        m_pivot-=right*(d.x()*sc); m_pivot+=up*(d.y()*sc); update();
    }
}
void Sim3DWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_orbiting=false; m_panning=false;
    if(m_mousePaused){
        m_mousePaused=false;
        if(m_playing) m_animTimer->start();   // la simu reprend où elle était
    }
    update();                                  // repasse en pleine résolution
}
void Sim3DWidget::mouseDoubleClickEvent(QMouseEvent*){fitAll();}
void Sim3DWidget::wheelEvent(QWheelEvent *e)
{m_dist=qMax(minZoomDist(),m_dist*(e->angleDelta().y()>0?.85f:1.15f));rebuildProjection();update();}

// ═══════════════════════════════════════════════════════════════
//  Tessellation arcs
// ═══════════════════════════════════════════════════════════════
bool Sim3DWidget::parseParam(const QString &code,QChar name,double &out)
{
    // Supprimer les commentaires ( ... ) avant de chercher le paramètre
    QString stripped = code;
    { int s=stripped.indexOf('(');
        while(s>=0){ int e=stripped.indexOf(')',s);
            if(e<0) { stripped=stripped.left(s); break; }
            stripped.remove(s,e-s+1); s=stripped.indexOf('('); } }
    QString up = stripped.toUpper();
    QChar un = name.toUpper();
    int idx = -1;
    for (int i = 0; i < up.size(); i++) if (up[i] == un) { idx = i; break; }
    if (idx < 0) return false;
    int j = idx + 1;
    while (j < up.size() && up[j] == ' ') ++j;
    int st = j;
    if (j < up.size() && (up[j] == '-' || up[j] == '+')) ++j;
    while (j < up.size() && (up[j].isDigit() || up[j] == '.')) ++j;
    if (j == st + (st < up.size() && (up[st] == '-' || up[st] == '+') ? 1 : 0)) return false;
    if (st >= stripped.size() || j > stripped.size() || j < st) return false;
    bool ok; out = stripped.mid(st, j - st).toDouble(&ok); return ok;
}
QVector<QVector3D> Sim3DWidget::tessArc(QVector3D from,QVector3D to,double cx,double cy,bool cw,int steps)
{
    QVector<QVector3D> pts; pts.reserve(steps+1);
    double r=sqrt(pow(from.x()-cx,2)+pow(from.y()-cy,2));
    double a0=atan2(from.y()-cy,from.x()-cx),a1=atan2(to.y()-cy,to.x()-cx);
    if(cw){if(a1>=a0)a1-=2*M_PI;}else{if(a1<=a0)a1+=2*M_PI;}
    double span=a1-a0; pts.append(from);
    for(int i=1;i<steps;i++){double t=double(i)/steps,a=a0+span*t;float z=from.z()+(to.z()-from.z())*float(t);
        pts.append(QVector3D(float(cx+r*cos(a)),float(cy+r*sin(a)),z));}
    pts.append(to); return pts;
}
QVector<QVector3D> Sim3DWidget::tessArcR(QVector3D from,QVector3D to,double R,bool cw,int steps)
{
    double dx=to.x()-from.x(),dy=to.y()-from.y(),d2=dx*dx+dy*dy;
    if(d2<1e-12) return {from,to};
    double r=qAbs(R),h=sqrt(qMax(0.0,r*r-d2/4.0));
    bool longArc=(R<0); double mx=(from.x()+to.x())/2,my=(from.y()+to.y())/2;
    double nx2=-dy/sqrt(d2),ny2=dx/sqrt(d2),cx,cy;
    bool flip=(cw!=longArc); if(flip){cx=mx-h*nx2;cy=my-h*ny2;}else{cx=mx+h*nx2;cy=my+h*ny2;}
    return tessArc(from,to,cx,cy,cw,steps);
}
// ── tessArcPlane — G17/G18/G19 ────────────────────────────────
QVector<QVector3D> Sim3DWidget::tessArcPlane(QVector3D from,QVector3D to,
                                             double ci,double cj,double ck,
                                             bool cw,int plane,int steps)
{
    QVector<QVector3D> pts; pts.reserve(steps+1);
    if(plane==17) return tessArc(from,to,from.x()+ci,from.y()+cj,cw,steps);
    if(plane==18){  // ZX : axes=X,Z
        double cx3=from.x()+ci,cz3=from.z()+ck;
        double r=sqrt(pow(from.x()-cx3,2)+pow(from.z()-cz3,2));
        double a0=atan2(from.z()-cz3,from.x()-cx3),a1=atan2(to.z()-cz3,to.x()-cx3);
        bool aCW=!cw; if(aCW){if(a1>=a0)a1-=2*M_PI;}else{if(a1<=a0)a1+=2*M_PI;}
        double span=a1-a0; pts.append(from);
        for(int i=1;i<steps;i++){double t=double(i)/steps,a=a0+span*t;float y=from.y()+(to.y()-from.y())*float(t);
            pts.append(QVector3D(float(cx3+r*cos(a)),y,float(cz3+r*sin(a))));}
        pts.append(to); return pts;
    }
    if(plane==19){  // YZ : axes=Y,Z
        double cy3=from.y()+cj,cz3=from.z()+ck;
        double r=sqrt(pow(from.y()-cy3,2)+pow(from.z()-cz3,2));
        double a0=atan2(from.z()-cz3,from.y()-cy3),a1=atan2(to.z()-cz3,to.y()-cy3);
        bool aCW=!cw; if(aCW){if(a1>=a0)a1-=2*M_PI;}else{if(a1<=a0)a1+=2*M_PI;}
        double span=a1-a0; pts.append(from);
        for(int i=1;i<steps;i++){double t=double(i)/steps,a=a0+span*t;float x=from.x()+(to.x()-from.x())*float(t);
            pts.append(QVector3D(x,float(cy3+r*cos(a)),float(cz3+r*sin(a))));}
        pts.append(to); return pts;
    }
    return {from,to};
}

// ═══════════════════════════════════════════════════════════════
//  buildMoves — via GCodeParserFreeCADCompatible
// ═══════════════════════════════════════════════════════════════
static QVector3D perpLeft2D(const QVector3D &f,const QVector3D &t)
{
    float dx=t.x()-f.x(),dy=t.y()-f.y(),l=sqrtf(dx*dx+dy*dy);
    if(l<1e-6f) return {0,0,0}; return {-dy/l,dx/l,0.f};
}
static QVector<QVector3D> applyRadiusComp(const QVector<QVector3D>&pts,float r,bool left)
{
    if(pts.size()<2||r<0.01f) return pts;
    QVector<QVector3D> out; out.reserve(pts.size());
    for(int i=0;i<pts.size();i++){
        QVector3D off{0,0,0};int cnt=0;
        if(i>0){auto p=perpLeft2D(pts[i-1],pts[i]);if(!left)p=-p;off+=p;cnt++;}
        if(i<pts.size()-1){auto p=perpLeft2D(pts[i],pts[i+1]);if(!left)p=-p;off+=p;cnt++;}
        if(cnt>0){off/=float(cnt);float l=sqrtf(off.x()*off.x()+off.y()*off.y());if(l>0.01f)off=off/l;}
        out.append(pts[i]+off*r);
    }
    return out;
}
void Sim3DWidget::buildMoves(const QVector<GCodeLine> &lines,double rDefault)
{
    invalidateCheckpoints();
    m_moves.clear(); m_ebaucheContour.clear(); m_finitionContour.clear();

    GCodeParserFreeCADCompatible parser;
    parser.parse(lines,rDefault);
    const auto &parsed=parser.moves();

    for(int li=0;li<parsed.size();++li){
        const GCodeParserFreeCADCompatible::ParsedMove &pm=parsed[li];
        // Pour li=0 : utiliser la position après les G0 initiaux skippés
        // (pas (0,0,0) qui cause la diagonale parasite en simulation)
        double x0=(li>0)?parsed[li-1].x:parser.initialX();
        double y0=(li>0)?parsed[li-1].y:parser.initialY();
        double z0=(li>0)?parsed[li-1].z:parser.initialZ();
        bool isArc=(pm.kind==GCodeParserFreeCADCompatible::ParsedMove::ArcCW||
                      pm.kind==GCodeParserFreeCADCompatible::ParsedMove::ArcCCW);
        bool isCW =(pm.kind==GCodeParserFreeCADCompatible::ParsedMove::ArcCW);
        QVector3D ptF{(float)x0,(float)y0,(float)z0};
        QVector3D ptT{(float)pm.x,(float)pm.y,(float)pm.z};

        SimMove mv;
        mv.kind=(pm.kind==GCodeParserFreeCADCompatible::ParsedMove::Rapid)?SimMove::Rapid:SimMove::Cut;
        mv.zMid=float((z0+pm.z)/2.0); mv.toolT=pm.toolT; mv.lineIdx=pm.lineIdx; mv.passColor=pm.passColor;

        if(isArc){
            mv.kind=SimMove::Cut;
            // Nombre de pas selon la LONGUEUR de l'arc (corde ≤ 0,25 mm).
            // Avant : 64 pas fixes -> un cercle Ø40 devenait un polygone de
            // 64 facettes de 2 mm, chacune éclairée différemment = rayures
            // verticales (effet crémaillère) sur les parois rondes.
            int steps = 64;
            {
                double r = 0.0, span = 2.0 * M_PI;
                if (pm.hasR && qAbs(pm.r) > 1e-6) {
                    r = qAbs(pm.r);
                    const double c = std::hypot(ptT.x() - ptF.x(), ptT.y() - ptF.y());
                    span = 2.0 * std::asin(qMin(1.0, c / (2.0 * r)));
                    if (pm.r < 0) span = 2.0 * M_PI - span;
                } else if (pm.plane == 17) {
                    const double cx = ptF.x() + pm.i, cy = ptF.y() + pm.j;
                    r = std::hypot(pm.i, pm.j);
                    double a0 = std::atan2(ptF.y() - cy, ptF.x() - cx);
                    double a1 = std::atan2(ptT.y() - cy, ptT.x() - cx);
                    if (isCW) { if (a1 >= a0) a1 -= 2.0 * M_PI; }
                    else      { if (a1 <= a0) a1 += 2.0 * M_PI; }
                    span = std::abs(a1 - a0);
                } else {
                    r = std::sqrt(pm.i*pm.i + pm.j*pm.j + pm.k*pm.k);
                }
                steps = qBound(8, int(std::ceil(span * r / 0.25)), 4000);
            }
            if(pm.hasR&&qAbs(pm.r)>1e-6) mv.pts=tessArcR(ptF,ptT,pm.r,isCW,steps);
            else mv.pts=tessArcPlane(ptF,ptT,pm.i,pm.j,pm.k,isCW,pm.plane,steps);
        } else mv.pts={ptF,ptT};

        // EazyNC génère du G-code profil pièce avec G41/G42.
        // Le CNC Centroid applique lui-même la CRC → pas de double compensation
        // dans le G-code. Pour la SIMULATION seule, on doit recalculer le
        // chemin réel du centre outil, sinon le rayon affiché est celui de la
        // pièce (nominal) et non celui parcouru par la fraise.
        // rawPts = coords brutes (profil programmé) pour affichage contours
        // verts/jaunes existants ; mv.pts devient le VRAI centre outil.
        // Fonctionne pour tout type d'élément (arc ou ligne) car
        // applyRadiusComp() travaille uniquement sur les points tessellés.
        if(pm.compMode!=0 && pm.compR>0.05){
            mv.rawPts = mv.pts;
            mv.pts = applyRadiusComp(mv.pts, float(pm.compR), pm.compMode==41);
        }
        if(mv.pts.size()>=2) m_moves.append(mv);
    }

    qDebug()<<"[Sim3D] buildMoves:"<<m_moves.size()<<"moves";  // log réduit

    // Post-process 1a : aligner Rapid→Cut
    for(int mi=0;mi<m_moves.size()-1;mi++)
        if(m_moves[mi].kind==SimMove::Rapid&&m_moves[mi+1].kind==SimMove::Cut
            &&!m_moves[mi].pts.isEmpty()&&!m_moves[mi+1].pts.isEmpty())
            m_moves[mi].pts.last()=m_moves[mi+1].pts.first();

    // Post-process 1b : coins G41
    for(int mi=0;mi<m_moves.size()-1;mi++){
        SimMove &m0=m_moves[mi]; SimMove &m1=m_moves[mi+1];
        if(m0.kind!=SimMove::Cut||m1.kind!=SimMove::Cut||m0.pts.size()<2||m1.pts.size()<2) continue;
        const QVector3D &A=m0.pts[m0.pts.size()-2],&B=m0.pts.last(),&C=m1.pts.first(),&D=m1.pts[1];
        float gap=(B-C).length(); if(gap<0.05f||gap>30.f) continue;
        QVector2D d1(B.x()-A.x(),B.y()-A.y()),d2(D.x()-C.x(),D.y()-C.y());
        float d1l=d1.length(),d2l=d2.length(); if(d1l<1e-6f||d2l<1e-6f) continue;
        d1/=d1l; d2/=d2l;
        float det=d1.x()*(-d2.y())-d1.y()*(-d2.x()); if(qAbs(det)<1e-6f) continue;
        float dxx=C.x()-B.x(),dyy=C.y()-B.y(),t=(dxx*(-d2.y())-dyy*(-d2.x()))/det;
        float ix=B.x()+t*d1.x(),iy=B.y()+t*d1.y();
        if(qSqrt((ix-B.x())*(ix-B.x())+(iy-B.y())*(iy-B.y()))>gap*3.f) continue;
        QVector3D inter(ix,iy,B.z()); m0.pts.last()=inter; m1.pts.first()=inter;
    }

    // Post-process 2 : contours permanents vert/jaune
    float lzE=1e9f,lzF=1e9f;
    for(const SimMove &mv:std::as_const(m_moves)){
        if(mv.kind==SimMove::Rapid) continue;
        const QVector<QVector3D>&src=mv.rawPts.isEmpty()?mv.pts:mv.rawPts;
        if(src.isEmpty()) continue;
        float z=src.first().z();
        if(mv.passColor==PassColor::EbaucheFinale){
            if(!m_ebaucheContour.isEmpty()&&qAbs(z-lzE)>0.05f) m_ebaucheContour.append(QVector3D(1e9f,1e9f,1e9f));
            m_ebaucheContour.append(src); lzE=z;
        } else if(mv.passColor==PassColor::FinitionFinale){
            if(!m_finitionContour.isEmpty()&&qAbs(z-lzF)>0.05f) m_finitionContour.append(QVector3D(1e9f,1e9f,1e9f));
            m_finitionContour.append(src); lzF=z;
        }
    }
    // Position initiale : au-dessus du brut (zMax + longueur outil)
    // pour que l'outil soit visible arrivant d'en haut
    {
        float zTop = m_stock.valid ? m_stock.zMax : 0.f;
        float toolLen = 30.f;
        if(!m_moves.isEmpty()){
            int T = m_moves.isEmpty() ? 0 : m_moves.front().toolT;
            ToolInfo ti = resolveToolInfo(T);
            toolLen = ti.len;
        }
        m_toolPos = QVector3D(0.f, 0.f, zTop + toolLen + 20.f);
    }
    setSpeed(m_speed);
}

// ═══════════════════════════════════════════════════════════════
//  Heightmap async
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::buildHeightmapAsync(int upToArg, int res)
{
    if(!m_stock.valid) return;
    stopBuild();
    m_buildPending = false;                     // ce calcul remplace celui qui attendait
    m_buildRes = (res > 1) ? res : m_hmapRes;   // par défaut : grille fine
    m_hmapPending=true; m_hmapProgress.store(0); emit progressChanged(0); m_progressTimer->start();
    int upTo = (upToArg != -2) ? upToArg
                               : (m_playing ? m_curMove : -1);  // heightmap suit le move courant
    m_hmapWatcher->setFuture(QtConcurrent::run([this,upTo]{buildHeightmapSync(upTo);}));
}
void Sim3DWidget::buildHeightmapSync(int upTo)
{
    if(!m_stock.valid||m_buildRes<2) return;
    // Construire entièrement dans m_hmapBack (thread pool uniquement)
    // Jamais de race : paintGL lit m_hmap, buildHeightmapSync écrit m_hmapBack
    // Résolution : m_buildRes (grille légère d'animation ou grille fine)
    m_hmapBack.res = m_buildRes;
    m_hmapBack.init(m_stock.xMin,m_stock.xMax,m_stock.yMin,m_stock.yMax,m_stock.zMax,m_stock.zMin);
    int limit = (upTo == -1) ? (int)m_moves.size() : qMax(0, qMin(upTo+1, (int)m_moves.size()));
    // Point de sauvegarde le plus proche (grille légère) : on repart de
    // là au lieu de tout recalculer depuis le brut → recherche instantanée
    int startMi = 0;
    if (m_buildRes == m_hmapRes && limit == m_moves.size() && limit > 0) {
        QMutexLocker ck(&m_ckptMutex);
        if (m_finalFineValid && m_finalFine.res == m_buildRes) { m_hmapBack = m_finalFine; startMi = limit; }
    }
    if (startMi == 0 && m_buildRes == m_ckptRes && limit > 0) {
        QMutexLocker ck(&m_ckptMutex);
        const HmCkpt *best = nullptr;
        for (const HmCkpt &c : m_ckpts) if (c.next <= limit && (!best || c.next > best->next)) best = &c;
        if (best) { m_hmapBack = best->hm; startMi = best->next; }
    }
    int total=0;
    for(int mi=startMi;mi<limit;mi++) if(m_moves[mi].kind==SimMove::Cut) total+=m_moves[mi].pts.size()-1;
    int done=0;
    for(int mi=startMi;mi<limit;mi++){
        if (m_buildCancel.load(std::memory_order_relaxed)) { return; }   // calcul abandonné
        const SimMove &mv=m_moves[mi];
        if(mv.kind!=SimMove::Cut) continue;
        ToolInfo ti=resolveToolInfo(mv.toolT);
        // Coupe exacte de chaque segment en une passe (plus de tampons
        // de disques, donc plus de « tenons fantômes » à filtrer)
        for(int i=1;i<mv.pts.size();i++){
            m_hmapBack.cutSegment(mv.pts[i-1], mv.pts[i], ti, mv.toolT);
            ++done;
            if ((done & 63) == 0 && m_buildCancel.load(std::memory_order_relaxed)) { return; }
        }
        if(total>0) m_hmapProgress.store(done*100/total);
    }

    // Morceau du déplacement en cours, jusqu'à la position de l'outil
    // (sinon une bande de matière resterait derrière lui à la reprise)
    if (m_bpMove >= 0 && m_bpMove < m_moves.size() && limit == m_bpMove) {
        const SimMove &mv = m_moves[m_bpMove];
        if (mv.kind == SimMove::Cut && mv.pts.size() >= 2) {
            ToolInfo ti = resolveToolInfo(mv.toolT);
            const int sMax = qMin(m_bpSeg, int(mv.pts.size()) - 1);
            for (int i = 1; i <= sMax; ++i)
                m_hmapBack.cutSegment(mv.pts[i-1], mv.pts[i], ti, mv.toolT);
            if (sMax < mv.pts.size())
                m_hmapBack.cutSegment(mv.pts[sMax], m_bpPos, ti, mv.toolT);
        }
    }
    m_bpMove = -1;
    // Pièce finie calculée en grille fine : on la garde pour « Fin »
    if (m_buildRes == m_hmapRes && limit == m_moves.size() && startMi < limit) {
        QMutexLocker ck(&m_ckptMutex);
        m_finalFine = m_hmapBack; m_finalFineValid = true;
    }

    // Swap atomique : back → front sous mutex (très court)
    QMutexLocker lk(&m_hmapMutex);
    std::swap(m_hmap, m_hmapBack);
    ++m_matVer;
    m_hmap.markAllDirty();                // toute la texture est à renvoyer
    m_hmapNormalsDirty = true;
    m_hmapVBODirty     = true;
    m_lastIncrPos      = QVector3D();   // reset : prochain incrémental partira de m_toolPos
    m_lastIncrValid    = false;
}

// ═══════════════════════════════════════════════════════════════
//  applyToolIncremental — enlèvement de matière en temps réel
//
//  Appelée depuis advanceAnimation() à chaque tick d'animation,
//  pour creuser la matière sous l'outil EXACTEMENT à sa position
//  actuelle (synchro parfaite outil ↔ matière).
//
//  Sample le segment [m_lastIncrPos → curPos] avec un pas suffisant
//  pour ne sauter aucune cellule, même à grande vitesse de simu.
//  Coût constant : ~10–50 cellules touchées par tick, peu importe
//  où on en est dans le programme.
//
//  Si m_lastIncrValid == false (premier appel ou après rebuild
//  global), on applique seulement le point courant.
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::applyToolIncremental(const QVector3D &curPos, int toolT)
{
    if (!m_stock.valid || m_hmap.data.empty() || m_hmap.res < 2) return;

    ToolInfo ti = resolveToolInfo(toolT);
    if (ti.r <= 0.f) return;

    QElapsedTimer perf; perf.start();
    QMutexLocker lk(&m_hmapMutex);

    // Coupe du morceau parcouru depuis le dernier point, en une passe
    const QVector3D a = m_lastIncrValid ? m_lastIncrPos : curPos;
    if (m_hmap.cutSegment(a, curPos, ti, toolT))
        m_lastCutClock.start();                   // la fraise est dans la matière
    m_perfCutNs += perf.nsecsElapsed();

    m_lastIncrPos   = curPos;
    m_lastIncrValid = true;
    ++m_matVer;
    m_hmapNormalsDirty = true;
    m_hmapVBODirty     = true;
}

// ═══════════════════════════════════════════════════════════════
//  Mesure 3D
// ═══════════════════════════════════════════════════════════════
QVector3D Sim3DWidget::pickSurface(const QPoint &sp) const
{
    if(!m_stock.valid||m_hmap.data.empty()||m_hmap.res<2) return {};
    float nX=2.f*sp.x()/float(width())-1.f,nY=-2.f*sp.y()/float(height())+1.f;
    QMatrix4x4 inv=(m_proj*m_view).inverted();
    QVector4D nH=inv*QVector4D(nX,nY,-1.f,1.f),fH=inv*QVector4D(nX,nY,1.f,1.f);
    if(qAbs(nH.w())<1e-6f||qAbs(fH.w())<1e-6f) return {};
    QVector3D rayO=nH.toVector3D()/nH.w(),rayD=(fH.toVector3D()/fH.w())-rayO; rayD.normalize();
    float tMax=m_dist*3.f,step=qMin((m_stock.xMax-m_stock.xMin)/float(m_hmap.res),.5f),prevT=0.f;
    bool hit=false; float hitT=0.f;
    for(int _ti=1;;++_ti){ float t=_ti*step; if(t>=tMax) break;
        QVector3D p=rayO+rayD*t;
        if(p.x()<m_stock.xMin||p.x()>m_stock.xMax||p.y()<m_stock.yMin||p.y()>m_stock.yMax){prevT=t;continue;}
        float fx=(p.x()-m_hmap.xMin)/(m_hmap.xMax-m_hmap.xMin)*(m_hmap.res-1);
        float fy=(p.y()-m_hmap.yMin)/(m_hmap.yMax-m_hmap.yMin)*(m_hmap.res-1);
        int ix=qBound(0,int(fx),m_hmap.res-2),iy=qBound(0,int(fy),m_hmap.res-2);
        float tx=fx-ix,ty=fy-iy;
        float hz=m_hmap.safe(ix,iy)*(1-tx)*(1-ty)+m_hmap.safe(ix+1,iy)*tx*(1-ty)
                   +m_hmap.safe(ix,iy+1)*(1-tx)*ty+m_hmap.safe(ix+1,iy+1)*tx*ty;
        if(p.z()<=hz){ float lo=prevT,hi=t;
            for(int bi=0;bi<8;bi++){ float mid=(lo+hi)*.5f; QVector3D pm=rayO+rayD*mid;
                float fxm=(pm.x()-m_hmap.xMin)/(m_hmap.xMax-m_hmap.xMin)*(m_hmap.res-1);
                float fym=(pm.y()-m_hmap.yMin)/(m_hmap.yMax-m_hmap.yMin)*(m_hmap.res-1);
                int ixm=qBound(0,int(fxm),m_hmap.res-2),iym=qBound(0,int(fym),m_hmap.res-2);
                float txm=fxm-ixm,tym=fym-iym;
                float hzm=m_hmap.safe(ixm,iym)*(1-txm)*(1-tym)+m_hmap.safe(ixm+1,iym)*txm*(1-tym)
                            +m_hmap.safe(ixm,iym+1)*(1-txm)*tym+m_hmap.safe(ixm+1,iym+1)*txm*tym;
                if(pm.z()<=hzm)hi=mid;else lo=mid; }
            hitT=(lo+hi)*.5f;hit=true;break; }
        prevT=t; }
    if(!hit) return {}; return rayO+rayD*hitT;
}
void Sim3DWidget::drawMeasurePoints()
{
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
    for(int i=0;i<m_measurePts.size();i++){
        const QVector3D &p=m_measurePts[i]; float s=m_dist*.015f;
        glLineWidth(2.5f); glColor4f(1.f,.9f,0.f,1.f);
        glBegin(GL_LINES);
        glVertex3f(p.x()-s,p.y(),p.z());glVertex3f(p.x()+s,p.y(),p.z());
        glVertex3f(p.x(),p.y()-s,p.z());glVertex3f(p.x(),p.y()+s,p.z());
        glVertex3f(p.x(),p.y(),p.z()-s);glVertex3f(p.x(),p.y(),p.z()+s); glEnd();
        glColor4f(1.f,.7f,0.f,.85f);glLineWidth(1.5f);
        glBegin(GL_LINE_LOOP); for(int k=0;k<24;k++){float a=float(k)/24*2.f*float(M_PI);glVertex3f(p.x()+s*cosf(a)*.7f,p.y()+s*sinf(a)*.7f,p.z());} glEnd();
    }
    if(m_measurePts.size()==2){
        glLineWidth(2.f);glColor4f(0.f,1.f,1.f,.9f);glEnable(GL_LINE_STIPPLE);glLineStipple(2,0xF0F0);
        glBegin(GL_LINES);glVertex3f(m_measurePts[0].x(),m_measurePts[0].y(),m_measurePts[0].z());
        glVertex3f(m_measurePts[1].x(),m_measurePts[1].y(),m_measurePts[1].z());glEnd();glDisable(GL_LINE_STIPPLE);}
    if(!m_measurePts.isEmpty()){
        glColor4f(.5f,.8f,1.f,.5f);glLineWidth(1.f);glEnable(GL_LINE_STIPPLE);glLineStipple(2,0xAAAA);
        glBegin(GL_LINES);glVertex3f(0,0,0);glVertex3f(m_measurePts[0].x(),m_measurePts[0].y(),m_measurePts[0].z());glEnd();glDisable(GL_LINE_STIPPLE);}
    glLineWidth(1.f);glEnable(GL_DEPTH_TEST);glEnable(GL_LIGHTING);
}
QString Sim3DWidget::formatMeasure() const
{
    if(m_measurePts.isEmpty()) return {};
    const QVector3D &p1=m_measurePts[0];
    float d1=sqrtf(p1.x()*p1.x()+p1.y()*p1.y()+p1.z()*p1.z());
    QString s=QString("<b>Point 1</b> : X=<b>%1</b>  Y=<b>%2</b>  Z=<b>%3</b> mm<br>Distance origine : <b>%4 mm</b>")
                    .arg(p1.x(),0,'f',3).arg(p1.y(),0,'f',3).arg(p1.z(),0,'f',3).arg(d1,0,'f',3);
    if(m_measurePts.size()==2){
        const QVector3D &p2=m_measurePts[1];
        float d2=sqrtf(p2.x()*p2.x()+p2.y()*p2.y()+p2.z()*p2.z());
        QVector3D dv=p2-p1; float dist=dv.length(),dxy=sqrtf(dv.x()*dv.x()+dv.y()*dv.y());
        s+=QString("<br><b>Point 2</b> : X=<b>%1</b>  Y=<b>%2</b>  Z=<b>%3</b> mm<br>Distance origine : <b>%4 mm</b><br>"
                     "<hr style='margin:4px 0'>ΔX=<b>%5</b>  ΔY=<b>%6</b>  ΔZ=<b>%7</b> mm<br>"
                     "Distance 3D : <b>%8 mm</b>   Distance XY : <b>%9 mm</b>")
                 .arg(p2.x(),0,'f',3).arg(p2.y(),0,'f',3).arg(p2.z(),0,'f',3).arg(d2,0,'f',3)
                 .arg(dv.x(),0,'f',3).arg(dv.y(),0,'f',3).arg(dv.z(),0,'f',3).arg(dist,0,'f',3).arg(dxy,0,'f',3);
    }
    return s;
}
void Sim3DWidget::detectFace(const QVector3D &clickPt)
{
    m_faceRegion.clear(); m_faceType=0;
    if(!m_stock.valid||m_hmap.data.empty()||m_hmap.res<2) return;
    const int R=m_hmap.res;
    float px=(clickPt.x()-m_hmap.xMin)/(m_hmap.xMax-m_hmap.xMin)*(R-1);
    float py=(clickPt.y()-m_hmap.yMin)/(m_hmap.yMax-m_hmap.yMin)*(R-1);
    int cx=qBound(1,int(px),R-2),cy=qBound(1,int(py),R-2);
    float zClick=clickPt.z(),tol=qMax(0.1f,qAbs(m_hmap.zTop-m_hmap.zBot)*.015f);
    QVector<QPoint> stk; stk.reserve(1024);
    QVector<QVector<bool>> vis(R,QVector<bool>(R,false));
    stk.append({cx,cy}); vis[cx][cy]=true;
    QVector<QVector3D> reg; int mx=8000;
    while(!stk.isEmpty()&&reg.size()<mx){
        QPoint pt=stk.takeLast(); float z=m_hmap.safe(pt.x(),pt.y());
        if(qAbs(z-zClick)>tol) continue;
        reg.append({m_hmap.worldX(pt.x()),m_hmap.worldY(pt.y()),z});
        const int dx[]={1,-1,0,0},dy[]={0,0,1,-1};
        for(int k=0;k<4;k++){ int nx=pt.x()+dx[k],ny=pt.y()+dy[k];
            if(nx<0||nx>=R||ny<0||ny>=R||vis[nx][ny]) continue;
            vis[nx][ny]=true; if(qAbs(m_hmap.safe(nx,ny)-zClick)<=tol) stk.append({nx,ny}); }
    }
    if(reg.size()<4) return; m_faceRegion=reg;
    float zMn=1e9f,zMx=-1e9f,xMn=1e9f,xMx=-1e9f,yMn=1e9f,yMx=-1e9f;
    for(const QVector3D &pt:std::as_const(reg)){zMn=qMin(zMn,pt.z());zMx=qMax(zMx,pt.z());xMn=qMin(xMn,pt.x());xMx=qMax(xMx,pt.x());yMn=qMin(yMn,pt.y());yMx=qMax(yMx,pt.y());}
    float dz=zMx-zMn,dx2=xMx-xMn,dy2=yMx-yMn,sp=sqrtf(dx2*dx2+dy2*dy2);
    if(dz<tol*2) m_faceType=1; else if(sp<dz*1.5f) m_faceType=2; else m_faceType=3;
    emit faceDetected(analyzeFaceRegion());
}
QString Sim3DWidget::analyzeFaceRegion() const
{
    if(m_faceRegion.isEmpty()) return {};
    float xMn=1e9f,xMx=-1e9f,yMn=1e9f,yMx=-1e9f,zMn=1e9f,zMx=-1e9f;
    for(const QVector3D &p:m_faceRegion){xMn=qMin(xMn,p.x());xMx=qMax(xMx,p.x());yMn=qMin(yMn,p.y());yMx=qMax(yMx,p.y());zMn=qMin(zMn,p.z());zMx=qMax(zMx,p.z());}
    float w=xMx-xMn,h=yMx-yMn,dp=qAbs(zMx-zMn),cx2=(xMn+xMx)/2,cy2=(yMn+yMx)/2;
    float rSum=0,r2Sum=0,n=float(m_faceRegion.size());
    for(const QVector3D &p:m_faceRegion){float dx=p.x()-cx2,dy=p.y()-cy2,r=sqrtf(dx*dx+dy*dy);rSum+=r;r2Sum+=r*r;}
    float rMoy=rSum/n,rVar=sqrtf(r2Sum/n-rMoy*rMoy);
    bool isCircle=(rMoy>0.5f&&rVar/rMoy<0.25f&&n>20);
    if(m_faceType==1){
        if(isCircle) return QString("<b style='color:#7cf'>CERCLE</b><br>Centre X=<b>%1</b> Y=<b>%2</b><br>Ø<b>%3</b> mm  Z=<b>%4</b>").arg(cx2,0,'f',3).arg(cy2,0,'f',3).arg(2*rMoy,0,'f',3).arg(zMx,0,'f',3);
        return QString("<b style='color:#afc'>FACE PLANE</b><br>X[%1…%2] L=<b>%3</b><br>Y[%4…%5] L=<b>%6</b><br>Z=<b>%7</b>").arg(xMn,0,'f',3).arg(xMx,0,'f',3).arg(w,0,'f',3).arg(yMn,0,'f',3).arg(yMx,0,'f',3).arg(h,0,'f',3).arg(zMx,0,'f',3);
    }
    if(m_faceType==2) return QString("<b style='color:#fca'>PAROI</b><br>Z[%1…%2] prof=<b>%3</b>").arg(zMn,0,'f',3).arg(zMx,0,'f',3).arg(dp,0,'f',3);
    return QString("<b style='color:#ccc'>RÉGION</b><br>X[%1…%2] Y[%3…%4]").arg(xMn,0,'f',3).arg(xMx,0,'f',3).arg(yMn,0,'f',3).arg(yMx,0,'f',3);
}
void Sim3DWidget::drawFaceHighlight()
{
    if(m_faceRegion.isEmpty()) return;
    glDisable(GL_LIGHTING);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    float cr,cg,cb; switch(m_faceType){case 1:cr=.5f;cg=.9f;cb=.5f;break;case 2:cr=.9f;cg=.6f;cb=.2f;break;default:cr=.5f;cg=.7f;cb=.9f;break;}
    glColor4f(cr,cg,cb,.35f);glPointSize(3.5f);
    glBegin(GL_POINTS); for(const QVector3D &p:std::as_const(m_faceRegion)) glVertex3f(p.x(),p.y(),p.z()+.05f); glEnd();
    float xMn=1e9f,xMx=-1e9f,yMn=1e9f,yMx=-1e9f,zV=-1e9f;
    for(const QVector3D &p:std::as_const(m_faceRegion)){xMn=qMin(xMn,p.x());xMx=qMax(xMx,p.x());yMn=qMin(yMn,p.y());yMx=qMax(yMx,p.y());zV=qMax(zV,p.z());}
    glColor4f(cr,cg,cb,.85f);glLineWidth(2.f);
    glBegin(GL_LINE_LOOP);glVertex3f(xMn,yMn,zV+.1f);glVertex3f(xMx,yMn,zV+.1f);glVertex3f(xMx,yMx,zV+.1f);glVertex3f(xMn,yMx,zV+.1f);glEnd();
    glLineWidth(1.f);glPointSize(1.f);glEnable(GL_DEPTH_TEST);glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════════
//  Barre de lecture (overlay blanc dans la vue 3D)
// ═══════════════════════════════════════════════════════════════
namespace {
enum TpIcon { IcStart, IcRewind, IcStepBack, IcPlay, IcPause, IcStepFwd, IcFast, IcEnd };
QIcon tpIcon(TpIcon k)
{
    const int S = 44;                      // dessin en 2x pour la netteté
    QPixmap pm(S, S); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(QColor(255,255,255));
    auto triR = [&](float x, float w){ QPolygonF t; t<<QPointF(x,10)<<QPointF(x+w,22)<<QPointF(x,34); p.drawPolygon(t); };
    auto triL = [&](float x, float w){ QPolygonF t; t<<QPointF(x+w,10)<<QPointF(x,22)<<QPointF(x+w,34); p.drawPolygon(t); };
    auto bar  = [&](float x){ p.drawRect(QRectF(x,10,4,24)); };
    switch (k) {
    case IcStart:    bar(9);  triL(14,10); triL(24,10); break;
    case IcRewind:   triL(8,13); triL(21,13); break;
    case IcStepBack: bar(11); triL(17,15); break;
    case IcPlay:     triR(14,18); break;
    case IcPause:    p.drawRect(QRectF(13,10,6,24)); p.drawRect(QRectF(25,10,6,24)); break;
    case IcStepFwd:  triR(12,15); bar(29); break;
    case IcFast:     triR(10,13); triR(23,13); break;
    case IcEnd:      triR(10,10); triR(20,10); bar(31); break;
    }
    p.end();
    return QIcon(pm);
}
}

void Sim3DWidget::buildTransportBar()
{
    m_tpBar = new QWidget(this);
    m_tpBar->setObjectName("tpBar");
    m_tpBar->setAttribute(Qt::WA_StyledBackground, true);
    m_tpBar->setStyleSheet(
        "#tpBar{background:rgba(15,15,20,160);border-radius:8px;}"
        "QToolButton{background:transparent;border:none;border-radius:5px;padding:2px;}"
        "QToolButton:hover{background:rgba(255,255,255,45);}"
        "QToolButton:pressed{background:rgba(255,255,255,80);}"
        "QToolButton:checked{background:rgba(255,255,255,60);}"
        "QLabel{color:white;font-size:11px;font-weight:600;}"
        "QSlider::groove:horizontal{height:4px;background:rgba(255,255,255,70);border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:white;border-radius:2px;}"
        "QSlider::handle:horizontal{background:white;width:14px;height:14px;margin:-5px 0;border-radius:7px;}");
    auto *v = new QVBoxLayout(m_tpBar); v->setContentsMargins(10,6,10,4); v->setSpacing(2);
    m_tpSlider = new QSlider(Qt::Horizontal, m_tpBar);
    m_tpSlider->setRange(0, 0); m_tpSlider->setFocusPolicy(Qt::NoFocus);
    m_tpSlider->installEventFilter(this);
    v->addWidget(m_tpSlider);
    auto *h = new QHBoxLayout; h->setSpacing(4); v->addLayout(h);
    auto mk = [&](TpIcon ic, const QString &tip){
        auto *b = new QToolButton(m_tpBar); b->setIcon(tpIcon(ic)); b->setIconSize(QSize(22,22));
        b->setToolTip(tip); b->setFocusPolicy(Qt::NoFocus); b->setAutoRaise(true); h->addWidget(b); return b; };
    auto *bStart = mk(IcStart,   "Debut du programme");
    auto *bRew   = mk(IcRewind,  "Rembobiner (x1, x2, x4, x8)");
    auto *bSb    = mk(IcStepBack,"Passe precedente");
    m_tpPlay     = mk(IcPlay,    "Lecture / Pause");
    auto *bSf    = mk(IcStepFwd, "Passe suivante");
    for (QToolButton *b : {bSb, bSf}) { b->setAutoRepeat(true); b->setAutoRepeatDelay(500); b->setAutoRepeatInterval(250); }
    auto *bFf    = mk(IcFast,    "Avance rapide (x2, x4, x8, x16)");
    auto *bEnd   = mk(IcEnd,     "Fin du programme");
    m_tpLbl = new QLabel(m_tpBar); m_tpLbl->setMinimumWidth(42); m_tpLbl->setAlignment(Qt::AlignCenter);
    h->addWidget(m_tpLbl);
    h->addStretch(1);
    m_tpPersp = new QToolButton(m_tpBar); m_tpPersp->setFocusPolicy(Qt::NoFocus);
    m_tpPersp->setToolTip("Vue en perspective / orthogonale");
    m_tpPersp->setStyleSheet("QToolButton{color:white;font-size:11px;font-weight:600;padding:3px 8px;"
                             "border:1px solid rgba(255,255,255,120);border-radius:5px;}");
    h->addWidget(m_tpPersp);

    connect(bStart, &QToolButton::clicked, this, &Sim3DWidget::goToStart);
    connect(bRew,   &QToolButton::clicked, this, &Sim3DWidget::rewind);
    connect(bSb,    &QToolButton::clicked, this, &Sim3DWidget::stepBackward);
    connect(m_tpPlay,&QToolButton::clicked,this, &Sim3DWidget::togglePlayPause);
    connect(bSf,    &QToolButton::clicked, this, &Sim3DWidget::stepForward);
    connect(bFf,    &QToolButton::clicked, this, &Sim3DWidget::fastForward);
    connect(bEnd,   &QToolButton::clicked, this, &Sim3DWidget::goToEnd);
    connect(m_tpPersp,&QToolButton::clicked,this,[this]{ setOrtho(!m_ortho); });

    // Barre de temps : glisser = aperçu rapide, relâcher = position exacte
    connect(m_tpSlider, &QSlider::sliderPressed, this, [this]{
        m_sliderDrag = true; stopRewind(false);
        if (m_playing) { m_playing=false; m_animTimer->stop(); m_hmapRefreshTimer->stop(); emit playingChanged(false); }
        updateTransportUi();
    });
    connect(m_tpSlider, &QSlider::sliderMoved, this, [this](int v){
        if (m_moves.isEmpty()) return;
        v = qBound(0, v, int(m_moves.size()));
        m_curMove = v; m_curSeg = 0; m_curSegT = 0.f; m_dlDirty = true; m_needSyncRebuild = true;
        if (v < m_moves.size() && !m_moves[v].pts.isEmpty()) m_toolPos = m_moves[v].pts.first();
        else if (!m_moves.last().pts.isEmpty())               m_toolPos = m_moves.last().pts.last();
        emitAnimStatus(true);
        requestBuildCurrent(m_animRes);
        update();
    });
    connect(m_tpSlider, &QSlider::sliderReleased, this, [this]{
        m_sliderDrag = false; seekToMove(m_tpSlider->value());
    });
    // Clic dans la barre (sans glisser) : saut direct
    connect(m_tpSlider, &QSlider::valueChanged, this, [this](int v){
        if (!m_sliderDrag) seekToMove(v);
    });

    m_rwTimer = new QTimer(this); m_rwTimer->setInterval(16);
    connect(m_rwTimer, &QTimer::timeout, this, &Sim3DWidget::onRewindTick);
    m_fineTimer = new QTimer(this); m_fineTimer->setSingleShot(true); m_fineTimer->setInterval(600);
    connect(m_fineTimer, &QTimer::timeout, this, [this]{
        if (m_playing || m_rwMul > 0 || m_sliderDrag) return;
        if (m_hmap.res != m_hmapRes || m_buildRes != m_hmapRes) requestBuildCurrent(m_hmapRes);
    });
    updateTransportUi();
}

bool Sim3DWidget::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_tpSlider && ev->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && m_tpSlider->maximum() > 0) {
            const int span = qMax(1, m_tpSlider->width() - 14);
            const int v = QStyle::sliderValueFromPosition(m_tpSlider->minimum(), m_tpSlider->maximum(),
                                                          int(me->position().x()) - 7, span);
            m_tpSlider->setValue(v);   // saut direct ; la poignée arrive sous la souris → glisser possible
        }
    }
    return QOpenGLWidget::eventFilter(obj, ev);
}

void Sim3DWidget::positionTransportBar()
{
    if (!m_tpBar) return;
    const int w = qMin(560, width() - 20), hgt = 74;
    m_tpBar->setGeometry((width() - w) / 2, height() - hgt - 10, w, hgt);
    m_tpBar->raise();
}

void Sim3DWidget::updateTransportUi()
{
    if (!m_tpBar) return;
    m_tpPlay->setIcon(tpIcon(m_playing ? IcPause : IcPlay));
    m_tpPersp->setText(m_ortho ? "Ortho" : "Persp.");
    QString t;
    if (m_rwMul > 0)                 t = QString("◀ x%1").arg(m_rwMul);
    else if (m_playing && m_ffMul>1) t = QString("x%1 ▶").arg(m_ffMul);
    m_tpLbl->setText(t);
    if (!m_sliderDrag) {
        QSignalBlocker b(m_tpSlider);
        m_tpSlider->setRange(0, int(m_moves.size()));
        m_tpSlider->setValue(qMin(m_curMove, int(m_moves.size())));
    }
}

void Sim3DWidget::launchBuildCurrent(int res)
{
    if (m_moves.isEmpty() || !m_stock.valid) return;
    if (m_curMove >= m_moves.size()) { m_bpMove = -1; buildHeightmapAsync(-1, res); return; }
    m_bpMove = (m_curSeg > 0 || m_curSegT > 0.f) ? m_curMove : -1;
    m_bpSeg = m_curSeg; m_bpPos = m_toolPos;
    buildHeightmapAsync(m_curMove > 0 ? m_curMove - 1 : -3, res);
}

// Lance le calcul tout de suite, ou dès que le calcul en cours se termine
// (jamais d'attente bloquante pendant qu'on fait glisser la barre)
void Sim3DWidget::requestBuildCurrent(int res)
{
    if (m_hmapWatcher->isRunning()) {
        // Un calcul fin (lent) est périmé dès qu'on bouge : on l'abandonne
        if (m_buildRes > m_animRes) { stopBuild(); launchBuildCurrent(res); return; }
        m_buildPending = true; m_pendingRes = res; return;   // calcul léger : on enchaîne
    }
    launchBuildCurrent(res);
}

void Sim3DWidget::togglePlayPause()
{
    stopRewind(false);
    if (m_playing) pause();
    else {
        m_ffMul = 1; setSpeed(m_speed);
        if (m_curMove >= m_moves.size()) resetSim();
        play();
    }
    updateTransportUi();
}

void Sim3DWidget::seekToMove(int idx)
{
    if (m_moves.isEmpty()) return;
    stopRewind(false);
    if (m_playing) { m_playing=false; m_animTimer->stop(); m_hmapRefreshTimer->stop(); emit playingChanged(false); }
    idx = qBound(0, idx, int(m_moves.size()));
    m_curMove = idx; m_curSeg = 0; m_curSegT = 0.f; m_dlDirty = true; m_lastEmittedLine = -1;
    if (idx < m_moves.size() && !m_moves[idx].pts.isEmpty()) m_toolPos = m_moves[idx].pts.first();
    else if (!m_moves.last().pts.isEmpty())                  m_toolPos = m_moves.last().pts.last();
    m_needSyncRebuild = true;
    requestBuildCurrent(m_animRes < m_hmapRes ? m_animRes : m_hmapRes);
    m_fineTimer->start();
    emitAnimStatus(true);
    updateTransportUi();
    update();
}

void Sim3DWidget::seekToLine(int lineIdx)
{
    if (m_moves.isEmpty()) return;
    int idx = int(m_moves.size());
    for (int i = 0; i < m_moves.size(); ++i)
        if (m_moves[i].lineIdx >= lineIdx) { idx = i; break; }
    seekToMove(idx);
}

void Sim3DWidget::goToStart()
{
    stopRewind(false);
    resetSim();
    m_needSyncRebuild = false;
    emitAnimStatus(true);
    updateTransportUi();
}

void Sim3DWidget::goToEnd() { seekToMove(int(m_moves.size())); }

void Sim3DWidget::stepForward()
{
    if (m_moves.isEmpty() || m_curMove >= m_moves.size()) return;
    stopRewind(false);
    if (m_playing) { m_playing=false; m_animTimer->stop(); m_hmapRefreshTimer->stop(); emit playingChanged(false); }
    if (m_needSyncRebuild) {                  // la pièce doit être à jour avant de creuser
        stopBuild();
        m_buildPending = false;
        m_bpMove = (m_curSeg > 0 || m_curSegT > 0.f) ? m_curMove : -1;
        m_bpSeg = m_curSeg; m_bpPos = m_toolPos;
        m_buildRes = (m_animRes > 1) ? m_animRes : m_hmapRes;   // grille légère : rapide
        buildHeightmapSync(m_curMove > 0 ? m_curMove - 1 : -3);
        m_needSyncRebuild = false;
    }
    // Une PASSE = suite de déplacements en coupe, jusqu'au prochain rapide (remontée).
    // 2 essais : si on est déjà (à quelques centièmes près) en fin de passe,
    // on enchaîne directement sur la passe suivante.
    for (int attempt = 0; attempt < 2 && m_curMove < m_moves.size(); ++attempt) {
    int target = m_curMove;
    bool seenCut = false;
    while (target < m_moves.size()) {
        if (m_moves[target].kind == SimMove::Cut) seenCut = true;
        else if (seenCut) break;
        ++target;
    }
    double rem = 0.0;
    for (int mi = m_curMove; mi < target; ++mi) {
        const SimMove &mv = m_moves[mi];
        for (int s = (mi == m_curMove ? m_curSeg : 0); s + 1 < mv.pts.size(); ++s) {
            const float l = mv.pts[s].distanceToPoint(mv.pts[s+1]);
            rem += (mi == m_curMove && s == m_curSeg) ? (1.f - m_curSegT) * l : l;
        }
    }
    if (rem < 0.05) {
        m_curMove = target; m_curSeg = 0; m_curSegT = 0.f; m_dlDirty = true;
        if (m_curMove < m_moves.size() && !m_moves[m_curMove].pts.isEmpty()) m_toolPos = m_moves[m_curMove].pts.first();
        continue;                              // presque rien fait : passe suivante
    }
    const float save = m_stepMM;
    m_stepMM = float(rem) + 0.02f;            // la matière est creusée tout le long
    advanceAnimation();
    m_stepMM = save;
    break;
    }
    if (m_hmap.res != m_hmapRes) m_fineTimer->start();
    emitAnimStatus(true);
    updateTransportUi();
    update();
}

void Sim3DWidget::stepBackward()
{
    if (m_moves.isEmpty()) return;
    // Début de la passe (groupe de coupes) qui précède la position actuelle
    auto groupStartBefore = [&](int i) -> int {   // i = dernier move déjà (au moins en partie) fait
        while (i >= 0 && m_moves[i].kind != SimMove::Cut) --i;   // remonte les rapides
        if (i < 0) return -1;
        while (i > 0 && m_moves[i-1].kind == SimMove::Cut) --i;  // remonte la passe
        return i;
    };
    const bool midMove = (m_curSeg > 0 || m_curSegT > 0.f);
    int last = qMin(int(m_moves.size()) - 1, midMove ? m_curMove : m_curMove - 1);
    int target = groupStartBefore(last);
    if (target >= 0 && target == m_curMove && !midMove) target = groupStartBefore(target - 1);  // déjà au début : passe d'avant
    if (target <= 0) { goToStart(); return; }
    seekToMove(target);
}

void Sim3DWidget::fastForward()
{
    if (m_moves.isEmpty()) return;
    stopRewind(false);
    m_ffMul = (m_ffMul >= 16 || !m_playing) ? 2 : m_ffMul * 2;
    setSpeed(m_speed);
    if (!m_playing) { if (m_curMove >= m_moves.size()) resetSim(); play(); }
    updateTransportUi();
}

void Sim3DWidget::rewind()
{
    if (m_moves.isEmpty() || (m_curMove == 0 && m_curSeg == 0 && m_curSegT <= 0.f)) return;
    if (m_playing) { m_playing=false; m_animTimer->stop(); m_hmapRefreshTimer->stop(); emit playingChanged(false); }
    m_ffMul = 1; setSpeed(m_speed);
    m_rwMul = (m_rwMul <= 0 || m_rwMul >= 8) ? 1 : m_rwMul * 2;
    m_needSyncRebuild = true;
    if (!m_rwTimer->isActive()) { m_rwBuildClock.start(); m_rwTimer->start(); }
    updateTransportUi();
}

void Sim3DWidget::stopRewind(bool refine)
{
    if (m_rwMul <= 0 && !m_rwTimer->isActive()) return;
    m_rwMul = 0; m_rwTimer->stop();
    requestBuildCurrent(m_animRes < m_hmapRes ? m_animRes : m_hmapRes);
    if (refine) m_fineTimer->start();
    updateTransportUi();
}

// Recule l'outil de dist mm le long du trajet. Retourne true au début du programme.
bool Sim3DWidget::retreat(float dist)
{
    if (m_moves.isEmpty()) return true;
    if (m_curMove >= m_moves.size()) {
        m_curMove = int(m_moves.size()) - 1;
        m_curSeg = qMax(0, int(m_moves[m_curMove].pts.size()) - 2); m_curSegT = 1.f;
    }
    int guard = 0;
    while (dist > 0.f && guard++ < 200000) {
        const SimMove &mv = m_moves[m_curMove];
        const bool segOk = mv.pts.size() >= 2 && m_curSeg + 1 < mv.pts.size();
        const float sl = segOk ? mv.pts[m_curSeg].distanceToPoint(mv.pts[m_curSeg+1]) : 0.f;
        if (!segOk || sl < 1e-6f || m_curSegT <= 0.f) {
            if (segOk && m_curSeg > 0) { m_curSeg--; m_curSegT = 1.f; continue; }
            if (m_curMove == 0) {
                m_curSeg = 0; m_curSegT = 0.f;
                if (!mv.pts.isEmpty()) m_toolPos = mv.pts.first();
                return true;
            }
            m_curMove--; m_dlDirty = true;
            m_curSeg = qMax(0, int(m_moves[m_curMove].pts.size()) - 2); m_curSegT = 1.f;
            continue;
        }
        const QVector3D &a = mv.pts[m_curSeg], &b = mv.pts[m_curSeg+1];
        const float back = m_curSegT * sl;
        if (dist >= back) { dist -= back; m_curSegT = 0.f; m_toolPos = a; }
        else { m_curSegT -= dist / sl; m_toolPos = a + (b - a) * m_curSegT; dist = 0.f; }
    }
    return false;
}

void Sim3DWidget::onRewindTick()
{
    if (m_rwMul <= 0) { m_rwTimer->stop(); return; }
    const bool atStart = retreat(m_stepBase * float(m_rwMul));
    // Matière remise à jour ~6 fois par seconde, sans bloquer
    if (!m_hmapWatcher->isRunning() && m_rwBuildClock.elapsed() > 160) {
        m_rwBuildClock.start();
        launchBuildCurrent(m_animRes < m_hmapRes ? m_animRes : m_hmapRes);
    }
    emitAnimStatus(false);
    updateTransportUi();
    update();
    if (atStart) { m_rwMul = 0; m_rwTimer->stop(); goToStart(); }
}

// ═══════════════════════════════════════════════════════════════
//  Calculs de pièce : annulation + points de sauvegarde
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::stopBuild()
{
    if (!m_hmapWatcher || !m_hmapWatcher->isRunning()) return;
    m_buildCancel.store(true);
    m_hmapWatcher->waitForFinished();      // le calcul s'arrête au prochain segment
    m_buildCancel.store(false);
}

void Sim3DWidget::invalidateCheckpoints()
{
    ++m_matVer;
    ++m_ckptGen;
    if (m_ckptFuture.isRunning()) m_ckptFuture.waitForFinished();
    QMutexLocker ck(&m_ckptMutex);
    m_ckpts.clear(); m_ckptRes = 0;
    m_finalFine = Heightmap(); m_finalFineValid = false;
}

// Calcule en tâche de fond, sur la grille légère, l'état de la pièce à
// intervalles réguliers du programme (~100 Mo au maximum).
void Sim3DWidget::startCheckpoints()
{
    invalidateCheckpoints();
    if (!m_stock.valid || m_moves.isEmpty() || m_animRes < 2) return;
    const int gen = m_ckptGen.load();
    const int res = m_animRes;
    const QVector<SimMove> moves = m_moves;          // copie partagée
    const StockData st = m_stock;
    qint64 total = 0;
    for (const SimMove &mv : moves) if (mv.kind == SimMove::Cut) total += qMax(0, int(mv.pts.size()) - 1);
    if (total <= 0) return;
    const double perCk = double(res) * res * 8.0;
    const int nCk = qBound(6, int(100e6 / perCk), 48);
    const qint64 interval = qMax<qint64>(150, total / nCk);
    const int fineRes = m_hmapRes;
    m_ckptFuture = QtConcurrent::run([this, gen, res, fineRes, moves, st, interval]{
        Heightmap hm; hm.res = res; hm.init(st.xMin, st.xMax, st.yMin, st.yMax, st.zMax, st.zMin);
        QVector<HmCkpt> local;
        qint64 acc = 0;
        for (int mi = 0; mi < moves.size(); ++mi) {
            if (m_ckptGen.load() != gen) return;
            const SimMove &mv = moves[mi];
            if (mv.kind != SimMove::Cut) continue;
            const ToolInfo ti = resolveToolInfo(mv.toolT);
            for (int i = 1; i < mv.pts.size(); ++i) hm.cutSegment(mv.pts[i-1], mv.pts[i], ti, mv.toolT);
            acc += qMax(0, int(mv.pts.size()) - 1);
            if (acc >= interval) { acc = 0; HmCkpt c; c.next = mi + 1; c.hm = hm; local.push_back(std::move(c)); }
        }
        {
            QMutexLocker ck(&m_ckptMutex);
            if (m_ckptGen.load() != gen) return;
            m_ckpts = std::move(local); m_ckptRes = res;
            if (m_finalFineValid) return;               // déjà calculée par ailleurs
        }
        if (fineRes <= res) return;
        Heightmap fh; fh.res = fineRes; fh.init(st.xMin, st.xMax, st.yMin, st.yMax, st.zMax, st.zMin);
        for (int mi = 0; mi < moves.size(); ++mi) {
            if (m_ckptGen.load() != gen) return;
            const SimMove &mv = moves[mi];
            if (mv.kind != SimMove::Cut) continue;
            const ToolInfo ti = resolveToolInfo(mv.toolT);
            for (int i = 1; i < mv.pts.size(); ++i) fh.cutSegment(mv.pts[i-1], mv.pts[i], ti, mv.toolT);
        }
        QMutexLocker ck(&m_ckptMutex);
        if (m_ckptGen.load() == gen && !m_finalFineValid) { m_finalFine = std::move(fh); m_finalFineValid = true; }
    });
}

// ═══════════════════════════════════════════════════════════════
//  Loupe : la zone visible recalculée en grille fine
// ═══════════════════════════════════════════════════════════════
QVector<float> Sim3DWidget::camKey() const
{
    return { m_azimuth, m_elevation, m_dist, m_pivot.x(), m_pivot.y(), m_pivot.z(),
             m_ortho ? 1.f : 0.f, float(width()), float(height()) };
}

void Sim3DWidget::cancelLoupe()
{
    ++m_lpGen;
    if (m_lpFuture.isRunning()) m_lpFuture.waitForFinished();
}

void Sim3DWidget::computeLoupe()
{
    if (m_playing || m_rwMul > 0 || m_sliderDrag || !m_stock.valid || m_moves.isEmpty()) return;
    if (m_lpFuture.isRunning()) { m_lpTimer->start(); return; }   // on réessaiera
    const QVector<float> cam = camKey();
    const int ver = m_matVer.load();
    if (cam == m_lpBuiltCam && ver == m_lpVerBuilt && m_lpValid) return;

    // ── Zone visible : rayons écran → plans du dessus et du fond du brut
    const QMatrix4x4 inv = (m_proj * m_view).inverted();
    const float zT = m_stock.zMax, zB = m_stock.zMin;
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    bool any = false;
    for (int sy = 0; sy <= 8; ++sy)
        for (int sx = 0; sx <= 8; ++sx) {
            const float nX = -1.f + 2.f * sx / 8.f, nY = -1.f + 2.f * sy / 8.f;
            QVector4D a = inv * QVector4D(nX, nY, -1.f, 1.f), b = inv * QVector4D(nX, nY, 1.f, 1.f);
            if (qAbs(a.w()) < 1e-9f || qAbs(b.w()) < 1e-9f) continue;
            const QVector3D o = a.toVector3D() / a.w(), d = b.toVector3D() / b.w() - o;
            if (qAbs(d.z()) < 1e-9f) continue;
            for (float zp : { zT, zB }) {
                const float t = (zp - o.z()) / d.z();
                if (t < 0.f || t > 1.f) continue;
                const QVector3D p = o + d * t;
                x0 = qMin(x0, p.x()); x1 = qMax(x1, p.x()); y0 = qMin(y0, p.y()); y1 = qMax(y1, p.y());
                any = true;
            }
        }
    m_lpBuiltCam = cam;
    if (!any) { m_lpVerBuilt = ver; if (m_lpValid) { m_lpValid = false; update(); } return; }
    const float mx = 0.04f * (x1 - x0), my = 0.04f * (y1 - y0);
    x0 = qMax(m_stock.xMin, x0 - mx); x1 = qMin(m_stock.xMax, x1 + mx);
    y0 = qMax(m_stock.yMin, y0 - my); y1 = qMin(m_stock.yMax, y1 + my);
    // Vue rasante : la zone visible file jusqu'au fond de la pièce. On ne
    // garde que l'entourage du centre de l'écran (le pivot), sur 1,5 fois
    // la largeur visible à cette distance : c'est là que se fait le zoom.
    {
        const float mmPx0 = (m_ortho ? 0.70f : 0.443f) * m_dist / float(qMax(1, height()));
        const float hw = 0.75f * mmPx0 * float(qMax(width(), height()));
        x0 = qMax(x0, m_pivot.x() - hw); x1 = qMin(x1, m_pivot.x() + hw);
        y0 = qMax(y0, m_pivot.y() - hw); y1 = qMin(y1, m_pivot.y() + hw);
    }
    if (x1 - x0 < 0.05f || y1 - y0 < 0.05f) { m_lpVerBuilt = ver; if (m_lpValid) { m_lpValid = false; update(); } return; }

    // ── Finesse visée : ~0,7 pixel par case, sans descendre sous 0,005 mm
    const float globalCell = (m_stock.xMax - m_stock.xMin) / float(qMax(2, m_hmapRes) - 1);
    const float mmPerPx = (m_ortho ? 0.70f : 0.443f) * m_dist / float(qMax(1, height()));
    const float target  = qMax(0.001f, 0.7f * mmPerPx);
    const float span    = qMax(x1 - x0, y1 - y0);
    const int   LR      = qBound(256, int(ceilf(span / target)) + 1, 1536);
    const float lCell   = span / float(LR - 1);
    if (lCell > globalCell / 1.4f) {                  // pas assez de gain : pas de loupe
        m_lpVerBuilt = ver; if (m_lpValid) { m_lpValid = false; update(); }
        return;
    }

    // ── Calcul en tâche de fond, uniquement des passes qui touchent la zone
    const int gen = ++m_lpGen;
    const QVector<SimMove> moves = m_moves;
    const int upTo = m_curMove, seg = m_curSeg;
    const bool partial = (m_curSeg > 0 || m_curSegT > 0.f) && m_curMove < m_moves.size();
    const QVector3D tp = m_toolPos;
    const StockData st = m_stock;
    const QRectF reg(QPointF(x0, y0), QPointF(x1, y1));
    m_lpFuture = QtConcurrent::run([this, gen, ver, moves, upTo, seg, partial, tp, st, reg, LR]{
        Heightmap lp; lp.res = LR;
        lp.init(float(reg.left()), float(reg.right()), float(reg.top()), float(reg.bottom()), st.zMax, st.zMin);
        const float rx0 = float(reg.left()), rx1 = float(reg.right()), ry0 = float(reg.top()), ry1 = float(reg.bottom());
        auto touches = [&](const QVector3D &a, const QVector3D &b, float r){
            return qMax(a.x(), b.x()) + r >= rx0 && qMin(a.x(), b.x()) - r <= rx1
                && qMax(a.y(), b.y()) + r >= ry0 && qMin(a.y(), b.y()) - r <= ry1; };
        const int lim = qMin(upTo, int(moves.size()));
        int n = 0;
        for (int mi = 0; mi < lim; ++mi) {
            if (m_lpGen.load() != gen) return;
            const SimMove &mv = moves[mi];
            if (mv.kind != SimMove::Cut) continue;
            const ToolInfo ti = resolveToolInfo(mv.toolT);
            const float r = ti.r + 1.f;
            for (int i = 1; i < mv.pts.size(); ++i) {
                if (touches(mv.pts[i-1], mv.pts[i], r)) lp.cutSegment(mv.pts[i-1], mv.pts[i], ti, mv.toolT);
                if ((++n & 31) == 0 && m_lpGen.load() != gen) return;
            }
        }
        if (partial && upTo < moves.size()) {       // morceau du déplacement en cours
            const SimMove &mv = moves[upTo];
            if (mv.kind == SimMove::Cut && mv.pts.size() >= 2) {
                const ToolInfo ti = resolveToolInfo(mv.toolT);
                const int sMax = qMin(seg, int(mv.pts.size()) - 1);
                for (int i = 1; i <= sMax; ++i) lp.cutSegment(mv.pts[i-1], mv.pts[i], ti, mv.toolT);
                if (sMax < mv.pts.size()) lp.cutSegment(mv.pts[sMax], tp, ti, mv.toolT);
            }
        }
        {
            QMutexLocker lk(&m_hmapMutex);
            if (m_lpGen.load() != gen) return;
            m_loupe = std::move(lp);
            m_lpRegion = reg;
            m_lpVerBuilt = ver;
            m_lpValid = true;
            m_lpUploaded = false;
        }
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    });
}

// ═══════════════════════════════════════════════════════════════
//  Mesure au survol (comme la simulation TNC 640) : le point de la
//  pièce sous la souris, ses coordonnées pièce et l'outil qui l'a usiné.
// ═══════════════════════════════════════════════════════════════
void Sim3DWidget::leaveEvent(QEvent *e)
{
    m_hoverValid = false; updateHoverLabel(); update();
    QOpenGLWidget::leaveEvent(e);
}

void Sim3DWidget::updateHover(const QPoint &sp)
{
    m_hoverValid = false;
    if (m_stock.valid && !m_hmap.data.empty() && m_hmap.res >= 3) {
        const QVector3D p = pickSurface(sp);
        const bool inside = p.x() >= m_stock.xMin - 1e-3f && p.x() <= m_stock.xMax + 1e-3f
                         && p.y() >= m_stock.yMin - 1e-3f && p.y() <= m_stock.yMax + 1e-3f;
        if (inside && !p.isNull()) {
            const Heightmap &h = m_hmap;
            const float fx = (p.x() - h.xMin) / (h.xMax - h.xMin) * (h.res - 1);
            const float fy = (p.y() - h.yMin) / (h.yMax - h.yMin) * (h.res - 1);
            const int i = qBound(1, int(fx + 0.5f), h.res - 2), j = qBound(1, int(fy + 0.5f), h.res - 2);
            const float cw = (h.xMax - h.xMin) / (h.res - 1), ch = (h.yMax - h.yMin) / (h.res - 1);
            // Normale : différences centrées sur 2 cases (paroi verticale = normale horizontale)
            const int k = 2;
            const int i0 = qMax(0, i - k), i1 = qMin(h.res - 1, i + k), j0 = qMax(0, j - k), j1 = qMin(h.res - 1, j + k);
            const float dzdx = (h.safe(i1, j) - h.safe(i0, j)) / ((i1 - i0) * cw);
            const float dzdy = (h.safe(i, j1) - h.safe(i, j0)) / ((j1 - j0) * ch);
            QVector3D n(-dzdx, -dzdy, 1.f);
            if (std::abs(dzdx) > 20.f || std::abs(dzdy) > 20.f) n.setZ(0.f);   // paroi
            m_hoverN  = n.normalized();
            m_hoverPt = p;
            m_hoverT  = h.toolMap.empty() ? 0 : h.toolMap[size_t(i) * h.res + j];
            m_hoverValid = true;
        }
    }
    updateHoverLabel();
    update();
}

void Sim3DWidget::updateHoverLabel()
{
    if (!m_hoverLbl) return;
    if (!m_measureMode || !m_hoverValid) { m_hoverLbl->hide(); return; }
    // Couleur de l'outil : la même palette que la pièce simulée
    // (T1 bleu, T2 vert, T3 orange, T4 violet, T5 cyan, T6 jaune, T7 rose, T8 vert d'eau)
    static const QColor pal[8] = { QColor(89,140,230), QColor(102,199,115), QColor(242,153,64),
                                   QColor(173,115,224), QColor(77,199,209), QColor(235,209,77),
                                   QColor(224,102,115), QColor(128,173,140) };
    const QColor col = (m_hoverT > 0) ? pal[(m_hoverT - 1) % 8].lighter(115) : QColor(170, 176, 190);
    // Bandeau sombre (comme la barre de lecture), bordure et texte à la couleur de l'outil
    m_hoverLbl->setStyleSheet(QString("QLabel{background:rgba(15,15,22,215);border:1px solid %1;border-radius:5px;"
                                      "font-family:'Consolas','Courier New',monospace;font-size:13px;padding:4px 12px;}")
                              .arg(col.name()));
    const QString l1 = QString::asprintf("X~ %+10.3f   Y~ %+10.3f   Z~ %+10.3f MM",
                                         m_hoverPt.x(), m_hoverPt.y(), m_hoverPt.z());
    QString l2;
    if (m_hoverT > 0) {
        const ToolData *td = ToolDatabase::instance().toolByT(m_hoverT);
        l2 = QString("T%1 %2").arg(m_hoverT).arg(td ? td->description.toHtmlEscaped() : QString());
    } else l2 = QStringLiteral("T0  -");                        // brut, pas encore usiné
    m_hoverLbl->setText(QString("<span style='color:%1;white-space:pre;'>%2</span><br>"
                                "<span style='color:%1;font-weight:bold;white-space:pre;'>&#9632; %3</span>")
                        .arg(col.name(), l1, l2.trimmed()));
    m_hoverLbl->adjustSize();
    m_hoverLbl->move(qMax(4, (width() - m_hoverLbl->width()) / 2), 8);
    m_hoverLbl->show();
    m_hoverLbl->raise();
}

void Sim3DWidget::drawHoverProbe()
{
    const QVector3D n = m_hoverN, p = m_hoverPt;
    // Taille fixe à l'écran (~14 pixels de rayon), quelle que soit la distance
    const QVector3D eye = m_view.inverted().map(QVector3D(0, 0, 0));
    const float pxMM = m_ortho ? (0.70f * m_dist / float(qMax(1, height())))
                               : (0.443f * (eye - p).length() / float(qMax(1, height())));
    const float r = 14.f * pxMM;
    // Repère du cercle : posé à plat sur la face (perpendiculaire à la normale)
    const QVector3D ref = (std::abs(n.z()) < 0.9f) ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
    const QVector3D u = QVector3D::crossProduct(n, ref).normalized();
    const QVector3D v = QVector3D::crossProduct(n, u).normalized();
    const QVector3D c = p + n * (0.6f * pxMM);           // juste au-dessus de la surface

    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST); glEnable(GL_LINE_SMOOTH);
    // Cercle en pointillés noir / blanc
    glLineWidth(2.5f);
    const int N = 32;
    glBegin(GL_LINES);
    for (int k = 0; k < N; ++k) {
        const float a0 = 2.f * float(M_PI) * k / N, a1 = 2.f * float(M_PI) * (k + 1) / N;
        if (k % 2) glColor4f(1.f, 1.f, 1.f, 0.95f); else glColor4f(0.05f, 0.05f, 0.08f, 0.95f);
        const QVector3D q0 = c + (u * std::cos(a0) + v * std::sin(a0)) * r;
        const QVector3D q1 = c + (u * std::cos(a1) + v * std::sin(a1)) * r;
        glVertex3f(q0.x(), q0.y(), q0.z()); glVertex3f(q1.x(), q1.y(), q1.z());
    }
    glEnd();
    // Palpeur : bille posée sur la surface + tige suivant la normale
    const QVector3D ball = p + n * (2.5f * pxMM), top = p + n * (18.f * pxMM);
    glLineWidth(3.f); glColor4f(0.82f, 0.86f, 0.95f, 1.f);
    glBegin(GL_LINES); glVertex3f(ball.x(), ball.y(), ball.z()); glVertex3f(top.x(), top.y(), top.z()); glEnd();
    glEnable(GL_POINT_SMOOTH); glPointSize(9.f);
    glBegin(GL_POINTS); glColor4f(0.92f, 0.94f, 1.f, 1.f); glVertex3f(ball.x(), ball.y(), ball.z()); glEnd();
    glPointSize(1.f); glLineWidth(1.f);
    glEnable(GL_DEPTH_TEST);
}
