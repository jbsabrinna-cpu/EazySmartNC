#pragma execution_character_set("utf-8")
#include "gcodeeditor.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QFont>
#include <QScrollBar>

// ─────────────────────────────────────────────────────────────
GCodeEditor::GCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    applyStyle();
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
}

void GCodeEditor::applyStyle()
{
    setStyleSheet(
        "QPlainTextEdit {"
        "  background:#0d0f14; color:#c8d4f0; border:none;"
        "  font-family:'IBM Plex Mono','Consolas','Courier New',monospace;"
        "  font-size:13px;"
        "  selection-background-color:rgba(86,212,78,0.25);"
        "  selection-color:#ffffff;"
        "}"
        "QScrollBar:vertical {"
        "  background:#13151a; width:8px; border:none;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background:rgba(255,255,255,0.15); border-radius:4px; min-height:20px;"
        "}"
        "QScrollBar::handle:vertical:hover { background:rgba(86,212,78,0.4); }"
        "QScrollBar:horizontal {"
        "  background:#13151a; height:8px; border:none;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background:rgba(255,255,255,0.15); border-radius:4px;"
        "}"
        "QScrollBar::add-line, QScrollBar::sub-line { width:0; height:0; }");
}

// ─────────────────────────────────────────────────────────────
//  loadGCode — depuis vecteur GCodeLine
// ─────────────────────────────────────────────────────────────
void GCodeEditor::loadGCode(const QVector<GCodeLine> &lines)
{
    m_gcode = lines;
    m_highlightedLine = -1;

    QStringList text;
    for (const GCodeLine &l : lines) {
        if (l.type == GCodeLine::Blank) {
            text << "";
            continue;
        }
        QString ln = l.code;
        if (!l.comment.isEmpty() && l.type == GCodeLine::Comment)
            ln = "( " + l.comment + " )";
        else if (!l.comment.isEmpty())
            ln += "  ; " + l.comment;
        text << ln;
    }
    setPlainText(text.join('\n'));
    applySyntaxColors();
}

// ─────────────────────────────────────────────────────────────
//  loadText — depuis texte brut
// ─────────────────────────────────────────────────────────────
void GCodeEditor::loadText(const QString &text)
{
    m_gcode.clear();
    m_highlightedLine = -1;
    setPlainText(text);
    applySyntaxColors();
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::Start);
    setTextCursor(c);
}

// ─────────────────────────────────────────────────────────────
//  highlightBlock
// ─────────────────────────────────────────────────────────────
void GCodeEditor::highlightBlock(int blockId)
{
    if (blockId <= 0) {
        highlightLine(qAbs(blockId));
        return;
    }

    // Chercher "; BLOC <id>" ou "( BLOC <id>" dans le texte
    const QString marker = QString("BLOC %1").arg(blockId);
    const QStringList lines = toPlainText().split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].contains(marker, Qt::CaseInsensitive)) {
            highlightLine(i);
            return;
        }
    }
    // Fallback : chercher dans m_gcode
    for (int i = 0; i < m_gcode.size(); ++i) {
        if (m_gcode[i].blockId == blockId) {
            highlightLine(i);
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  highlightLine
// ─────────────────────────────────────────────────────────────
void GCodeEditor::highlightLine(int lineIndex)
{
    if (lineIndex < 0) return;
    m_highlightedLine = lineIndex;

    QTextBlock block = document()->begin();
    int lineNum = 0;
    while (block.isValid()) {
        QTextCursor lc(block);
        lc.select(QTextCursor::LineUnderCursor);
        const QString txt = block.text().trimmed();
        QTextCharFormat fmt;

        if (lineNum == lineIndex) {
            fmt.setBackground(QColor(86, 212, 78, 35));
            fmt.setForeground(QColor("#56d44e"));
            QFont fn = fmt.font(); fn.setBold(true); fmt.setFont(fn);
        } else {
            fmt.setBackground(Qt::transparent);
            fmt = syntaxFormat(txt);
        }

        lc.setCharFormat(fmt);
        block = block.next();
        ++lineNum;
    }

    // Scroller vers la ligne
    QTextBlock target = document()->findBlockByLineNumber(lineIndex);
    if (target.isValid()) {
        QTextCursor sc(target);
        setTextCursor(sc);
        ensureCursorVisible();
        verticalScrollBar()->setValue(
            qMax(0, verticalScrollBar()->value() - viewport()->height() / 2));
    }
}

// ─────────────────────────────────────────────────────────────
//  clearHighlight
// ─────────────────────────────────────────────────────────────
void GCodeEditor::clearHighlight()
{
    m_highlightedLine = -1;
    applySyntaxColors();
}

// ─────────────────────────────────────────────────────────────
//  applySyntaxColors — coloration complète du document
// ─────────────────────────────────────────────────────────────
void GCodeEditor::applySyntaxColors()
{
    QTextBlock block = document()->begin();
    while (block.isValid()) {
        QTextCursor lc(block);
        lc.select(QTextCursor::LineUnderCursor);
        QTextCharFormat fmt = syntaxFormat(block.text().trimmed());
        fmt.setBackground(Qt::transparent);
        lc.setCharFormat(fmt);
        block = block.next();
    }
}

// ─────────────────────────────────────────────────────────────
//  syntaxFormat — retourne le format selon le début de la ligne
// ─────────────────────────────────────────────────────────────
QTextCharFormat GCodeEditor::syntaxFormat(const QString &txt) const
{
    QTextCharFormat fmt;
    if      (txt.startsWith('(') || txt.startsWith(';'))
        fmt.setForeground(QColor("#4a5570"));   // commentaire : gris
    else if (txt.startsWith('G'))
        fmt.setForeground(QColor("#88ccff"));   // G-code : bleu cyan
    else if (txt.startsWith('M'))
        fmt.setForeground(QColor("#f0a500"));   // M-code : orange
    else if (txt.startsWith('T'))
        fmt.setForeground(QColor("#c080ff"));   // T-code : violet
    else if (txt.startsWith('S') || txt.startsWith('F'))
        fmt.setForeground(QColor("#56d44e"));   // S/F : vert
    else if (txt.startsWith('N'))
        fmt.setForeground(QColor("#607090"));   // numéro de ligne : gris foncé
    else if (txt.isEmpty())
        fmt.setForeground(QColor("#c8d4f0"));
    else
        fmt.setForeground(QColor("#c8d4f0"));   // défaut : blanc/bleu clair
    return fmt;
}
