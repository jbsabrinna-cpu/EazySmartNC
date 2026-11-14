#pragma once
// ================================================================
//  filepreviewwidget.h  v5 - utilise Sim2DViews directement
//  Rendu 100% identique a la simulation 2D
// ================================================================
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "gcodeengine.h"
#include "sim3dwidget.h"
#include "sim2dviews.h"
#include "Ncfileparser.h"
#include "contourelement.h"
#include "cadimporter.h"

class FilePreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FilePreviewWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0,0,0,0);
        lay->setSpacing(0);

        // Utiliser Sim2DViews directement — vue XY uniquement
        m_sim = new Sim2DViews(this);
        m_sim->setShowRapids(false);
        // Masquer les vues XZ et YZ en les cherchant par ordre
        // Sim2DViews layout: legend + vXY(3) + bot(vXZ+vYZ)(2) + lblInfo
        // On va juste le laisser complet — le user verra XY+XZ+YZ
        // Mais on peut cacher XZ/YZ via les enfants PlanView
        {
            QList<PlanView*> views = m_sim->findChildren<PlanView*>();
            // Le premier PlanView = XY (le plus grand), garder
            // Les autres = XZ, YZ, masquer
            for (int i = 1; i < views.size(); ++i)
                views[i]->setVisible(false);
            // Masquer aussi la legende (premier QWidget enfant non-PlanView)
            QList<QWidget*> widgets = m_sim->findChildren<QWidget*>(
                QString(), Qt::FindDirectChildrenOnly);
            for (QWidget *w : widgets) {
                if (!qobject_cast<PlanView*>(w) &&
                    !qobject_cast<QLabel*>(w))
                    w->setVisible(false); // legende
            }
        }

        lay->addWidget(m_sim, 1);

        m_lblInfo = new QLabel(this);
        m_lblInfo->setFixedHeight(28);
        m_lblInfo->setStyleSheet(
            "background:#EAEAEA; color:#000; font-size:8pt;"
            "border-top:1px solid #B0B0B0; padding:1px 6px;");
        lay->addWidget(m_lblInfo);

        clear();
    }

    void loadFile(const QString &path)
    {
        if (path.isEmpty()) { clear(); return; }
        const QFileInfo fi(path);
        const QString ext = fi.suffix().toLower();

        if (ext == "nc")       loadNc(path);
        else if (ext == "enc") loadEnc(path);
        else if (ext == "cnt") loadCnt(path);
        else if (ext == "dxf") loadDxf(path);
        else                   { clear(); m_lblInfo->setText(fi.fileName()); }
    }

    void clear()
    {
        if (m_sim) {
            m_sim->loadFromGCode({});
            m_sim->setStock(StockData{});
        }
        if (m_lblInfo) m_lblInfo->setText("Aucun fichier selectionne");
    }

private:
    Sim2DViews *m_sim    = nullptr;
    QLabel     *m_lblInfo = nullptr;

    void loadNc(const QString &path)
    {
        const QVector<GCodeLine> lines = NcFileParser::parse(path);
        if (lines.isEmpty()) { clear(); m_lblInfo->setText("Fichier NC vide"); return; }
        StockData stock = inferStock(lines);
        m_sim->setStock(stock);
        m_sim->loadFromGCode(lines);
        const QFileInfo fi(path);
        m_lblInfo->setText(fi.fileName() + "  " + formatSize(fi.size()));
    }

    void loadEnc(const QString &path)
    {
        const QFileInfo fi(path);
        // Chercher le .nc genere a cote
        const QString ncPath = fi.absolutePath() + "/" + fi.baseName() + ".nc";
        if (QFile::exists(ncPath)) {
            loadNc(ncPath);
            m_lblInfo->setText(fi.fileName() + "  (via .nc)");
            return;
        }
        // Afficher juste le brut
        StockData stock = stockFromEnc(path);
        if (stock.valid) {
            m_sim->setStock(stock);
            m_sim->loadFromGCode({});
        } else {
            clear();
        }
        m_lblInfo->setText(fi.fileName() + "  — generer le .nc");
    }

    void loadCnt(const QString &path)
    {
        const QFileInfo fi(path);
        ContourFile cf;
        if (!cf.load(path)) { clear(); m_lblInfo->setText("Erreur .cnt"); return; }

        // Convertir en GCodeLine simules pour loadFromGCode
        QVector<GCodeLine> lines;
        const auto rawSegs = cf.toSegments(32);
        bool first = true;
        for (const ContourFile::Segment &rs : std::as_const(rawSegs)) {
            for (int i = 0; i < rs.pts.size(); ++i) {
                GCodeLine gl;
                if (first || i == 0) {
                    gl.type = GCodeLine::Rapid;
                    gl.code = QString("G0 X%1 Y%2")
                                  .arg(rs.pts[i].x(), 0, 'f', 3)
                                  .arg(rs.pts[i].y(), 0, 'f', 3);
                    lines << gl;
                    first = false;
                } else {
                    gl.type = GCodeLine::Linear;
                    gl.code = QString("G1 X%1 Y%2")
                                  .arg(rs.pts[i].x(), 0, 'f', 3)
                                  .arg(rs.pts[i].y(), 0, 'f', 3);
                    lines << gl;
                }
            }
        }

        StockData stock = stockFromLines(lines);
        m_sim->setStock(stock);
        m_sim->loadFromGCode(lines);
        m_lblInfo->setText(fi.fileName() + "  " + formatSize(fi.size()));
    }

    void loadDxf(const QString &path)
    {
        const QFileInfo fi(path);
        CadDocument doc = CadImporter::loadDxf(path);
        if (!doc.valid) { clear(); m_lblInfo->setText("Erreur DXF"); return; }

        QVector<GCodeLine> lines;
        for (const CadContour &c : std::as_const(doc.contours)) {
            const QVector<QPointF> pts = c.toPolyline(16);
            for (int i = 0; i < pts.size(); ++i) {
                GCodeLine gl;
                gl.type = (i == 0) ? GCodeLine::Rapid : GCodeLine::Linear;
                gl.code = QString("%1 X%2 Y%3")
                              .arg(i==0?"G0":"G1")
                              .arg(pts[i].x(), 0, 'f', 3)
                              .arg(pts[i].y(), 0, 'f', 3);
                lines << gl;
            }
        }

        StockData stock = stockFromLines(lines);
        m_sim->setStock(stock);
        m_sim->loadFromGCode(lines);
        m_lblInfo->setText(fi.fileName() + "  " + formatSize(fi.size()));
    }

    // ── inferStock depuis G-code (lignes ::::) ────────────────
    StockData inferStock(const QVector<GCodeLine> &lines)
    {
        static const QRegularExpression rX(
            R"(::::.*X-\s*=\s*(-?[\d.]+).*X\+\s*=\s*(-?[\d.]+))",
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression rY(
            R"(::::.*Y-\s*=\s*(-?[\d.]+).*Y\+\s*=\s*(-?[\d.]+))",
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression rZ(
            R"(::::.*Z-\s*=\s*(-?[\d.]+).*Z\+\s*=\s*(-?[\d.]+))",
            QRegularExpression::CaseInsensitiveOption);
        StockData s; bool xOk=false,yOk=false,zOk=false;
        for (const GCodeLine &gl : std::as_const(lines)) {
            const QString raw = gl.code + " " + gl.comment;
            auto mX=rX.match(raw), mY=rY.match(raw), mZ=rZ.match(raw);
            if(mX.hasMatch()&&!xOk){s.xMin=mX.captured(1).toFloat();s.xMax=mX.captured(2).toFloat();xOk=true;}
            if(mY.hasMatch()&&!yOk){s.yMin=mY.captured(1).toFloat();s.yMax=mY.captured(2).toFloat();yOk=true;}
            if(mZ.hasMatch()&&!zOk){s.zMin=mZ.captured(1).toFloat();s.zMax=mZ.captured(2).toFloat();zOk=true;}
            if(xOk&&yOk&&zOk){s.valid=true;return s;}
        }
        if(xOk&&yOk){s.zMin=-50;s.zMax=0;s.valid=true;}
        return s;
    }

    // ── Stock depuis .enc JSON ────────────────────────────────
    StockData stockFromEnc(const QString &path)
    {
        StockData s;
        QFile f(path); if(!f.open(QIODevice::ReadOnly)) return s;
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if(err.error != QJsonParseError::NoError) return s;
        for (const auto &bv : doc.object()["blocks"].toArray()) {
            const QJsonObject b = bv.toObject();
            if(b["type"].toString() != "BlkForm") continue;
            const QJsonObject d = b.value("data").toObject();
            s.xMin=d["xMinus"].toDouble(-5); s.xMax=d["xPlus"].toDouble(200);
            s.yMin=d["yMinus"].toDouble(-5); s.yMax=d["yPlus"].toDouble(150);
            s.zMin=d["zMinus"].toDouble(-50);s.zMax=d["zPlus"].toDouble(0);
            s.valid=true; break;
        }
        return s;
    }

    // ── Stock depuis segments GCodeLine ───────────────────────
    StockData stockFromLines(const QVector<GCodeLine> &lines)
    {
        StockData s;
        double xMn=1e9,xMx=-1e9,yMn=1e9,yMx=-1e9;
        bool any=false;
        auto pv=[](const QString&c,QChar ch,double&out)->bool{
            QString cc=c; { int sc=cc.indexOf(';'); if(sc>=0) cc=cc.left(sc); }
            QString up=cc.toUpper(); int i=up.indexOf(ch.toUpper()); if(i<0)return false;
            int j=i+1; while(j<up.size()&&up[j]==' ')++j;
            int st=j; if(j<up.size()&&(up[j]=='-'||up[j]=='+'))++j;
            while(j<up.size()&&(up[j].isDigit()||up[j]=='.'))++j;
            if(st>=cc.size()||j>cc.size()||j<=st) return false;
            bool ok; out=cc.mid(st,j-st).toDouble(&ok); return ok;
        };
        double cx=0,cy=0;
        for (const GCodeLine &gl : std::as_const(lines)) {
            double tx=cx,ty=cy; bool mv=false;
            if(pv(gl.code,'X',tx))mv=true;
            if(pv(gl.code,'Y',ty))mv=true;
            if(mv){
                xMn=std::min(xMn,tx);xMx=std::max(xMx,tx);
                yMn=std::min(yMn,ty);yMx=std::max(yMx,ty);
                any=true; cx=tx; cy=ty;
            }
        }
        if(any&&xMx>xMn){
            double mg=std::max(2.0,(xMx-xMn)*0.08);
            s.xMin=xMn-mg;s.xMax=xMx+mg;
            s.yMin=yMn-mg;s.yMax=yMx+mg;
            s.zMin=-50;s.zMax=0;s.valid=true;
        }
        return s;
    }

    static QString formatSize(qint64 b)
    {
        if(b<1024) return QString("%1 o").arg(b);
        if(b<1024*1024) return QString("%1 Ko").arg(b/1024);
        return QString("%1 Mo").arg(b/(1024LL*1024));
    }
};