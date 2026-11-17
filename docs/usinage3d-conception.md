# Unité « Usinage 3D » — conception validée avec Jb (15/11/2026)

## Bloc
- Un seul bloc « Usinage 3D », tuiles Ébauche / Finition / Ébauche + Finition (comme les autres cycles).
- Entrées : fichier STEP + origine, brut = BLK FORM, outil d'ébauche, outil de finition
  (type plat / boule / torique, Ø, longueur utile), surépaisseurs latérale et fond.
- Usinage par le dessus seulement (3 axes). Retournement plus tard.

## Ébauche
- Couches réglées par la **profondeur de passe d'ébauche**.
- Ajouter automatiquement le Z des faces planes (fonds et marches à la cote).
- Contour d'une couche = ombre de la pièce AU-DESSUS de ce Z (pas de reprise sous un surplomb).
- Zone à vider = brut − (ombre + surépaisseur), vidée avec l'algorithme adaptatif du Contour.
- Chaque couche ne vide que ce qui reste.

## Garde-fou (décision de Jb)
- Là où l'outil d'ébauche est trop gros pour passer : l'ébauche laisse PLUS de matière, sans forcer.
- La finition connaît cette matière restante et en tient compte (passes supplémentaires à cet endroit).
- Avertissements : rayon de finition > rayon creux de la pièce ; longueur utile < profondeur.

## Finition
- Sa propre **profondeur de passe de finition** (distincte de celle d'ébauche).
- Stratégie 1 : balayage (allers-retours parallèles) — dessus galbé du chevalet.
- Stratégie 2 (plus tard) : Z constant pour les flancs raides.

## Sortie
- G-code CNC12 classique, vérification dans la simulation 3D d'EazyNC.
- Pièce test : C:\cncm\ncfiles\chevalet jumbo.step (57,9 × 209,6 × 12 mm).
