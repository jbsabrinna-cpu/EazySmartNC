"""Petit moteur de rendu 3D (lancer de rayons sur une carte de hauteur)
pour fabriquer les animations d'aide d'EazyNC.

Repère : X droite, Y fond, Z haut (mm). La matière est une carte de
hauteur H(x, y) ; l'outil est un cylindre vertical (partie coupante
avec goujures + queue).
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont

# ── Pièce ────────────────────────────────────────────────────────
X0, X1, Y0, Y1, ZB = 0.0, 60.0, 0.0, 40.0, -22.0
YCUT = None            # plan de coupe (y) : la matière devant est retirée
RES = 0.1                              # pas de la carte de hauteur (mm)
NX, NY = int((X1 - X0) / RES) + 1, int((Y1 - Y0) / RES) + 1
GX = X0 + np.arange(NX) * RES
GY = Y0 + np.arange(NY) * RES


class Piece:
    def __init__(self):
        self.H = np.zeros((NY, NX), dtype=np.float32)       # dessus à Z0
        self.T = np.zeros((NY, NX), dtype=np.uint8)         # 0 matière, 1 surép., 2 recouvrement, 3 bride
        self.passe = np.zeros((NY, NX), dtype=np.int16)     # n° de passe qui a coupé (recouvrement)
        self.Zs = np.full((NY, NX), -1e9, dtype=np.float32)  # surép. : matière au-dessus de ce Z
        self.marquer_recouvrement = False
        self.n_passe = 0

    def enlever(self, cx, cy, cz, r):
        """Passage d'un outil plat de rayon r, bout à cz."""
        i0 = max(0, int((cx - r - X0) / RES) - 1); i1 = min(NX, int((cx + r - X0) / RES) + 2)
        j0 = max(0, int((cy - r - Y0) / RES) - 1); j1 = min(NY, int((cy + r - Y0) / RES) + 2)
        xx, yy = np.meshgrid(GX[i0:i1], GY[j0:j1])
        m = (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r
        sub = self.H[j0:j1, i0:i1]
        coupe = m & (sub > cz + 1e-4)
        tg = self.T[j0:j1, i0:i1]
        if self.marquer_recouvrement:
            ps = self.passe[j0:j1, i0:i1]
            dejà = m & (ps > 0) & (ps < self.n_passe) & (np.abs(sub - cz) < 1e-3)
            tg[dejà] = 2
            ps[m & (ps == 0)] = self.n_passe
        tg[coupe & (tg == 1)] = 0
        sub[m] = np.minimum(sub[m], cz)

    def boite_haut(self):
        return float(max(0.01, self.H.max() + 0.01))

    def zs(self, x, y):
        i = np.clip(((x - X0) / RES).round().astype(np.int32), 0, NX - 1)
        j = np.clip(((y - Y0) / RES).round().astype(np.int32), 0, NY - 1)
        return self.Zs[j, i]

    def tag(self, x, y):
        i = np.clip(((x - X0) / RES).round().astype(np.int32), 0, NX - 1)
        j = np.clip(((y - Y0) / RES).round().astype(np.int32), 0, NY - 1)
        return self.T[j, i]

    def h(self, x, y):
        i = np.clip(((x - X0) / RES).round().astype(np.int32), 0, NX - 1)
        j = np.clip(((y - Y0) / RES).round().astype(np.int32), 0, NY - 1)
        return self.H[j, i]


# ── Caméra ───────────────────────────────────────────────────────
class Camera:
    def __init__(self, pos, cible, fov_deg, w, h, ortho=None):
        self.ortho = ortho            # demi-hauteur visible (mm) en projection parallèle
        self.pos = np.array(pos, float)
        f = np.array(cible, float) - self.pos; f /= np.linalg.norm(f)
        r = np.cross(f, [0, 0, 1.0]); r /= np.linalg.norm(r)
        u = np.cross(r, f)
        self.f, self.r, self.u, self.w, self.h = f, r, u, w, h
        self.k = np.tan(np.radians(fov_deg) / 2)

    def origines(self):
        n = self.w * self.h
        if not self.ortho:
            return np.broadcast_to(self.pos, (n, 3)).copy()
        a = self.w / self.h
        px = (np.arange(self.w) + 0.5) / self.w * 2 - 1
        py = 1 - (np.arange(self.h) + 0.5) / self.h * 2
        PX, PY = np.meshgrid(px * self.ortho * a, py * self.ortho)
        o = self.pos[None, None] + PX[..., None] * self.r + PY[..., None] * self.u
        return o.reshape(-1, 3)

    def rayons(self):
        if self.ortho:
            return np.broadcast_to(self.f, (self.w * self.h, 3)).copy()
        a = self.w / self.h
        px = (np.arange(self.w) + 0.5) / self.w * 2 - 1
        py = 1 - (np.arange(self.h) + 0.5) / self.h * 2
        PX, PY = np.meshgrid(px * self.k * a, py * self.k)
        d = self.f[None, None] + PX[..., None] * self.r + PY[..., None] * self.u
        d /= np.linalg.norm(d, axis=-1, keepdims=True)
        return d.reshape(-1, 3)

    def projeter(self, p):
        v = np.asarray(p, float) - self.pos
        z = v @ self.f
        if self.ortho:
            x = (v @ self.r) / (self.ortho * self.w / self.h)
            y = (v @ self.u) / self.ortho
        else:
            x = (v @ self.r) / (z * self.k * self.w / self.h)
            y = (v @ self.u) / (z * self.k)
        return np.stack([(x + 1) / 2 * self.w, (1 - y) / 2 * self.h, z], axis=-1)


# ── Intersections ────────────────────────────────────────────────
def boite(o, d, zt):
    """Entrée / sortie du rayon dans la boîte englobante de la pièce."""
    lo = np.array([X0, Y0 if YCUT is None else YCUT, ZB]); hi = np.array([X1, Y1, zt])
    with np.errstate(divide="ignore", invalid="ignore"):
        t1 = (lo - o) / d; t2 = (hi - o) / d
    tmin = np.nanmax(np.minimum(t1, t2), axis=-1)
    tmax = np.nanmin(np.maximum(t1, t2), axis=-1)
    return tmin, tmax


def marche_matiere(piece, o, d, pas=0.25):
    """Premier point où le rayon passe sous la surface. Retourne t (inf sinon)."""
    n = len(d)
    tmin, tmax = boite(o, d, piece.boite_haut())
    t_hit = np.full(n, np.inf)
    actifs = np.where(tmax > np.maximum(tmin, 0))[0]
    t = np.maximum(tmin[actifs], 0) + 1e-4
    tm = tmax[actifs]
    prev = t.copy()
    while len(actifs):
        p = o[actifs] + d[actifs] * t[:, None]
        dedans = p[:, 2] <= piece.h(p[:, 0], p[:, 1])
        if dedans.any():
            # affinage par dichotomie entre prev et t
            a, b = prev[dedans], t[dedans]
            idx = actifs[dedans]
            for _ in range(8):
                mid = (a + b) / 2
                q = o[idx] + d[idx] * mid[:, None]
                inside = q[:, 2] <= piece.h(q[:, 0], q[:, 1])
                b = np.where(inside, mid, b); a = np.where(inside, a, mid)
            t_hit[idx] = b
        garde = (~dedans) & (t < tm)
        actifs, prev, t, tm = actifs[garde], t[garde], t[garde] + pas, tm[garde]
    return t_hit


def cylindre(o, d, cx, cy, r, z0, z1):
    """Intersection avec la surface latérale d'un cylindre vertical."""
    ox, oy = o[:, 0] - cx, o[:, 1] - cy
    a = d[:, 0] ** 2 + d[:, 1] ** 2
    b = 2 * (ox * d[:, 0] + oy * d[:, 1])
    c = ox ** 2 + oy ** 2 - r * r
    disc = b * b - 4 * a * c
    t = np.full(len(d), np.inf)
    ok = (disc >= 0) & (a > 1e-9)
    s = np.sqrt(np.where(ok, disc, 0))
    t1 = (-b - s) / (2 * np.where(ok, a, 1))
    z = o[:, 2] + t1 * d[:, 2]
    good = ok & (t1 > 0) & (z >= z0) & (z <= z1)
    t[good] = t1[good]
    return t


def disque(o, d, cx, cy, r, z):
    with np.errstate(divide="ignore", invalid="ignore"):
        t = (z - o[:, 2]) / d[:, 2]
    p = o + d * t[:, None]
    ok = (t > 0) & ((p[:, 0] - cx) ** 2 + (p[:, 1] - cy) ** 2 <= r * r)
    return np.where(ok, t, np.inf)


# ── Rendu d'une image ────────────────────────────────────────────
LUM = np.array([-0.35, -0.45, 1.25]); LUM /= np.linalg.norm(LUM)
FOND = np.array([0xEB, 0xEB, 0xEB], float) / 255


def normales_matiere(piece, p, d):
    e = RES * 1.5
    hx = piece.h(p[:, 0] + e, p[:, 1]) - piece.h(p[:, 0] - e, p[:, 1])
    hy = piece.h(p[:, 0], p[:, 1] + e) - piece.h(p[:, 0], p[:, 1] - e)
    n = np.stack([-hx / (2 * e), -hy / (2 * e), np.ones(len(p))], axis=-1)
    # parois : direction horizontale lissée (gradient sur un pas plus large)
    raide = (np.abs(hx) + np.abs(hy)) > 0.5
    if raide.any():
        q = p[raide]; E = 0.6
        acc = np.zeros((len(q), 2))
        for ox in (-E, 0, E):
            for oy in (-E, 0, E):
                hh = piece.h(q[:, 0] + ox, q[:, 1] + oy)
                acc[:, 0] -= ox * hh; acc[:, 1] -= oy * hh
        ln = np.linalg.norm(acc, axis=-1, keepdims=True)
        hz = np.where(ln > 1e-9, acc / np.maximum(ln, 1e-9), 0)
        n[raide] = np.stack([hz[:, 0], hz[:, 1], np.zeros(len(q))], -1)
    # faces latérales du bloc
    y0 = Y0 if YCUT is None else YCUT
    for ax, val, sgn in ((0, X0, -1), (0, X1, 1), (1, y0, -1), (1, Y1, 1)):
        m = np.abs(p[:, ax] - val) < 0.05
        m &= p[:, 2] < piece.h(p[:, 0], p[:, 1]) - 0.05
        nn = np.zeros(3); nn[ax] = sgn
        n[m] = nn
    # parois raides : normale horizontale nette
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n


def ombre(piece, p, outil):
    """True si le point est à l'ombre (matière ou outil entre lui et la lumière)."""
    n = len(p)
    om = np.zeros(n, bool)
    o = p + LUM * 0.05
    d = np.broadcast_to(LUM, (n, 3)).copy()
    # outil
    if outil is not None:
        cx, cy, cz, r = outil
        om |= np.isfinite(cylindre(o, d, cx, cy, r, cz, cz + 70))
    # matière : petite marche vers la lumière
    t = np.full(n, 0.3)
    zmax = piece.boite_haut()
    act = np.where(~om)[0]
    for _ in range(90):
        if not len(act):
            break
        q = o[act] + d[act] * t[act, None]
        y0 = Y0 if YCUT is None else YCUT
        dans = (q[:, 0] >= X0) & (q[:, 0] <= X1) & (q[:, 1] >= y0) & (q[:, 1] <= Y1) & (q[:, 2] <= zmax)
        hit = dans & (q[:, 2] <= piece.h(q[:, 0], q[:, 1]))
        om[act[hit]] = True
        sort = (q[:, 2] > zmax)
        act = act[~hit & ~sort]
        t[act] += 0.4
    return om


def rendre(piece, cam, outil=None, angle=0.0, trajet=None, lc=16.0):
    """outil = (cx, cy, z_bout, rayon). Retourne image RGB (h, w, 3)."""
    d = cam.rayons()
    n = len(d)
    o = cam.origines()
    col = np.tile(FOND, (n, 1))
    zbuf = np.full(n, np.inf)

    t_mat = marche_matiere(piece, o, d)

    # outil : partie coupante + queue + porte-outil (cône simplifié)
    t_cut = t_queue = t_haut = np.full(n, np.inf)
    if outil is not None:
        cx, cy, cz, r = outil
        t_cut = cylindre(o, d, cx, cy, r, cz, cz + lc)
        t_queue = cylindre(o, d, cx, cy, r * 0.82, cz + lc, cz + lc + 30)
        t_bout = disque(o, d, cx, cy, r, cz)
        t_cut = np.minimum(t_cut, t_bout)

    t_min = np.minimum.reduce([t_mat, t_cut, t_queue])
    zbuf = t_min * (d @ cam.f)

    # ── matière
    m = np.isfinite(t_mat) & (t_mat <= t_min)
    if m.any():
        p = o[m] + d[m] * t_mat[m, None]
        nr = normales_matiere(piece, p, d[m])
        usine = p[:, 2] < -0.05
        base = np.where(usine[:, None], [0.80, 0.83, 0.87], [0.66, 0.70, 0.76])
        # faces latérales du bloc plus sombres (coupe)
        cote = np.abs(nr[:, 2]) < 0.3
        # couleur selon l'étiquette (prise un peu à l'intérieur de la matière)
        qx, qy = p[:, 0] - nr[:, 0] * 0.15, p[:, 1] - nr[:, 1] * 0.15
        tgx = piece.tag(qx, qy).copy()
        tgx[(tgx == 1) & (p[:, 2] < piece.zs(qx, qy) - 0.02)] = 0
        base = np.where((tgx == 1)[:, None], [0.95, 0.58, 0.20], base)
        base = np.where((tgx == 2)[:, None], [0.45, 0.65, 0.95], base)
        base = np.where((tgx == 3)[:, None], [0.30, 0.32, 0.35], base)
        lam = np.clip(nr @ LUM, 0, 1)
        sh = ombre(piece, p, outil)
        lam = np.where(sh, lam * 0.25, lam)
        h_ = LUM - d[m]; h_ /= np.linalg.norm(h_, axis=-1, keepdims=True)
        spec = np.clip((nr * h_).sum(-1), 0, 1) ** 40 * np.where(sh, 0, 0.35)
        # assombrir le fond de poche (occlusion ambiante simple)
        prof = np.clip(-p[:, 2] / 20.0, 0, 1)
        amb = 0.42 - 0.12 * prof * usine
        c = base * (amb + 0.62 * lam)[:, None] + spec[:, None]
        c = np.where(cote[:, None] & ~usine[:, None], c * 0.92, c)
        if YCUT is not None:
            sec = (np.abs(p[:, 1] - YCUT) < 0.05) & (p[:, 2] < piece.h(p[:, 0], p[:, 1]) - 0.05)
            if sec.any():
                hach = ((p[sec, 0] + p[sec, 2]) % 2.0) < 0.35
                cs = np.where(hach[:, None], [0.45, 0.48, 0.53], [0.78, 0.80, 0.84])
                st = (piece.tag(p[sec, 0], p[sec, 1]) == 1) & (p[sec, 2] >= piece.zs(p[sec, 0], p[sec, 1]) - 0.02)
                cs = np.where((st & ~hach)[:, None], [0.97, 0.70, 0.40], cs)
                cs = np.where((st & hach)[:, None], [0.80, 0.45, 0.15], cs)
                c[sec] = cs
        col[m] = c

    # ── outil
    if outil is not None:
        cx, cy, cz, r = outil
        for tt, kind in ((t_cut, "cut"), (t_queue, "queue")):
            mk = np.isfinite(tt) & (tt <= t_min)
            if not mk.any():
                continue
            p = o[mk] + d[mk] * tt[mk, None]
            v = np.stack([p[:, 0] - cx, p[:, 1] - cy, np.zeros(len(p))], -1)
            ln = np.linalg.norm(v, axis=-1, keepdims=True)
            bout = (ln[:, 0] < r - 0.02) & (np.abs(p[:, 2] - cz) < 0.02)
            nr = np.where(ln > 1e-6, v / np.maximum(ln, 1e-6), [0, 0, 1])
            nr[bout] = [0, 0, -1]
            lam = np.clip(nr @ LUM, 0, 1)
            h_ = LUM - d[mk]; h_ /= np.linalg.norm(h_, axis=-1, keepdims=True)
            spec = np.clip((nr * h_).sum(-1), 0, 1) ** 25
            if kind == "cut":
                th = np.arctan2(p[:, 1] - cy, p[:, 0] - cx)
                hel = th + angle + (p[:, 2] - cz) * 0.18      # goujures hélicoïdales
                g = np.cos(4 * hel)
                base = np.where(g[:, None] > 0.55, [0.30, 0.31, 0.34], [0.55, 0.57, 0.60])
                c = base * (0.35 + 0.65 * lam)[:, None] + 0.55 * spec[:, None]
            else:
                base = np.array([0.74, 0.75, 0.77])
                c = base * (0.40 + 0.60 * lam)[:, None] + 0.7 * spec[:, None]
            col[mk] = c

    img = np.clip(col, 0, 1).reshape(cam.h, cam.w, 3)
    t_outil = np.minimum(t_cut, t_queue)
    z_outil = np.where(np.isfinite(t_outil) & (t_outil <= t_min), t_outil * (d @ cam.f), np.inf)
    rendre.z_outil = z_outil.reshape(cam.h, cam.w)
    return (img * 255).astype(np.uint8), zbuf.reshape(cam.h, cam.w)


def tracer_trajet(img, zbuf, cam, pts, couleur=(0, 154, 0), largeur=2, alpha_cache=0.25):
    """Dessine une polyligne 3D avec test de profondeur (parties cachées pâles)."""
    pr = cam.projeter(np.asarray(pts))
    im = Image.fromarray(img).convert("RGBA")
    calque = Image.new("RGBA", im.size, (0, 0, 0, 0))
    dr = ImageDraw.Draw(calque)
    h, w = zbuf.shape
    for a, b in zip(pr[:-1], pr[1:]):
        mx, my = int((a[0] + b[0]) / 2), int((a[1] + b[1]) / 2)
        vis = True
        if 0 <= mx < w and 0 <= my < h:
            # distance le long du rayon ~ profondeur caméra / cos ; marge 0.6 mm
            vis = (a[2] + b[2]) / 2 <= zbuf[my, mx] * 1.0 + 0.6
        al = 255 if vis else int(255 * alpha_cache)
        dr.line([(a[0], a[1]), (b[0], b[1])], fill=couleur + (al,), width=largeur)
    return np.array(Image.alpha_composite(im, calque).convert("RGB"))


def tracer_chemin(img, zcache, cam, pts, couleur, largeur, pointille=False):
    """Polyligne 3D ; segments cachés par zcache (profondeur) non dessinés."""
    pr = cam.projeter(np.asarray(pts))
    im = Image.fromarray(img)
    dr = ImageDraw.Draw(im)
    h, w = zcache.shape
    for i, (a, b) in enumerate(zip(pr[:-1], pr[1:])):
        if pointille and (i // 3) % 2:
            continue
        mx, my = int((a[0] + b[0]) / 2), int((a[1] + b[1]) / 2)
        if 0 <= mx < w and 0 <= my < h and (a[2] + b[2]) / 2 > zcache[my, mx] + 0.3:
            continue
        dr.line([(a[0], a[1]), (b[0], b[1])], fill=couleur, width=largeur)
    return np.array(im)


def titre(im, txt, taille=17):
    dr = ImageDraw.Draw(im)
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", taille)
    dr.text((10, 8), txt, fill=(48, 48, 48), font=f)
    return im
