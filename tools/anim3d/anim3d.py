"""Fabrique d'animations d'aide 3D (GIF) pour EazyNC."""
import math, numpy as np
from PIL import Image, ImageDraw, ImageFont
import rendu
from rendu import Piece, Camera, rendre, tracer_chemin

W, H = 480, 336
SS = 2
FONT_B = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_R = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
VERT, VERT_PALE, ROUGE = (0, 150, 0), (120, 190, 120), (208, 40, 40)
BLEU, GRIS = (0, 88, 200), (70, 70, 70)


def police(t, gras=False):
    return ImageFont.truetype(FONT_B if gras else FONT_R, t)


# ── Trajectoire ──────────────────────────────────────────────────
class Mouvement:
    def __init__(self, kind, pts, passe=0):
        self.kind = kind                       # 'rapide' | 'avance'
        self.pts = [np.array(p, float) for p in pts]
        self.passe = passe
        self.L = sum(np.linalg.norm(b - a) for a, b in zip(self.pts[:-1], self.pts[1:]))

    def point(self, s):
        """Point à l'abscisse curviligne s (0..L)."""
        for a, b in zip(self.pts[:-1], self.pts[1:]):
            l = np.linalg.norm(b - a)
            if s <= l or l == 0:
                return a + (b - a) * (s / l if l else 0)
            s -= l
        return self.pts[-1]

    def jusqua(self, s, pas=0.25):
        out = [self.pts[0]]
        acc = 0.0
        for a, b in zip(self.pts[:-1], self.pts[1:]):
            l = np.linalg.norm(b - a)
            if acc + l >= s:
                n = max(1, int((s - acc) / pas))
                for k in range(1, n + 1):
                    out.append(a + (b - a) * ((s - acc) * k / n / l if l else 0))
                return out
            n = max(1, int(l / pas))
            for k in range(1, n + 1):
                out.append(a + (b - a) * k / n)
            acc += l
        return out


def helice(cx, cy, R, z0, z1, tours, n=400, phase=0.0):
    return [(cx + R * math.cos(phase + 2 * math.pi * tours * i / n),
             cy + R * math.sin(phase + 2 * math.pi * tours * i / n),
             z0 + (z1 - z0) * i / n) for i in range(n + 1)]


# ── Projection et dessins 2D sur l'image finale ──────────────────
def proj(cam, p):
    q = cam.projeter(np.asarray([p], float))[0]
    return q[0] / SS, q[1] / SS


def fleche(dr, a, b, c, w=2, tete=8):
    dr.line([a, b], fill=c, width=w)
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    if L < 1:
        return
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux
    dr.polygon([b, (b[0] - ux * tete + nx * tete * 0.45, b[1] - uy * tete + ny * tete * 0.45),
                (b[0] - ux * tete - nx * tete * 0.45, b[1] - uy * tete - ny * tete * 0.45)], fill=c)


def etiquette(dr, pos, txt, c, gras=True, ancre="lm", taille=13):
    f = police(taille, gras)
    bb = dr.textbbox(pos, txt, font=f, anchor=ancre)
    dr.rounded_rectangle((bb[0] - 3, bb[1] - 2, bb[2] + 3, bb[3] + 2), 3, fill=(235, 235, 235, 225))
    dr.text(pos, txt, fill=c, font=f, anchor=ancre)


def cote(im, cam, p0, p1, txt, actif=True, ancre="lm", decal=(6, 0)):
    dr = ImageDraw.Draw(im, "RGBA")
    a, b = proj(cam, p0), proj(cam, p1)
    c = BLEU if actif else GRIS
    fleche(dr, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), a, c, 2 if actif else 1)
    fleche(dr, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), b, c, 2 if actif else 1)
    m = ((a[0] + b[0]) / 2 + decal[0], (a[1] + b[1]) / 2 + decal[1])
    etiquette(dr, m, txt, c, actif, ancre)


def niveau(im, cam, z, x0, x1, y, txt, c):
    dr = ImageDraw.Draw(im, "RGBA")
    a, b = proj(cam, (x0, y, z)), proj(cam, (x1, y, z))
    # pointillé
    n = 28
    for i in range(n):
        if i % 2:
            continue
        p = (a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n)
        q = (a[0] + (b[0] - a[0]) * (i + 1) / n, a[1] + (b[1] - a[1]) * (i + 1) / n)
        dr.line([p, q], fill=c, width=2)
    etiquette(dr, (b[0] + 5, b[1]), txt, c, False)


def titre(im, txt, sous=None):
    dr = ImageDraw.Draw(im)
    dr.text((10, 8), txt, fill=(48, 48, 48), font=police(17, True))
    if sous:
        dr2 = ImageDraw.Draw(im, "RGBA")
        etiquette(dr2, (W // 2, H - 13), sous, (60, 60, 60), False, "mm", 12)
    return im


# ── Scène ────────────────────────────────────────────────────────
class Scene:
    def __init__(self, titre, cam_pos, cam_cible, fov, r_outil, mouvements,
                 ycut=None, init=None, overlay=None, legende=None,
                 n_images=76, n_fin=10, vitesse_rapide=3.0, recouvrement=False,
                 trace=True, longueur_coupe=16.0, ortho=None):
        self.titre, self.r = titre, r_outil
        self.cam = Camera(cam_pos, cam_cible, fov, W * SS, H * SS, ortho)
        self.cam_f = self.cam   # projection overlay (÷SS)
        self.mv = mouvements
        self.ycut, self.init, self.overlay, self.legende = ycut, init, overlay, legende
        self.n, self.n_fin, self.vr = n_images, n_fin, vitesse_rapide
        self.recouvrement, self.trace = recouvrement, trace
        self.lc = longueur_coupe
        self.durees = [m.L / (self.vr if m.kind == "rapide" else 1.0) for m in mouvements]
        self.T = sum(self.durees)

    def etat(self, tau):
        """(index mouvement, abscisse) au temps tau."""
        acc = 0.0
        for i, (m, d) in enumerate(zip(self.mv, self.durees)):
            if tau <= acc + d or i == len(self.mv) - 1:
                f = 0 if d == 0 else min(1, (tau - acc) / d)
                return i, f * m.L
            acc += d
        return len(self.mv) - 1, self.mv[-1].L

    def fabriquer(self, angle_pas=0.4, log=True):
        rendu.YCUT = self.ycut
        piece = Piece()
        piece.marquer_recouvrement = self.recouvrement
        if self.init:
            self.init(piece)
        images, ang = [], 0.0
        fait = [0.0] * len(self.mv)          # abscisse déjà enlevée par mouvement
        total = self.n + self.n_fin
        for k in range(total):
            tau = self.T * min(1.0, (k + 1) / self.n)
            im_i, s = self.etat(tau)
            # enlèvement de matière jusqu'à l'état courant
            for j in range(im_i + 1):
                m = self.mv[j]
                s_fin = m.L if j < im_i else s
                if m.kind == "avance" and s_fin > fait[j] + 1e-6:
                    piece.n_passe = m.passe
                    pts = m.jusqua(s_fin, 0.2)
                    # ne reprendre que la partie nouvelle
                    acc, prev = 0.0, pts[0]
                    for p in pts[1:]:
                        acc += np.linalg.norm(p - prev); prev = p
                        if acc >= fait[j] - 0.2:
                            piece.enlever(p[0], p[1], p[2], self.r)
                    if fait[j] == 0.0:
                        piece.enlever(pts[0][0], pts[0][1], pts[0][2], self.r)
                    fait[j] = s_fin
            pos = self.mv[im_i].point(s)
            img, zb = rendre(piece, self.cam, (pos[0], pos[1], pos[2], self.r), angle=ang,
                             lc=self.lc)
            zt = rendre.z_outil
            if self.trace:
                # trajet à venir pâle, fait en couleur
                for j, m in enumerate(self.mv):
                    if self.trace == "courant" and j != im_i:
                        continue
                    if j > im_i or (j == im_i and s < m.L):
                        reste = [m.point(x) for x in np.linspace(s if j == im_i else 0, m.L, 120)]
                        img = tracer_chemin(img, zt, self.cam, reste,
                                            (235, 160, 160) if m.kind == "rapide" else VERT_PALE,
                                            2 * SS, pointille=True)
                    if j < im_i or (j == im_i and s > 0):
                        faits = [m.point(x) for x in np.linspace(0, m.L if j < im_i else s, 120)]
                        img = tracer_chemin(img, zt, self.cam, faits,
                                            ROUGE if m.kind == "rapide" else VERT,
                                            (2 if m.kind == "rapide" else 3) * SS,
                                            pointille=(m.kind == "rapide"))
            im = Image.fromarray(img).resize((W, H), Image.LANCZOS)
            if self.overlay:
                self.overlay(im, self, k / max(1, total - 1), im_i, s, piece)
            leg = self.legende(im_i, s) if callable(self.legende) else self.legende
            titre(im, self.titre, leg)
            images.append(np.array(im.convert("RGB")))
            ang += angle_pas
            if log and k % 10 == 0:
                print(" ", self.titre, k, "/", total, flush=True)
        return images

    def projeter(self, p):
        return proj(self.cam_f, p)


def gif(images, chemin, ms=66, pause_fin=700):
    sample = Image.fromarray(np.concatenate(images[::6], axis=0))
    pal = sample.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    out = [Image.fromarray(f).quantize(palette=pal, dither=Image.Dither.NONE) for f in images]
    dur = [ms] * len(out); dur[-1] = pause_fin
    out[0].save(chemin, save_all=True, append_images=out[1:], duration=dur, loop=0, disposal=1)
