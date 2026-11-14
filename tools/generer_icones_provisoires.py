#!/usr/bin/env python3
"""Crée des icônes provisoires pour les icônes TNC non publiées.

Les icônes du dossier icons/unit/ (style Heidenhain TNC) ne sont pas dans le
dépôt public. Sans elles, Qt (rcc) refuse de compiler icons.qrc.
Ce script crée une image grise portant le nom de chaque icône manquante,
pour que le projet compile. Remplacez-les ensuite par vos propres icônes.

Usage (depuis la racine du projet) :  python tools/generer_icones_provisoires.py
Nécessite Pillow :  pip install pillow
"""
import os, re
from PIL import Image, ImageDraw

racine = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
qrc = open(os.path.join(racine, "icons.qrc"), encoding="utf-8").read()
fichiers = re.findall(r"<file[^>]*>([^<]+)</file>", qrc)
crees = 0
for rel in fichiers:
    chemin = os.path.join(racine, rel)
    if os.path.exists(chemin):
        continue
    os.makedirs(os.path.dirname(chemin), exist_ok=True)
    im = Image.new("RGB", (96, 64), (200, 204, 216))
    d = ImageDraw.Draw(im)
    nom = os.path.splitext(os.path.basename(rel))[0]
    for i, ligne in enumerate([nom[k:k + 14] for k in range(0, len(nom), 14)][:4]):
        d.text((4, 4 + i * 14), ligne, fill=(40, 40, 60))
    im.save(chemin)
    crees += 1
print(f"{crees} icône(s) provisoire(s) créée(s).")
