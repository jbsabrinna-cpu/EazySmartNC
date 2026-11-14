#!/usr/bin/env python3
# ===============================================================
#  eazync_scanner.py -- Scanner statique EazyNC
#  Analyse tous les .cpp/.h du projet et produit un rapport
#  Categories : CRASH, WARN, PERF, STYLE
# ===============================================================
import os, re, sys, io
from pathlib import Path
# Forcer UTF-8 sur Windows (Qt Creator utilise cp1252)
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

# -- Repertoires a scanner ---------------------------------------
# Dossier du projet EazyNC - modifier si necessaire
import os as _os
_script_dir = _os.path.dirname(_os.path.abspath(__file__))
SCAN_DIRS = [ _script_dir ]
EXCLUDE_DIRS  = {'build', 'debug', 'release', '.git', 'moc_', 'ui_'}
EXCLUDE_FILES = {'CentroidWrapper.cpp', 'CentroidWrapper.h'}

# -- Regles : (niveau, description, pattern_regex) ---------------
RULES = [
    # -- CRASH --------------------------------------------------
    # setPixelSize OK sur QPainter mais crash sur txt->setFont (QGraphicsItem)
    ('CRASH', 'setPixelSize puis txt->setFont sur QGraphicsItem (crash)',
     re.compile(r'setPixelSize.*?txt->setFont|setPixelSize.*?->setFont', re.DOTALL)),
    ('CRASH', 'setPointSize sur QGraphicsItem avec ItemIgnoresTransformations',
     re.compile(r'ItemIgnoresTransformations')),
    ('CRASH', 'addSimpleText dans une scene (problemes font Qt/coords mm)',
     re.compile(r'->addSimpleText\s*\(')),
    ('CRASH', 'Qt::UniqueConnection avec lambda (ASSERT Qt6)',
     re.compile(r'UniqueConnection')),
    ('CRASH', 'static bool dans methode Qt (partage entre instances)',
     re.compile(r'\bstatic\s+bool\s+\w+\s*=')),
    ('CRASH', 'emit signal sur pointeur externe (m_editor->signal)',
     re.compile(r'emit\s+\w+->(?!data\b|text\b|value\b)')),
    ('CRASH', 'QFont sur QGraphicsTextItem avec setPointSize',
     re.compile(r'QFont\s+\w+\s*;.*setPointSize', re.DOTALL)),
    ('CRASH', 'boucle while sur angle flottant sans borne max (risque infini)',
     re.compile(r'while\s*\(\s*a[12]\s*[<>]\s*a[12]\s*\)\s*a[12]\s*[+-]=')),
    ('CRASH', 'setDefault(true) sur bouton dans QDialog avec champs de saisie',
     re.compile(r'setDefault\s*\(\s*true\s*\)')),
    ('CRASH', 'deleteLater() appele sur parent encore actif',
     re.compile(r'deleteLater\s*\(\s*\)')),
    ('CRASH', 'dlg->exec() suivi de dlg->deleteLater() (double delete possible)',
     re.compile(r'exec\s*\(\s*\)\s*;[^}]*deleteLater', re.DOTALL)),

    # -- SIGNAL/SLOT --------------------------------------------
    ('WARN', 'connect sans verification objet null (peut crasher si widget detruit)',
     re.compile(r'connect\s*\(\s*(?!this|m_[a-zA-Z]+,)'
                r'(?:nullptr|NULL)\s*,')),
    ('WARN', 'dataChanged() emis sans garde m_syncing dans load()',
     re.compile(r'emit\s+dataChanged\(\)')),
    ('WARN', 'textChanged -> appel direct rebuildScene (cascade signaux)',
     re.compile(r'textChanged.*rebuildScene|rebuildScene.*textChanged')),
    ('WARN', 'returnPressed dans QDialog non bloque (peut declencher accept)',
     re.compile(r'returnPressed.*onValidate|onValidate.*returnPressed')),

    # -- MEMORY / LIFETIME -------------------------------------
    ('WARN', 'new sans parent Qt (fuite memoire potentielle)',
     re.compile(r'\bnew\s+Q(?:Widget|Dialog|Label|LineEdit|PushButton|Layout)\s*\('
                r'(?!\s*this|\s*\w+\s*\)|\s*parent)')),
    ('WARN', 'QGraphicsItem sans setZValue (superposition non definie)',
     re.compile(r'->addEllipse|->addLine|->addRect|->addPath|->addPolygon')),
    ('WARN', 'Acces ui-> sans verification null dans slot asynchrone',
     re.compile(r'QTimer::singleShot.*ui->')),

    # -- PERF --------------------------------------------------
    ('PERF', 'Boucle flottante for(double x=...) (erreur arrondi + warning clang)',
     re.compile(r'for\s*\(\s*double\s+\w+\s*=')),
    ('PERF', 'range-loop sans as_const sur container Qt (detachement implicite)',
     re.compile(r'for\s*\(const\s+\w[\w:<>* ]+&\s+\w+\s*:\s*(?!std::as_const)'
                r'(?!{)(?!\s*std::)(?!qAsConst)'
                r'm_\w+|for\s*\(const\s+\w[\w:<>* ]+&\s+\w+\s*:\s*'
                r'(?!std::as_const)(?!{)\w+\.(?!begin|end|c_str)\w+\(\)')),
    ('PERF', 'QString::arg() chaine .arg().arg() (utiliser multi-arg Qt6)',
     re.compile(r'\.arg\([^)]+\)\.arg\(')),
    ('PERF', 'rebuildScene() appele dans textChanged sans timer (trop frequent)',
     re.compile(r'textChanged.*[^S]rebuildScene\b')),

    # -- STYLE / MAINTENANCE ------------------------------------
    ('STYLE', 'TODO ou FIXME non resolu',
     re.compile(r'//\s*(TODO|FIXME|HACK|XXX)\b', re.IGNORECASE)),
    ('STYLE', 'qDebug() laisse en production',
     re.compile(r'\bqDebug\s*\(')),
    ('STYLE', 'printf/fprintf dans code Qt (utiliser qDebug)',
     re.compile(r'\b(printf|fprintf)\s*\(')),
    ('STYLE', 'Comparaison avec NULL (utiliser nullptr en C++11)',
     re.compile(r'[!=]=\s*NULL\b')),
    ('STYLE', 'cast C-style dans code C++ (utiliser static_cast etc.)',
     re.compile(r'\(\s*(?:int|double|float|long|char\s*\*|void\s*\*)\s*\)\s*\w')),
]

# -- Fichiers specifiques pour regles contextuelles -------------
CONTEXT_RULES = [
    # setPointSize dans un fichier qui gere une QGraphicsScene
    ('CRASH', 'setPointSize dans fichier avec QGraphicsScene (crash coords mm)',
     lambda content: 'QGraphicsScene' in content and 'setPointSize' in content),
    # emit dataChanged sans m_syncing dans une fonction load()
    ('CRASH', 'emit dataChanged() dans load() sans guard m_syncing',
     lambda content: re.search(r'void\s+\w+::load\s*\([^)]*\)[^{]*\{[^}]*emit\s+dataChanged', content, re.DOTALL) is not None),
]

# -- Scanner ----------------------------------------------------
def scan_file(path):
    try:
        content = open(path, encoding='utf-8', errors='replace').read()
    except:
        return []
    
    results = []
    lines = content.split('\n')
    
    for level, desc, pattern in RULES:
        for m in pattern.finditer(content):
            lineno = content[:m.start()].count('\n') + 1
            ctx = lines[lineno-1].strip()[:80]
            results.append((level, lineno, desc, ctx))
    
    for level, desc, checker in CONTEXT_RULES:
        if checker(content):
            results.append((level, 0, desc, '(analyse globale du fichier)'))
    
    return results

def find_cpp_files(dirs):
    files = []
    seen = set()
    for d in dirs:
        if not os.path.exists(d): continue
        for root, subdirs, filenames in os.walk(d):
            # Filtrer dossiers exclus
            subdirs[:] = [s for s in subdirs
                          if not any(e in s for e in EXCLUDE_DIRS)]
            for f in filenames:
                if f in EXCLUDE_FILES: continue
                if f.endswith(('.cpp', '.h', '.hpp')):
                    full = os.path.join(root, f)
                    # Dedupliquer (meme fichier dans uploads et outputs)
                    key = f
                    if key in seen: continue
                    seen.add(key)
                    files.append(full)
    return sorted(files)

def main():
    files = find_cpp_files(SCAN_DIRS)
    
    all_issues = {}
    counts = {'CRASH': 0, 'WARN': 0, 'PERF': 0, 'STYLE': 0}
    
    for f in files:
        issues = scan_file(f)
        if issues:
            all_issues[f] = issues
            for lvl, *_ in issues:
                counts[lvl] = counts.get(lvl, 0) + 1
    
    # Rapport
    lines_out = []
    lines_out.append('=' * 70)
    lines_out.append('  EazyNC -- RAPPORT DE DIAGNOSTIC STATIQUE')
    lines_out.append('=' * 70)
    lines_out.append(f'  Fichiers analyses : {len(files)}')
    lines_out.append(f'  CRASH  : {counts["CRASH"]:3d}  <- traiter en priorite absolue')
    lines_out.append(f'  WARN   : {counts["WARN"]:3d}')
    lines_out.append(f'  PERF   : {counts["PERF"]:3d}')
    lines_out.append(f'  STYLE  : {counts["STYLE"]:3d}')
    lines_out.append('=' * 70)
    
    for level in ['CRASH', 'WARN', 'PERF', 'STYLE']:
        first = True
        for fpath, issues in sorted(all_issues.items()):
            file_issues = [(l,d,c) for (lvl,l,d,c) in issues if lvl==level]
            if not file_issues: continue
            if first:
                lines_out.append(f'\n{"-"*70}')
                lines_out.append(f'  [{level}]')
                lines_out.append(f'{"-"*70}')
                first = False
            fname = os.path.basename(fpath)
            for lineno, desc, ctx in file_issues:
                loc = f'L{lineno}' if lineno else 'global'
                lines_out.append(f'  {fname}:{loc}')
                lines_out.append(f'    -> {desc}')
                lines_out.append(f'    code: {ctx}')
    
    lines_out.append('\n' + '=' * 70)
    report = '\n'.join(lines_out)
    print(report)
    
    out_path = _os.path.join(_script_dir, 'diagnostic_report.txt')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(report)
    print(f'\nRapport sauvegarde : {out_path}')
    return counts['CRASH']

if __name__ == '__main__':
    sys.exit(main())
EOF