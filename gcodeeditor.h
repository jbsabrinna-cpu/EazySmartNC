#pragma once
#pragma execution_character_set("utf-8")

#include <QPlainTextEdit>
#include <QVector>
#include <QTextCharFormat>
#include "gcodeengine.h"

// ═══════════════════════════════════════════════════════════════
//  GCodeEditor — visualiseur G-code avec coloration syntaxique
//  et surlignage de la ligne/bloc courant
// ═══════════════════════════════════════════════════════════════
class GCodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit GCodeEditor(QWidget *parent = nullptr);

    // Charger depuis vecteur GCodeLine (mode conversationnel)
    void loadGCode(const QVector<GCodeLine> &lines);

    // Charger depuis texte brut (mode fichier .nc/.cnc)
    void loadText(const QString &text);

    // Surligner le bloc ou la ligne courante
    // blockId > 0  : cherche le marqueur "; BLOC <id>"
    // blockId <= 0 : surligne la ligne numéro abs(blockId)
    void highlightBlock(int blockId);

    // Effacer le surlignage
    void clearHighlight();

private:
    void            applyStyle();
    void            applySyntaxColors();
    void            highlightLine(int lineIndex);
    QTextCharFormat syntaxFormat(const QString &txt) const;

    QVector<GCodeLine> m_gcode;
    int                m_highlightedLine = -1;
};
