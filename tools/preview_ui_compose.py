"""Offline mirror of the GUI's draw rules, so a composite can be looked at
without launching the game - the same primitives as gui_render.hpp/gui.hpp:
quads in 1280x720 layout space with per-corner colour, orientation flips,
free rotation and the GX2 blend modes.

This is how the cursor/plate/selection-frame bugs were found and fixed: build
the composite here, look at the PNG, then port the numbers to the header.
Needs the decoded textures next to it - extract Layout/Common.sblarc's timg/
folder and point TIMG at it - plus preview_ui_assets.py for the de-tiler.

    import preview_ui_compose as pc
    c = pc.Canvas(700, 300)
    c.rounded_box(60, 60, 276, 40, (0, 0, 0, 200), 8)
    c.frame_from_corner("Nt_CursorS_00^s", 32, 32, 332, 96, 48,
                        (255, 252, 198, 128), "additive")
    c.save("row.png")
"""
import math, os
from PIL import Image
import preview_ui_assets as gx2tex

TIMG = os.environ.get("BOTW_TIMG", "common/timg")
_cache = {}


def sprite(name):
    if name not in _cache:
        im, info = gx2tex.load_bflim(os.path.join(TIMG, name + ".bflim"))
        _cache[name] = im
    return _cache[name]


class Canvas:
    def __init__(self, w=1280, h=720, bg=(28, 34, 44, 255)):
        self.w, self.h = w, h
        self.buf = Image.new("RGBA", (w, h), bg)
        self.px = self.buf.load()

    # --- blending -------------------------------------------------------
    def _blend(self, x, y, sr, sg, sb, sa, mode):
        if sa <= 0 and mode not in ("multiply",):
            return
        dr, dg, db, da = self.px[x, y]
        s = (sr, sg, sb)
        d = (dr, dg, db)
        a = sa / 255.0
        if mode == "alpha":
            out = [s[i] * a + d[i] * (1 - a) for i in range(3)]
        elif mode == "additive":
            out = [min(255, s[i] * a + d[i]) for i in range(3)]
        elif mode == "overlay":   # src*dst + dst*srcAlpha
            out = [min(255, s[i] * d[i] / 255.0 + d[i] * a) for i in range(3)]
        elif mode == "multiply":
            out = [s[i] * d[i] / 255.0 for i in range(3)]
        else:
            out = [s[i] * a + d[i] * (1 - a) for i in range(3)]
        self.px[x, y] = (int(out[0]), int(out[1]), int(out[2]), 255)

    # --- the one primitive ----------------------------------------------
    def quad(self, img, x0, y0, x1, y1, u0=0.0, v0=0.0, u1=1.0, v1=1.0,
             color=(255, 255, 255, 255), orient=0, blend="alpha", rot=0.0):
        """orient: 1=flipH 2=flipV 4/8/12 = rotate 90/180/270 of the UVs.
        rot: free rotation of the QUAD (degrees, clockwise on screen)."""
        u = [u0, u1, u0, u1]
        v = [v0, v0, v1, v1]
        for _ in range((orient & 12) // 4):
            u = [u[2], u[0], u[3], u[1]]
            v = [v[2], v[0], v[3], v[1]]
        if orient & 1:
            u[0], u[1] = u[1], u[0]; v[0], v[1] = v[1], v[0]
            u[2], u[3] = u[3], u[2]; v[2], v[3] = v[3], v[2]
        if orient & 2:
            u[0], u[2] = u[2], u[0]; v[0], v[2] = v[2], v[0]
            u[1], u[3] = u[3], u[1]; v[1], v[3] = v[3], v[1]

        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        ca, sa_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        iw, ih = img.size
        ip = img.load()
        # bounding box of the rotated quad
        pts = [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]
        if rot:
            pts = [(cx + (px - cx) * ca - (py - cy) * sa_, cy + (px - cx) * sa_ + (py - cy) * ca) for px, py in pts]
        bx0 = max(0, int(min(p[0] for p in pts)) - 1); bx1 = min(self.w, int(max(p[0] for p in pts)) + 2)
        by0 = max(0, int(min(p[1] for p in pts)) - 1); by1 = min(self.h, int(max(p[1] for p in pts)) + 2)
        for y in range(by0, by1):
            for x in range(bx0, bx1):
                # inverse-rotate the sample point into quad space
                sx, sy = x + 0.5, y + 0.5
                if rot:
                    dx, dy = sx - cx, sy - cy
                    sx = cx + dx * ca + dy * sa_
                    sy = cy - dx * sa_ + dy * ca
                if not (x0 <= sx < x1 and y0 <= sy < y1):
                    continue
                fx = (sx - x0) / max(x1 - x0, 1e-6)
                fy = (sy - y0) / max(y1 - y0, 1e-6)
                # bilinear in UV space across the quad's four corners
                uu = (u[0] * (1 - fx) + u[1] * fx) * (1 - fy) + (u[2] * (1 - fx) + u[3] * fx) * fy
                vv = (v[0] * (1 - fx) + v[1] * fx) * (1 - fy) + (v[2] * (1 - fx) + v[3] * fx) * fy
                tx = min(iw - 1, max(0, int(uu * iw)))
                ty = min(ih - 1, max(0, int(vv * ih)))
                tr, tg, tb, ta = ip[tx, ty]
                self._blend(x, y, tr * color[0] // 255, tg * color[1] // 255, tb * color[2] // 255,
                            ta * color[3] // 255, blend)

    def rect(self, x, y, w, h, color, blend="alpha"):
        white = Image.new("RGBA", (2, 2), (255, 255, 255, 255))
        self.quad(white, x, y, x + w, y + h, color=color, blend=blend)

    # --- composites, mirroring gui.hpp ----------------------------------
    def corners(self, name, x, y, w, h, size, color, blend="alpha"):
        img = sprite(name)
        self.quad(img, x, y, x + size, y + size, color=color, orient=0, blend=blend)
        self.quad(img, x + w - size, y, x + w, y + size, color=color, orient=1, blend=blend)
        self.quad(img, x, y + h - size, x + size, y + h, color=color, orient=2, blend=blend)
        self.quad(img, x + w - size, y + h - size, x + w, y + h, color=color, orient=3, blend=blend)

    def frame_from_corner(self, name, x, y, w, h, corner, color, blend="alpha"):
        img = sprite(name)
        iw, ih = img.size
        eu = 1.0 - 0.5 / iw
        ev = 1.0 - 0.5 / ih
        self.corners(name, x, y, w, h, corner, color, blend)
        self.quad(img, x + corner, y, x + w - corner, y + corner, eu, 0.0, 1.0, 1.0, color, 0, blend)
        self.quad(img, x + corner, y + h - corner, x + w - corner, y + h, eu, 0.0, 1.0, 1.0, color, 2, blend)
        self.quad(img, x, y + corner, x + corner, y + h - corner, 0.0, ev, 1.0, 1.0, color, 0, blend)
        self.quad(img, x + w - corner, y + corner, x + w, y + h - corner, 0.0, ev, 1.0, 1.0, color, 1, blend)

    def rounded_box(self, x, y, w, h, color, radius=8):
        self.corners("CornerR3_00^s", x, y, w, h, radius, color)
        self.rect(x + radius, y, w - 2 * radius, radius, color)
        self.rect(x, y + radius, w, h - 2 * radius, color)
        self.rect(x + radius, y + h - radius, w - 2 * radius, radius, color)

    def rounded_outline(self, x, y, w, h, color, radius=8, thickness=2):
        self.corners("CornerLineR2_00^s", x, y, w, h, radius, color)
        self.rect(x + radius, y, w - 2 * radius, thickness, color)
        self.rect(x + radius, y + h - thickness, w - 2 * radius, thickness, color)
        self.rect(x, y + radius, thickness, h - 2 * radius, color)
        self.rect(x + w - thickness, y + radius, thickness, h - 2 * radius, color)

    def save(self, path):
        self.buf.convert("RGB").save(path)
