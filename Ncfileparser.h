#pragma once
// ═══════════════════════════════════════════════════════════════
//  NcFileParser — Lit un fichier .nc brut et produit
//  un QVector<GCodeLine> compatible avec Sim3DWidget
//
//  Utilisation :
//    auto lines = NcFileParser::parse("/path/file.nc");
//    simPage->loadNc(lines, stockData);
// ═══════════════════════════════════════════════════════════════
#ifndef NCFILEPARSER_H
#define NCFILEPARSER_H

#include <QVector>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include "gcodeengine.h"   // GCodeLine

class NcFileParser
{
public:
    // Parse un fichier .nc et retourne les GCodeLine
    static QVector<GCodeLine> parse(const QString &filePath)
    {
        QVector<GCodeLine> result;
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return result;

        QTextStream in(&f);
        while (!in.atEnd()) {
            QString rawLine = in.readLine().trimmed();
            if (rawLine.isEmpty()) {
                GCodeLine gl; gl.type = GCodeLine::Blank; gl.code = "";
                result.append(gl);
                continue;
            }

            // Séparer code et commentaire ( ... )
            QString code    = rawLine;
            QString comment;
            int cb = rawLine.indexOf('(');
            int ce = rawLine.lastIndexOf(')');
            if (cb >= 0 && ce > cb) {
                comment = rawLine.mid(cb+1, ce-cb-1).trimmed();
                code    = (rawLine.left(cb) + rawLine.mid(ce+1)).trimmed();
            }
            // Commentaire ; ou %
            if (code.startsWith(';') || code.startsWith('%')) {
                GCodeLine gl; gl.type=GCodeLine::Comment;
                gl.code=code; gl.comment=comment;
                result.append(gl);
                continue;
            }

            if (code.isEmpty()) {
                GCodeLine gl; gl.type=GCodeLine::Comment;
                gl.code=""; gl.comment=comment;
                result.append(gl);
                continue;
            }

            GCodeLine gl;
            gl.code    = code;
            gl.comment = comment;
            gl.type    = classifyLine(code.toUpper());
            result.append(gl);
        }
        return result;
    }

private:
    static GCodeLine::Type classifyLine(const QString &upper)
    {
        static const QRegularExpression reG0 (R"(\bG0*0\b)");
        static const QRegularExpression reG1 (R"(\bG0*1\b)");
        static const QRegularExpression reG2 (R"(\bG0*2\b)");
        static const QRegularExpression reG3 (R"(\bG0*3\b)");
        static const QRegularExpression reT  (R"(\bT\d+\b)");
        static const QRegularExpression reM3 (R"(\bM0*3\b|\bM0*4\b|\bM0*5\b)");
        static const QRegularExpression reG8 (R"(\bG8[0-9]\b|\bG7[3-6]\b)");

        if (upper.contains(reG0))  return GCodeLine::Rapid;
        if (upper.contains(reG1))  return GCodeLine::Linear;
        if (upper.contains(reG2) ||
            upper.contains(reG3))  return GCodeLine::Arc;
        if (upper.contains(reT))   return GCodeLine::ToolCall;
        if (upper.contains(reM3))  return GCodeLine::Spindle;
        if (upper.contains(reG8))  return GCodeLine::Cycle;
        if (upper.startsWith('M')) return GCodeLine::MFunction;
        return GCodeLine::Other;
    }
};

#endif // NCFILEPARSER_H
