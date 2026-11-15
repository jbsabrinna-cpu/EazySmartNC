"""Toutes les scènes d'aide 3D. Usage : python3 scenes.py nom [nom...]"""
import sys, math, numpy as np
from PIL import ImageDraw
from anim3d import *
import rendu

CAM_COUPE = dict(cam_pos=(62, -62, 38), cam_cible=(30, 24, -6), fov=26)
CAM_DESSUS = dict(cam_pos=(30, -30, 62), cam_cible=(30, 19, -5), fov=30)
OUT = "out"


def rect_mask(x0, x1, y0, y1, rc=0.0):
    xx, yy = np.meshgrid(rendu.GX, rendu.GY)
    m = (xx >= x0) & (xx <= x1) & (yy >= y0) & (yy <= y1)
    if rc > 0:
        for cx, cy, sx, sy in ((x0 + rc, y0 + rc, -1, -1), (x1 - rc, y0 + rc, 1, -1),
                               (x0 + rc, y1 - rc, -1, 1), (x1 - rc, y1 - rc, 1, 1)):
            coin = ((xx - cx) * sx > 0) & ((yy - cy) * sy > 0)
            m &= ~(coin & ((xx - cx) ** 2 + (yy - cy) ** 2 > rc * rc))
    return m


def disque_mask(cx, cy, r):
    xx, yy = np.meshgrid(rendu.GX, rendu.GY)
    return (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r


def rect_path(x0, x1, y0, y1, z, rc=0.0, n_arc=12):
    """Contour (centre outil) sens horaire vu de dessus, départ milieu bas."""
    if rc <= 0:
        return [((x0 + x1) / 2, y0, z), (x0, y0, z), (x0, y1, z), (x1, y1, z), (x1, y0, z), ((x0 + x1) / 2, y0, z)]
    pts = [((x0 + x1) / 2, y0, z)]
    def arc(cx, cy, a0, a1):
        for i in range(n_arc + 1):
            a = math.radians(a0 + (a1 - a0) * i / n_arc)
            pts.append((cx + rc * math.cos(a), cy + rc * math.sin(a), z))
    arc(x0 + rc, y0 + rc, 270, 180)
    arc(x0 + rc, y1 - rc, 180, 90)
    arc(x1 - rc, y1 - rc, 90, 0)
    arc(x1 - rc, y0 + rc, 0, -90)
    pts.append(((x0 + x1) / 2, y0, z))
    return pts


# ═══ Plongées ═══════════════════════════════════════════════════
def plongee(mode):
    cx, cy, r = 30.0, 20.0, 5.0
    zA, zF = 1.0, -12.0
    if mode == 0:
        R = 3.5
        mv = [Mouvement("rapide", [(cx + R, cy, 14), (cx + R, cy, zA)]),
              Mouvement("avance", helice(cx, cy, R, zA, zF, 4))]
        t, leg = "Plongée hélicoïdale", "Descente en hélice autour du centre"
    elif mode == 1:
        L, n = 12.0, 4
        pts = []
        for k in range(n + 1):
            x = cx - L / 2 if k % 2 == 0 else cx + L / 2
            pts.append((x, cy, zA + (zF - zA) * k / n))
        mv = [Mouvement("rapide", [(cx - L / 2, cy, 14), (cx - L / 2, cy, zA)]),
              Mouvement("avance", pts)]
        t, leg = "Plongée en rampe", "Descente en aller-retour sur une ligne"
    else:
        mv = [Mouvement("rapide", [(cx, cy, 14), (cx, cy, zA)]),
              Mouvement("avance", [(cx, cy, zA), (cx, cy, zF)])]
        t, leg = "Plongée directe", "Descente verticale, avance de plongée réduite"
    return Scene(t, r_outil=r, mouvements=mv, ycut=20.0, legende=leg, **CAM_COUPE)


# ═══ Passes / profondeur ════════════════════════════════════════
def passes(actif_passe):
    r = 4.0
    xs, xe = 20.0, 40.0
    ys = [16.0, 20.0, 24.0]
    mv = []
    z_prev = 0.0
    for k, z in enumerate((-4.0, -8.0, -12.0)):
        mv.append(Mouvement("rapide", [(xs, ys[0], 6 if k == 0 else 1.0), (xs, ys[0], z_prev + 1.0)]))
        pts = [(xs, ys[0], z_prev + 1.0), (xs, ys[0], z)]
        for i, y in enumerate(ys):
            a, b = (xs, xe) if i % 2 == 0 else (xe, xs)
            if i > 0:
                pts.append((a, y, z))
            pts.append((b, y, z))
        mv.append(Mouvement("avance", pts))
        mv.append(Mouvement("rapide", [pts[-1], (pts[-1][0], pts[-1][1], 1.0), (xs, ys[0], 1.0)]))
        z_prev = z

    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        for zz in (-4.0, -8.0):
            a, b = sc.projeter((14, 20, zz)), sc.projeter((46, 20, zz))
            for k in range(0, 20, 2):
                dr.line([(a[0] + (b[0] - a[0]) * k / 20, a[1] + (b[1] - a[1]) * k / 20),
                         (a[0] + (b[0] - a[0]) * (k + 1) / 20, a[1] + (b[1] - a[1]) * (k + 1) / 20)],
                        fill=(60, 60, 60, 200), width=1)
        cote(im, sc.cam_f, (49, 20, 0), (49, 20, -4), "Passe", actif_passe, "lm", (8, 0))
        cote(im, sc.cam_f, (11, 20, 0), (11, 20, -12), "Profondeur", not actif_passe, "rm", (-8, 0))

    return Scene("Profondeur de passe" if actif_passe else "Profondeur totale",
                 r_outil=r, mouvements=mv, ycut=20.0, overlay=overlay,
                 legende="La poche est vidée passe par passe", n_images=96, trace="courant", **CAM_COUPE)


# ═══ Approche / saut de bride ═══════════════════════════════════
def approche():
    zSB, zAp, zF = 12.0, 2.0, -8.0
    cx, cy, r = 38.0, 20.0, 5.0
    mv = [Mouvement("rapide", [(cx, cy, zSB), (cx, cy, zAp)]),
          Mouvement("avance", [(cx, cy, zAp), (cx, cy, zF)]),
          Mouvement("rapide", [(cx, cy, zF), (cx, cy, zSB)])]

    def init(piece):
        m = rect_mask(4, 18, 21, 38)
        piece.H[m] = 5.0
        piece.T[m] = 3

    def overlay(im, sc, ph, i, s, piece):
        niveau(im, sc.cam_f, zSB, 0, 46, 20, "Saut de bride", ROUGE)
        niveau(im, sc.cam_f, zAp, 0, 46, 20, "Z approche", BLEU)
        niveau(im, sc.cam_f, 0.0, 0, 46, 20, "Z0", GRIS)
        cote(im, sc.cam_f, (23, 20, zAp), (23, 20, 0), "approche", True, "lm", (8, -2))

    def leg(i, s):
        return ["Rapide jusqu'au Z approche", "Avance travail sous le Z approche",
                "Retour en rapide au saut de bride"][i]

    return Scene("Z approche et saut de bride", r_outil=r, mouvements=mv, ycut=20.0,
                 init=init, overlay=overlay, legende=leg, vitesse_rapide=2.0,
                 cam_pos=(70, -66, 44), cam_cible=(30, 24, -2), fov=28)


# ═══ Recouvrement ═══════════════════════════════════════════════
def recouvrement():
    r, z = 5.0, -3.0
    xs, xe = 19.0, 41.0
    ys = [12.5, 18.5, 24.5]
    mv = [Mouvement("rapide", [(xs, ys[0], 8), (xs, ys[0], 1)]),
          Mouvement("avance", [(xs, ys[0], 1), (xs, ys[0], z), (xe, ys[0], z)], passe=1)]
    for k in range(1, len(ys)):
        a, b = (xe, xs) if k % 2 else (xs, xe)
        mv.append(Mouvement("avance", [(a, ys[k - 1], z), (a, ys[k], z), (b, ys[k], z)], passe=k + 1))

    def overlay(im, sc, ph, i, s, piece):
        if i >= 2 and s > 4:
            dr = ImageDraw.Draw(im, "RGBA")
            p = sc.projeter((xs + 3, (ys[0] + ys[1]) / 2, z))
            q = (p[0] - 30, p[1] + 46)
            fleche(dr, q, p, BLEU, 2)
            etiquette(dr, q, "recouvrement", BLEU, True, "mm")

    return Scene("Recouvrement entre passes", r_outil=r, mouvements=mv, recouvrement=True,
                 overlay=overlay, legende="Zone balayée deux fois (en bleu)", n_images=84,
                 cam_pos=(30, -44, 110), cam_cible=(30, 19, -4), fov=21, ortho=19, longueur_coupe=10)


# ═══ Surépaisseurs ══════════════════════════════════════════════
def surep_lat():
    r, z, se = 4.0, -8.0, 1.5
    x0, x1, y0, y1 = 14.0, 46.0, 9.0, 31.0
    def init(piece):
        poche = rect_mask(x0, x1, y0, y1)
        brut = rect_mask(x0 + se, x1 - se, y0 + se, y1 - se)
        piece.H[brut] = z
        bande = poche & ~brut
        piece.T[bande] = 1
        piece.Zs[bande] = z
    path = rect_path(x0 + r, x1 - r, y0 + r, y1 - r, z)
    mv = [Mouvement("rapide", [(path[0][0], path[0][1], 8), (path[0][0], path[0][1], z)]),
          Mouvement("avance", path)]

    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        p = sc.projeter((x0 + se / 2, (y0 + y1) / 2, 0))
        q = (p[0] - 34, p[1] - 30)
        fleche(dr, q, p, BLEU, 2)
        etiquette(dr, q, "surép.", BLEU, True, "rm")

    leg = lambda i, s: ("Après ébauche : matière laissée sur la paroi" if i == 0
                        else "Finition : l'outil enlève la surépaisseur")
    return Scene("Surépaisseur latérale", r_outil=r, mouvements=mv, init=init, overlay=overlay,
                 legende=leg, cam_pos=(30, -42, 104), cam_cible=(30, 20, -6), fov=21, ortho=19, longueur_coupe=12)


def surep_lat_circ():
    r, z, se, R = 4.0, -8.0, 1.5, 13.0
    cx, cy = 30.0, 20.0
    def init(piece):
        poche = disque_mask(cx, cy, R)
        brut = disque_mask(cx, cy, R - se)
        piece.H[brut] = z
        bande = poche & ~brut
        piece.T[bande] = 1
        piece.Zs[bande] = z
    rc = R - r
    cercle = [(cx + rc * math.cos(math.pi - 2 * math.pi * i / 120), cy + rc * math.sin(math.pi - 2 * math.pi * i / 120), z)
              for i in range(121)]
    mv = [Mouvement("rapide", [(cx - rc, cy, 8), (cx - rc, cy, z)]), Mouvement("avance", cercle)]

    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        p = sc.projeter((cx + R - se / 2, cy, 0))
        q = (p[0] + 36, p[1] - 28)
        fleche(dr, q, p, BLEU, 2)
        etiquette(dr, q, "surép.", BLEU, True, "lm")

    leg = lambda i, s: ("Après ébauche : matière laissée sur la paroi" if i == 0
                        else "Finition : l'outil enlève la surépaisseur")
    return Scene("Surépaisseur latérale", r_outil=r, mouvements=mv, init=init, overlay=overlay,
                 legende=leg, cam_pos=(30, -42, 104), cam_cible=(30, 20, -6), fov=21, ortho=19, longueur_coupe=12)


def surep_fond():
    r, zr, zf = 5.0, -6.0, -8.0
    x0, x1, y0, y1 = 14.0, 46.0, 10.0, 30.0
    def init(piece):
        m = rect_mask(x0, x1, y0, y1)
        piece.H[m] = zr
        piece.T[m] = 1
        piece.Zs[m] = zf
    xs, xe = x0 + r, x1 - r
    ys = [y0 + r, (y0 + y1) / 2, y1 - r]
    pts = [(xs, ys[0], zr + 1), (xs, ys[0], zf)]
    for i, y in enumerate(ys):
        a, b = (xs, xe) if i % 2 == 0 else (xe, xs)
        if i > 0:
            pts.append((a, y, zf))
        pts.append((b, y, zf))
    mv = [Mouvement("rapide", [(xs, ys[0], 8), (xs, ys[0], zr + 1)]), Mouvement("avance", pts)]

    def overlay(im, sc, ph, i, s, piece):
        cote(im, sc.cam_f, (48.5, 20, zr), (48.5, 20, zf), "surép. fond", True, "lm", (8, 0))

    leg = lambda i, s: ("Après ébauche : matière laissée au fond" if i == 0
                        else "Finition du fond à la cote")
    return Scene("Surépaisseur de fond", r_outil=r, mouvements=mv, init=init, overlay=overlay,
                 ycut=20.0, legende=leg, **CAM_COUPE)


# ═══ Avalant / opposition ═══════════════════════════════════════
def fraisage(avalant):
    r, z, prise = 5.0, -10.0, 2.5
    yc = -r + prise
    xa, xb = (52.0, 8.0) if avalant else (8.0, 52.0)       # avalant M3 : avance vers -X
    mv = [Mouvement("rapide", [(xa, yc, 8), (xa, yc, z)]), Mouvement("avance", [(xa, yc, z), (xb, yc, z)])]

    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        pos = sc.mv[i].point(s)
        # flèche de rotation (horaire vu de dessus) autour du haut de la partie coupante
        zr = pos[2] + 13
        pts = []
        for k in range(26):
            a = math.radians(200 - 140 * k / 25)
            pts.append(sc.projeter((pos[0] + (r + 2.5) * math.cos(a), pos[1] + (r + 2.5) * math.sin(a), zr)))
        dr.line(pts[:-1], fill=GRIS, width=2)
        fleche(dr, pts[-3], pts[-1], GRIS, 2, 9)
        etiquette(dr, (pts[0][0] - 6, pts[0][1]), "M3", GRIS, True, "rm")
        # sens d'avance
        d = -1 if avalant else 1
        a = sc.projeter((pos[0] - 6 * d, yc - 9, 0)); b = sc.projeter((pos[0] + 6 * d, yc - 9, 0))
        fleche(dr, a, b, VERT, 3, 10)
        etiquette(dr, (b[0] + 8 * d, b[1]), "avance", VERT, True, "lm" if d > 0 else "rm")

    t = "Fraisage en avalant (M3)" if avalant else "Fraisage en opposition (M3)"
    leg = ("Matière à droite du sens d'avance : copeau épais vers mince" if avalant
           else "Matière à gauche du sens d'avance : copeau mince vers épais")
    return Scene(t, r_outil=r, mouvements=mv, overlay=overlay, legende=leg, trace=False,
                 cam_pos=(30, -60, 80), cam_cible=(30, 8, -6), fov=24, ortho=23)


# ═══ Rayon d'angle / longueur x largeur / diamètre ═════════════
def poche_contour(titre_, rc, overlay, leg):
    r, z = 3.0, -6.0
    x0, x1, y0, y1 = 14.0, 46.0, 8.0, 32.0
    def init(piece):
        m = rect_mask(x0 + 2, x1 - 2, y0 + 2, y1 - 2, max(rc - 2, 0.01))
        piece.H[m] = z
    path = rect_path(x0 + r, x1 - r, y0 + r, y1 - r, z, max(rc - r, 0))
    mv = [Mouvement("rapide", [(path[0][0], path[0][1], 8), (path[0][0], path[0][1], z)]),
          Mouvement("avance", path)]
    return Scene(titre_, r_outil=r, mouvements=mv, init=init, overlay=overlay, legende=leg,
                 cam_pos=(30, -42, 104), cam_cible=(30, 20, -5), fov=22, ortho=20, longueur_coupe=12)


def rayon():
    rc = 8.0
    def overlay(im, sc, ph, i, s, piece):
        c = (46 - rc, 32 - rc, 0)
        a = math.radians(45)
        cote_r = (c[0] + rc * math.cos(a), c[1] + rc * math.sin(a), 0)
        dr = ImageDraw.Draw(im, "RGBA")
        p0, p1 = sc.projeter(c), sc.projeter(cote_r)
        fleche(dr, p0, p1, BLEU, 2, 9)
        dr.ellipse((p0[0] - 2, p0[1] - 2, p0[0] + 2, p0[1] + 2), fill=BLEU)
        etiquette(dr, (p0[0] - 8, p0[1] + 6), "R", BLEU, True, "rm", 15)
    return poche_contour("Rayon d'angle de la poche", rc, overlay, "0 = angle vif (le rayon de l'outil reste)")


def longueur():
    def overlay(im, sc, ph, i, s, piece):
        cote(im, sc.cam_f, (14, 3, 0), (46, 3, 0), "Longueur", True, "mm", (0, 14))
        cote(im, sc.cam_f, (50, 8, 0), (50, 32, 0), "Largeur", True, "rm", (-8, 0))
    return poche_contour("Longueur x largeur", 3.0, overlay, None)


def diametre():
    r, z, R = 4.0, -6.0, 13.0
    cx, cy = 30.0, 20.0
    rc = R - r
    spi = []
    for i in range(361):
        a = 2 * math.pi * 3 * i / 360
        spi.append((cx + rc * i / 360 * math.cos(a), cy + rc * i / 360 * math.sin(a), z))
    spi += [(cx + rc * math.cos(2 * math.pi * i / 90), cy + rc * math.sin(2 * math.pi * i / 90), z) for i in range(1, 91)]
    mv = [Mouvement("rapide", [(cx, cy, 8), (cx, cy, 1)]),
          Mouvement("avance", [(cx, cy, 1), (cx, cy, z)] + spi[1:])]

    def overlay(im, sc, ph, i, s, piece):
        a = math.radians(160)
        p0 = (cx + R * math.cos(a), cy + R * math.sin(a), 0)
        p1 = (cx - R * math.cos(a), cy - R * math.sin(a), 0)
        cote(im, sc.cam_f, p0, p1, "Diamètre", True, "mm", (0, -14))

    return Scene("Diamètre de la poche", r_outil=r, mouvements=mv, overlay=overlay,
                 legende="Vidage en spirale du centre vers la paroi", n_images=90,
                 cam_pos=(30, -42, 104), cam_cible=(30, 20, -6), fov=21, ortho=19, longueur_coupe=12)


SCENES = {
    "plongee_0": lambda: plongee(0), "plongee_1": lambda: plongee(1), "plongee_2": lambda: plongee(2),
    "prof_passe": lambda: passes(True), "profondeur": lambda: passes(False),
    "approche": approche, "recouvrement": recouvrement,
    "surep_lat": surep_lat, "surep_lat_circ": surep_lat_circ, "surep_fond": surep_fond,
    "fraisage_0": lambda: fraisage(True), "fraisage_1": lambda: fraisage(False),
    "rayon_angle": rayon, "longueur_largeur": longueur, "diametre": diametre,
}

# ═══ Variantes poche circulaire ═════════════════════════════════
def spirale(cx, cy, r_max, z, tours=2.0, n=240, cercle=True):
    pts = [(cx + r_max * i / n * math.cos(2 * math.pi * tours * i / n),
            cy + r_max * i / n * math.sin(2 * math.pi * tours * i / n), z) for i in range(n + 1)]
    if cercle:
        a0 = 2 * math.pi * tours
        pts += [(cx + r_max * math.cos(a0 + 2 * math.pi * i / 90),
                 cy + r_max * math.sin(a0 + 2 * math.pi * i / 90), z) for i in range(1, 91)]
    return pts


def passes_circ(actif_passe):
    r, R, cx, cy = 4.0, 12.0, 30.0, 20.0
    mv, z_prev = [], 0.0
    for k, z in enumerate((-4.0, -8.0, -12.0)):
        mv.append(Mouvement("rapide", [(cx, cy, 6 if k == 0 else 1.0), (cx, cy, z_prev + 1.0)]))
        mv.append(Mouvement("avance", [(cx, cy, z_prev + 1.0)] + spirale(cx, cy, R - r, z)))
        mv.append(Mouvement("rapide", [mv[-1].pts[-1], (mv[-1].pts[-1][0], mv[-1].pts[-1][1], 1.0), (cx, cy, 1.0)]))
        z_prev = z

    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        for zz in (-4.0, -8.0):
            a, b = sc.projeter((cx - R, 20, zz)), sc.projeter((cx + R, 20, zz))
            for k in range(0, 20, 2):
                dr.line([(a[0] + (b[0] - a[0]) * k / 20, a[1] + (b[1] - a[1]) * k / 20),
                         (a[0] + (b[0] - a[0]) * (k + 1) / 20, a[1] + (b[1] - a[1]) * (k + 1) / 20)],
                        fill=(60, 60, 60, 200), width=1)
        cote(im, sc.cam_f, (47, 20, 0), (47, 20, -4), "Passe", actif_passe, "lm", (8, 0))
        cote(im, sc.cam_f, (13, 20, 0), (13, 20, -12), "Profondeur", not actif_passe, "rm", (-8, 0))

    return Scene("Profondeur de passe" if actif_passe else "Profondeur totale",
                 r_outil=r, mouvements=mv, ycut=20.0, overlay=overlay,
                 legende="La poche est vidée passe par passe", n_images=96, trace="courant", **CAM_COUPE)


def recouvrement_circ():
    r, z, cx, cy = 5.0, -3.0, 30.0, 20.0
    pas = 6.0
    mv = [Mouvement("rapide", [(cx, cy, 8), (cx, cy, 1)]),
          Mouvement("avance", [(cx, cy, 1), (cx, cy, z)], passe=1)]
    for k, rr in enumerate((pas, 2 * pas), start=2):
        cercle = [(cx + rr * math.cos(2 * math.pi * i / 100), cy + rr * math.sin(2 * math.pi * i / 100), z)
                  for i in range(101)]
        mv.append(Mouvement("avance", [(cx + rr - pas, cy, z)] + cercle, passe=k))

    def overlay(im, sc, ph, i, s, piece):
        if i >= 2 and s > 6:
            dr = ImageDraw.Draw(im, "RGBA")
            p = sc.projeter((cx - (pas - r + r) / 2 - 1.5, cy - 0.5, z))
            q = (p[0] - 50, p[1] + 52)
            fleche(dr, q, p, BLEU, 2)
            etiquette(dr, q, "recouvrement", BLEU, True, "mm")

    return Scene("Recouvrement entre passes", r_outil=r, mouvements=mv, recouvrement=True,
                 overlay=overlay, legende="Zone balayée deux fois (en bleu)", n_images=84,
                 cam_pos=(30, -44, 110), cam_cible=(30, 19, -4), fov=21, ortho=19, longueur_coupe=10)


def surep_fond_circ():
    r, zr, zf, R, cx, cy = 5.0, -6.0, -8.0, 13.0, 30.0, 20.0
    def init(piece):
        m = disque_mask(cx, cy, R)
        piece.H[m] = zr
        piece.T[m] = 1
        piece.Zs[m] = zf
    mv = [Mouvement("rapide", [(cx, cy, 8), (cx, cy, zr + 1)]),
          Mouvement("avance", [(cx, cy, zr + 1)] + spirale(cx, cy, R - r, zf))]

    def overlay(im, sc, ph, i, s, piece):
        cote(im, sc.cam_f, (47.5, 20, zr), (47.5, 20, zf), "surép. fond", True, "lm", (8, 0))

    leg = lambda i, s: ("Après ébauche : matière laissée au fond" if i == 0
                        else "Finition du fond à la cote")
    return Scene("Surépaisseur de fond", r_outil=r, mouvements=mv, init=init, overlay=overlay,
                 ycut=20.0, legende=leg, **CAM_COUPE)


SCENES.update({
    "prof_passe_circ": lambda: passes_circ(True), "profondeur_circ": lambda: passes_circ(False),
    "recouvrement_circ": recouvrement_circ, "surep_fond_circ": surep_fond_circ,
})


# ═══ Fraisage de trou ═══════════════════════════════════════════
FT = dict(cx=30.0, cy=20.0, R=10.0, Rp=4.0, r=4.0, prof=12.0, pas=3.0, pilote=15.0)


def ft_mouvements_helice(cx, cy, Rc, prof, pas):
    tours = (prof + 1.0) / pas
    hel = helice(cx, cy, Rc, 1.0, -prof, tours, n=int(120 * tours), phase=math.pi)
    a0 = math.pi + 2 * math.pi * tours
    fond = [(cx + Rc * math.cos(a0 + 2 * math.pi * i / 80), cy + Rc * math.sin(a0 + 2 * math.pi * i / 80), -prof)
            for i in range(1, 81)]
    return [Mouvement("rapide", [(cx - Rc, cy, 8), (cx - Rc, cy, 1.0)]),
            Mouvement("avance", hel + fond + [(cx, cy, -prof)]),
            Mouvement("rapide", [(cx, cy, -prof), (cx, cy, 8)])]


def ft_trou(actif):
    c = FT
    cx, cy, R, Rp, r, prof, pas = c["cx"], c["cy"], c["R"], c["Rp"], c["r"], c["prof"], c["pas"]
    def init(piece):
        m = disque_mask(cx, cy, Rp)
        piece.H[m] = -c["pilote"]
    mv = ft_mouvements_helice(cx, cy, R - r, prof, pas)

    def overlay(im, sc, ph, i, s, piece):
        cote(im, sc.cam_f, (cx - R, cy, 1.5), (cx + R, cy, 1.5), "Ø fini", actif == "fini", "mm", (0, -13))
        cote(im, sc.cam_f, (cx - Rp, cy, -c["pilote"] - 1.5), (cx + Rp, cy, -c["pilote"] - 1.5),
             "Ø avant-trou", actif == "avant", "mm", (0, 13))
        cote(im, sc.cam_f, (cx + R + 4, cy, 0), (cx + R + 4, cy, -prof), "Profondeur", actif == "prof", "lm", (8, 0))
        cote(im, sc.cam_f, (cx - R - 2, cy, -3), (cx - R - 2, cy, -3 - pas), "Pas", actif == "pas", "rm", (-8, 0))

    titres = {"fini": "Diamètre fini", "avant": "Diamètre de l'avant-trou",
              "prof": "Profondeur du trou", "pas": "Pas hélicoïdal"}
    legs = {"fini": "L'outil tourne en hélice jusqu'au diamètre fini",
            "avant": "Avant-trou percé avant le fraisage",
            "prof": "Profondeur depuis le dessus de la pièce",
            "pas": "Descente par tour d'hélice"}
    return Scene(titres[actif], r_outil=r, mouvements=mv, init=init, overlay=overlay, ycut=cy,
                 legende=legs[actif], n_images=96, **CAM_COUPE)


def ft_finition(mode):
    """mode : 'surep' | 'passe' | 'helico' | 'directe'"""
    c = FT
    cx, cy, R, r, prof, pas = c["cx"], c["cy"], c["R"], c["r"], c["prof"], c["pas"]
    se = 1.0
    def init(piece):
        brut = disque_mask(cx, cy, R - se)
        piece.H[brut] = -prof
        bande = disque_mask(cx, cy, R) & ~brut
        piece.T[bande] = 1
        piece.Zs[bande] = -prof
    if mode == "directe":
        Rc = R - r
        cercle = [(cx + Rc * math.cos(math.pi + 2 * math.pi * i / 100), cy + Rc * math.sin(math.pi + 2 * math.pi * i / 100), -prof)
                  for i in range(101)]
        mv = [Mouvement("rapide", [(cx, cy, 8), (cx, cy, 1.0)]),
              Mouvement("avance", [(cx, cy, 1.0), (cx, cy, -prof), (cx - Rc, cy, -prof)] + cercle + [(cx, cy, -prof)]),
              Mouvement("rapide", [(cx, cy, -prof), (cx, cy, 8)])]
    else:
        mv = ft_mouvements_helice(cx, cy, R - r, prof, pas)

    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        p = sc.projeter((cx + R - se / 2, cy + 0.2, -prof / 2))
        q = (p[0] + 46, p[1] - 30)
        fleche(dr, q, p, BLEU, 2)
        etiquette(dr, q, "passe finition" if mode == "passe" else "surép.", BLEU, True, "lm")

    titres = {"surep": "Surépaisseur latérale", "passe": "Passe de finition",
              "helico": "Descente finition hélicoïdale", "directe": "Descente finition directe"}
    def leg(i, s):
        if i == 0:
            return "Après ébauche : matière laissée sur la paroi"
        return {"helico": "Finition en hélice : convient aux fraises courtes",
                "directe": "Descente au centre puis un tour à la cote (LU suffisante)"}.get(
                    mode, "Finition : l'outil enlève la surépaisseur")
    return Scene(titres[mode], r_outil=r, mouvements=mv, init=init, overlay=overlay, ycut=cy,
                 legende=leg, n_images=90, **CAM_COUPE)


SCENES.update({
    "ft_diam_fini": lambda: ft_trou("fini"), "ft_diam_avant": lambda: ft_trou("avant"),
    "ft_profondeur": lambda: ft_trou("prof"), "ft_pas_helico": lambda: ft_trou("pas"),
    "ft_surep_lat": lambda: ft_finition("surep"), "ft_passe_fin": lambda: ft_finition("passe"),
    "ft_desc_helico": lambda: ft_finition("helico"), "ft_desc_directe": lambda: ft_finition("directe"),
})


# ═══ Avances / entrée-sortie / angle / correction ═══════════════
def avance(mode):
    r = 5.0
    if mode == "contour":
        z = -5.0
        def init(piece):
            piece.H[rect_mask(10, 50, 14, 26)] = 0.0
        mv = [Mouvement("rapide", [(12, 20, 8), (12, 20, z)]), Mouvement("avance", [(12, 20, z), (48, 20, z)])]
        def overlay(im, sc, ph, i, s, piece):
            if i == 1:
                dr = ImageDraw.Draw(im, "RGBA")
                pos = sc.mv[1].point(s)
                a, b = sc.projeter((pos[0] - 8, 11, 0)), sc.projeter((pos[0] + 4, 11, 0))
                fleche(dr, a, b, VERT, 4, 12)
                etiquette(dr, (a[0] - 6, a[1]), "F avance", VERT, True, "rm", 14)
        return Scene("Avance de travail", r_outil=r, mouvements=mv, init=init, overlay=overlay,
                     legende="Vitesse de coupe dans la matière (mm/min ou mm/dent)", trace=False,
                     cam_pos=(30, -50, 70), cam_cible=(30, 18, -5), fov=24, ortho=21)
    if mode == "plongee":
        mv = [Mouvement("rapide", [(30, 20, 14), (30, 20, 1)]), Mouvement("avance", [(30, 20, 1), (30, 20, -12)])]
        def overlay(im, sc, ph, i, s, piece):
            if i == 1:
                dr = ImageDraw.Draw(im, "RGBA")
                pos = sc.mv[1].point(s)
                a, b = sc.projeter((38, 20, pos[2] + 6)), sc.projeter((38, 20, pos[2]))
                fleche(dr, a, b, VERT, 4, 12)
                etiquette(dr, (a[0] + 8, a[1]), "F plongée", VERT, True, "lm", 14)
        return Scene("Avance de plongée", r_outil=r, mouvements=mv, ycut=20.0, overlay=overlay,
                     legende="Vitesse de descente dans la matière (souvent réduite)", **CAM_COUPE)
    # rampe (pendulaire)
    sc = plongee(1)
    def overlay(im, sc, ph, i, s, piece):
        if i == 1:
            dr = ImageDraw.Draw(im, "RGBA")
            pos = sc.mv[1].point(s); nxt = sc.mv[1].point(min(sc.mv[1].L, s + 3))
            a, b = sc.projeter((pos[0], 15.5, pos[2])), sc.projeter((nxt[0], 15.5, nxt[2]))
            if abs(a[0] - b[0]) + abs(a[1] - b[1]) > 2:
                fleche(dr, a, b, VERT, 4, 12)
            etiquette(dr, (b[0] + 10, b[1] - 18), "F rampe", VERT, True, "lm", 14)
    sc.overlay = overlay
    sc.titre = "Avance en rampe"
    sc.legende = "Vitesse de l'aller-retour en descente"
    return sc


def rayon_es():
    r, z = 4.0, -6.0
    x0, x1, y0, y1 = 14.0, 46.0, 8.0, 32.0
    re = 5.0
    def init(piece):
        piece.H[rect_mask(x0 + 1.5, x1 - 1.5, y0 + 1.5, y1 - 1.5)] = z
    yb = y0 + r
    xm = (x0 + x1) / 2
    cx_e, cy_e = xm, yb + re                      # centre de l'arc d'entrée
    ent = [(cx_e + re * math.cos(math.radians(a)), cy_e + re * math.sin(math.radians(a)), z) for a in range(0, -91, -5)]
    ent = [(cx_e + re, cy_e, z)] + ent             # départ dans la poche
    tour = [(xm, yb, z), (x0 + r, yb, z), (x0 + r, y1 - r, z), (x1 - r, y1 - r, z), (x1 - r, yb, z), (xm, yb, z)]
    sor = [(cx_e - re * math.cos(math.radians(a)), cy_e + re * math.sin(math.radians(a)), z) for a in range(-90, 1, 5)]
    mv = [Mouvement("rapide", [(cx_e + re, cy_e, 8), (cx_e + re, cy_e, z)]),
          Mouvement("avance", ent + tour[1:] + sor)]
    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        c = sc.projeter((cx_e, cy_e, z))
        p = sc.projeter((cx_e + re * math.cos(math.radians(-45)), cy_e + re * math.sin(math.radians(-45)), z))
        fleche(dr, c, p, BLEU, 2, 9)
        etiquette(dr, (c[0] - 6, c[1] - 10), "R", BLEU, True, "rm", 15)
    return Scene("Rayon d'entrée / sortie", r_outil=r, mouvements=mv, init=init, overlay=overlay,
                 legende="L'outil arrive et repart en arc tangent à la paroi",
                 cam_pos=(30, -42, 104), cam_cible=(30, 20, -5), fov=22, ortho=17, longueur_coupe=12)


def angulaire():
    r, z = 3.0, -6.0
    cx, cy, L, l, ang = 30.0, 20.0, 30.0, 16.0, 20.0
    ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    def loc(u, v):
        return (cx + u * ca - v * sa, cy + u * sa + v * ca)
    def init(piece):
        xx, yy = np.meshgrid(rendu.GX, rendu.GY)
        u = (xx - cx) * ca + (yy - cy) * sa
        v = -(xx - cx) * sa + (yy - cy) * ca
        piece.H[(np.abs(u) <= L / 2 - 1.5) & (np.abs(v) <= l / 2 - 1.5)] = z
    a, b = L / 2 - r, l / 2 - r
    path = [(*loc(0, -b), z), (*loc(-a, -b), z), (*loc(-a, b), z), (*loc(a, b), z), (*loc(a, -b), z), (*loc(0, -b), z)]
    mv = [Mouvement("rapide", [(*loc(0, -b), 8), (*loc(0, -b), z)]), Mouvement("avance", path)]
    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        o = sc.projeter((cx, cy, 0))
        ax = sc.projeter((cx + 20, cy, 0))
        ar = sc.projeter((*loc(20, 0), 0))
        for k in range(0, 10, 2):
            dr.line([(o[0] + (ax[0] - o[0]) * k / 10, o[1] + (ax[1] - o[1]) * k / 10),
                     (o[0] + (ax[0] - o[0]) * (k + 1) / 10, o[1] + (ax[1] - o[1]) * (k + 1) / 10)], fill=GRIS, width=2)
        dr.line([o, ar], fill=BLEU, width=2)
        arc = [sc.projeter((cx + 16 * math.cos(math.radians(t)), cy + 16 * math.sin(math.radians(t)), 0)) for t in range(0, int(ang) + 1, 2)]
        dr.line(arc, fill=BLEU, width=2)
        fleche(dr, arc[-3], arc[-1], BLEU, 2, 8)
        etiquette(dr, (arc[len(arc) // 2][0] + 10, arc[len(arc) // 2][1]), "angle", BLEU, True, "lm")
    return Scene("Position angulaire", r_outil=r, mouvements=mv, init=init, overlay=overlay,
                 legende="La poche est tournée autour de son centre (axe X = 0°)",
                 cam_pos=(30, -42, 104), cam_cible=(30, 20, -5), fov=22, ortho=17, longueur_coupe=12)


def surep_fin():
    sc = surep_lat()
    x0 = 14.0
    def overlay(im, sc_, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        p = sc_.projeter((x0, 20, 0))
        fleche(dr, (p[0], p[1]), (p[0] + 34, p[1]), ROUGE, 3, 9)
        etiquette(dr, (p[0] + 38, p[1]), "+", ROUGE, True, "lm", 16)
        fleche(dr, (p[0], p[1]), (p[0] - 34, p[1]), (0, 150, 0), 3, 9)
        etiquette(dr, (p[0] - 38, p[1]), "−", (0, 150, 0), True, "rm", 16)
    sc.overlay = overlay
    sc.titre = "Correction de finition"
    sc.legende = lambda i, s: "+ laisse de la matière   /   − enlève plus"
    return sc


# ═══ Contournage extérieur ══════════════════════════════════════
CNT = dict(x0=20.0, x1=40.0, y0=13.0, y1=27.0, r=4.0)


def cnt_passes(actif):
    c = CNT
    r = c["r"]
    mv = []
    for k, z in enumerate((-3.0, -6.0, -9.0)):
        path = rect_path(c["x0"] - r, c["x1"] + r, c["y0"] - r, c["y1"] + r, z, r)
        st = path[0]
        mv.append(Mouvement("rapide", [(st[0], st[1] - 6, 6 if k == 0 else 1.0), (st[0], st[1] - 6, z)]))
        mv.append(Mouvement("avance", [(st[0], st[1] - 6, z)] + path))
        mv.append(Mouvement("rapide", [path[-1], (path[-1][0], path[-1][1], 1.0)]))
    def overlay(im, sc, ph, i, s, piece):
        cote(im, sc.cam_f, (52, 20, 0), (52, 20, -3), "Passe", actif == "passe", "lm", (8, -6))
        cote(im, sc.cam_f, (4, 20, 0), (4, 20, -9), "Profondeur", actif == "prof", "lm", (8, 0))
    return Scene("Profondeur de passe" if actif == "passe" else "Profondeur du contour",
                 r_outil=r, mouvements=mv, ycut=20.0, overlay=overlay, trace="courant",
                 legende="Contournage extérieur, passe par passe", n_images=96, **CAM_COUPE)


def cnt_surep(label):
    c = CNT
    r, z, se = c["r"], -8.0, 1.2
    def init(piece):
        autour = rect_mask(c["x0"] - 2 * r - 2, c["x1"] + 2 * r + 2, c["y0"] - 2 * r - 2, c["y1"] + 2 * r + 2, 2 * r)
        bossage = rect_mask(c["x0"] - se, c["x1"] + se, c["y0"] - se, c["y1"] + se)
        piece.H[autour & ~bossage] = z
        bande = bossage & ~rect_mask(c["x0"], c["x1"], c["y0"], c["y1"])
        piece.T[bande] = 1
        piece.Zs[bande] = z
    path = rect_path(c["x0"] - r, c["x1"] + r, c["y0"] - r, c["y1"] + r, z, r)
    mv = [Mouvement("rapide", [(path[0][0], path[0][1] - 4, 8), (path[0][0], path[0][1] - 4, z)]),
          Mouvement("avance", [(path[0][0], path[0][1] - 4, z)] + path)]
    def overlay(im, sc, ph, i, s, piece):
        dr = ImageDraw.Draw(im, "RGBA")
        p = sc.projeter((c["x0"] - se / 2, 20, 0))
        q = (p[0] - 40, p[1] - 34)
        fleche(dr, q, p, BLEU, 2)
        etiquette(dr, q, label, BLEU, True, "rm")
    leg = lambda i, s: ("Après ébauche : matière laissée sur le contour" if i == 0
                        else "Finition : l'outil enlève la surépaisseur")
    return Scene("Surépaisseur latérale" if label == "surép." else "Passe de finition",
                 r_outil=r, mouvements=mv, init=init, overlay=overlay, legende=leg,
                 cam_pos=(30, -42, 104), cam_cible=(30, 20, -6), fov=21, ortho=19, longueur_coupe=12)


SCENES.update({
    "avance": lambda: avance("contour"), "avance_plongee": lambda: avance("plongee"),
    "avance_rampe": lambda: avance("rampe"), "rayon_es": rayon_es, "angulaire": angulaire,
    "surep_fin": surep_fin,
    "cnt_passe": lambda: cnt_passes("passe"), "cnt_profondeur": lambda: cnt_passes("prof"),
    "cnt_surep_lat": lambda: cnt_surep("surép."), "cnt_passe_fin": lambda: cnt_surep("passe finition"),
})


def passe_fin_poche(circ):
    sc = surep_lat_circ() if circ else surep_lat()
    sc.titre = "Passe de finition"
    base = sc.overlay
    if circ:
        cx, R, se = 30.0, 13.0, 1.5
        def overlay(im, sc_, ph, i, s, piece):
            dr = ImageDraw.Draw(im, "RGBA")
            p = sc_.projeter((cx + R - se / 2, 20.0, 0))
            q = (p[0] + 36, p[1] - 28)
            fleche(dr, q, p, BLEU, 2)
            etiquette(dr, q, "passe finition", BLEU, True, "lm")
    else:
        def overlay(im, sc_, ph, i, s, piece):
            dr = ImageDraw.Draw(im, "RGBA")
            p = sc_.projeter((14.0 + 0.75, 20.0, 0))
            q = (p[0] + 50, p[1] - 40)
            fleche(dr, q, p, BLEU, 2)
            etiquette(dr, q, "passe finition", BLEU, True, "lm")
    sc.overlay = overlay
    return sc


SCENES.update({"passe_fin": lambda: passe_fin_poche(False), "passe_fin_circ": lambda: passe_fin_poche(True)})


if __name__ == "__main__":
    import os, time
    os.makedirs(OUT, exist_ok=True)
    apercu = "--apercu" in sys.argv
    for nom in [a for a in sys.argv[1:] if not a.startswith("--")]:
        t0 = time.time()
        sc = SCENES[nom]()
        if apercu:
            sc.n, sc.n_fin = 6, 0
            imgs = sc.fabriquer(log=False)
            from PIL import Image
            Image.fromarray(np.concatenate([np.concatenate(imgs[0:3], 1), np.concatenate(imgs[3:6], 1)], 0)).save(f"{OUT}/apercu_{nom}.png")
        else:
            imgs = sc.fabriquer()
            gif(imgs, f"{OUT}/{nom}.gif")
        print(nom, "ok", round(time.time() - t0), "s", flush=True)
