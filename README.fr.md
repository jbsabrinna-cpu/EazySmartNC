# EazySmartNC

🇬🇧 [English version](README.md)

**FAO conversationnelle pour Centroid CNC12, dans l'esprit du SmartNC Heidenhain.**

EazySmartNC (application *EazyNC*) permet de programmer une fraiseuse CNC **sans écrire de G-code** : on remplit des blocs (matière, poches, perçages, contournages…), le logiciel calcule les trajectoires, les simule en 3D et génère un programme `.nc` prêt pour **Centroid CNC12** (Acorn, AcornSix, Oak…).

Écrit en C++ / Qt 6 par un fraiseur, pour des fraiseurs.

> [!WARNING]
> **Logiciel en phase de test.** EazySmartNC est en cours de développement et de test. Vérifiez toujours le programme en simulation avant d'usiner, et faites le premier passage avec prudence (avance réduite, Z de sécurité).
>
> **Configuration de test du développeur :** Intel Core i3-12100F, 16 Go de RAM, carte graphique NVIDIA RTX 3060, Windows 11, Centroid CNC12 Acorn Mill v5.42.10 avec carte Acorn.
>
> Le logiciel n'a pas encore été essayé sur des PC plus modestes. Avec une carte graphique intégrée (Intel, AMD) ou peu de mémoire, la simulation 3D et le calcul de l'usinage 3D peuvent être lents. Il faut un pilote graphique à jour (OpenGL).
>
> **Liaison avec CNC12 (API Centroid) :** les appels à l'API ont été corrigés, EazyNC arrive maintenant à charger le programme dans CNC12. Aucun essai d'usinage n'a encore été fait avec cette liaison. D'autres corrections des appels à l'API sont prévues dans un futur proche.

![Simulation d'une poche étagée avec îlots](docs/images/simulation-poche-etagee.gif)

---

## Aperçu

### Poche de contour avec îlots

Une ligne par contour : **Poche** ou **Îlot**, avec Z départ et profondeur. L'ordre de la liste compte, comme des calques. Le cycle découpe la hauteur en tranches à chaque cote (fonds de poche, dessus d'îlots) et calcule la zone à vider à chaque passe.

![Saisie d'une poche contour](docs/images/saisie-poche-contour.gif)

### Convertisseur DXF

Sélection d'un contour d'un clic (enchaînement automatique, sens d'usinage, éléments à cocher façon TNC) et des positions de perçage au cadre, avec un filtre par diamètre.

![Convertisseur DXF](docs/images/convertisseur-dxf.gif)

### Aide animée en 3D

Chaque champ de saisie affiche une courte animation 3D dans le cadre AIDE : modes de plongée, profondeur de passe, Z approche et saut de bride, recouvrement, surépaisseurs, avalant / opposition, rayon d'entrée, fraisage de trou… Les poches rondes ont des animations rondes. Les animations sont fabriquées par un petit outil Python fourni dans `tools/anim3d`.

![Aide animée : plongée hélicoïdale](docs/images/aide-plongee-helice.gif)

### Usinage 3D depuis un STEP

Mise en place de la pièce façon Fusion, en trois étapes : le sens des axes machine en cliquant les faces de la boîte pièce (le 3e axe est déduit, l'origine ne bouge pas), l'origine sur la boîte du brut, sur la boîte de la pièce (pavé 3×3 + dessus / milieu / dessous) ou sur un point de la pièce, puis le brut autour de la pièce ou en dimensions fixes. « Calculer et figer » remplit le BLK FORM.

![Mise en place du STEP : sens des axes](docs/images/mise-en-place-axes.png)

![Mise en place du STEP : origine sur la boîte de la pièce](docs/images/mise-en-place-origine.png)

Le bloc Usinage 3D fait ensuite l'ébauche couche par couche (un contour par couche, depuis le brut) et la finition couche par couche à la fraise boule, avec l'origine et le brut figés du BLK FORM.

![Bloc Usinage 3D](docs/images/bloc-usinage3d.png)

### Simulation 3D

Enlèvement de matière en temps réel, une couleur par outil, lecture ligne à ligne du G-code.

![Simulation d'un corps de Telecaster](docs/images/simulation-telecaster.png)

---

## Fonctionnalités

**Programmation par blocs**
- Matière (BLK FORM) avec paramètres hérités par tous les blocs : Z approche, saut de bride, Z liaison, origine.
- Sélection, copie, coupe et insertion de plusieurs blocs.
- Fichiers `.enc` (programme), `.cnt` (contours) et `.pnt` (modèles de points).

**Cycles**
- Poche rectangulaire et poche circulaire.
- **Poche de contour** avec îlots, poches étagées, îlots plus bas que la surface, et Z départ par contour.
- Contournage : extérieur avec ébauche adaptative depuis le brut, ou R0 / RL / RR. Entrées et sorties perpendiculaires, en arc ou en ligne tangente.
- Fraisage de trou (alésage hélicoïdal).
- Perçage simple et débourrage, pointage, alésage.
- Taraudage : cycle à venir.
- Plongée directe, en hélice ou en rampe, à l'avance pendulaire.
- Ébauche et finition séparées : surépaisseurs latérale et fond, finition des fonds et des parois.
- Avalant ou opposition.
- Compensation calculée par EazyNC (Clipper2) : le G-code reste en G40.

**Outils et import**
- Table d'outils : rayon, nombre de dents, angle de rampe. Avances en mm/min, mm/tr ou mm/dent, et vitesse de coupe.
- Convertisseur DXF : contours, positions, origine, mm/pouce, tolérance d'enchaînement.
- Import STEP/IGES optionnel via OpenCASCADE.

**Simulation et machine**
- Simulation 3D (OpenGL) avec légende par outil.
- Pages Mode manuel, Exécution et MDI reliées à CNC12 via l'API Centroid (optionnel).

---

## Compilation

**Environnement testé** : Windows 11, **Qt 6.8 (MSVC 2022, 64 bits)**, Qt Creator, sur Intel Core i3-12100F, 16 Go de RAM, NVIDIA RTX 3060, avec Centroid CNC12 Acorn Mill v5.42.10.

1. Cloner le dépôt et ouvrir `EazyNC.pro` dans Qt Creator.
2. **Icônes TNC** : elles ne sont pas fournies (voir plus bas). Créer des icônes provisoires pour que le projet compile :
   ```
   pip install pillow
   python tools/generer_icones_provisoires.py
   ```
3. Compiler et lancer (kit *Desktop Qt 6.8 MSVC2022 64bit*).
4. Copier `eazync.msg` dans un dossier `lang/` à côté de l'exécutable : ce sont les textes de l'interface.

**Options qmake** (*Projets > Build > qmake > Arguments supplémentaires*)
- `CONFIG+=WITH_OCC` : import STEP/IGES. Nécessite OpenCASCADE via vcpkg ; adapter `VCPKG_ROOT` dans `EazyNC.pro`.
- `DEFINES+=WITH_CENTROID_API` : liaison temps réel avec CNC12. Nécessite le wrapper `CentroidWrapper` (sources dans `CentroidWrapper/`) et la DLL de l'API Centroid, fournie avec l'installation de CNC12.

Sans ces options, EazyNC fonctionne en mode autonome : programmation, simulation et génération du `.nc`.

---

## Ressources non incluses

Le dépôt est public. Ces éléments appartiennent à Heidenhain et ne sont **pas** publiés :

| Élément | Effet | Que faire |
|---|---|---|
| Police `TNC640Global.ttf` | L'interface utilise une police de remplacement | Rien d'obligatoire. Si vous disposez de la police, placez-la à la racine du projet. |
| Icônes `icons/unit/` (94 fichiers, style TNC) | `icons.qrc` ne compile pas sans elles | Lancer `tools/generer_icones_provisoires.py`, ou fournir vos propres icônes (liste dans `docs/icones-tnc-a-fournir.txt`). |

---

## Organisation du code

```
EazyNC.pro                 projet Qt
mainwindow.*               fenêtre principale, softkeys, navigation
programmepage.*            liste des blocs, génération du G-code
uniteditor.* / UnitEditor.ui   pages de saisie des blocs
cycles/                    générateurs de cycles (un fichier par cycle)
gcodeengine.*              en-tête et fin de programme, structures de données
clipper2/ clipper2utils.*  calcul des décalages et booléens (Clipper2)
cadviewwidget.* cadimporter.*   convertisseur DXF / STEP
sim3dwidget.* simulationpage.h  simulation 3D
tooldatabase.* tooltabledialog.*  table d'outils
modules/ CentroidWrapper/  liaison avec CNC12 (optionnel)
icons/                     icônes et images d'aide
docs/images/               captures de ce README
```

---

## Feuille de route

- [ ] Cycle de pré-perçage (avant-trous pour la plongée directe)
- [x] Avalant / opposition dans le cycle de contournage
- [x] Aide animée en 3D pour les champs de saisie
- [ ] Vue 3D STEP : origine par faces et arêtes, reconnaissance des splines
- [ ] Mode pouce (G20)
- [ ] Réorganisation du code en `src/`, `ui/`, `resources/`

---

## Contribuer

Les retours de fraiseurs et de développeurs sont les bienvenus : ouvrez une *Issue* pour un bug ou une idée de cycle, ou une *Pull Request*.

Développeur : **JB**, fraiseur sur commande numérique (Heidenhain TNC 530 / 620 / 640).
