# Paints Kolot's background: a wheat field at sunset. Run: python3 tools/make_background.py
import numpy as np
from PIL import Image, ImageFilter, ImageDraw
rng = np.random.default_rng(7)
W, H = 1960, 1360
HZ = int(H * 0.50)                       # horizon line
SUNX, SUNY, SUNR = int(W * 0.64), HZ + 10, 70

def lerp_stops(t, stops):
    t = np.clip(t, 0, 1); out = np.zeros(t.shape + (3,))
    for (t0, c0), (t1, c1) in zip(stops[:-1], stops[1:]):
        m = (t >= t0) & (t <= t1); u = ((t - t0) / max(t1 - t0, 1e-6))[m][:, None]
        out[m] = np.array(c0) * (1 - u) + np.array(c1) * u
    return out

y, x = np.mgrid[0:H, 0:W].astype(float)
# --- sky
sky = lerp_stops(y / HZ, [(0, (22, 18, 58)), (0.35, (70, 36, 92)), (0.62, (178, 72, 88)),
                          (0.82, (238, 128, 64)), (0.95, (255, 196, 110)), (1.0, (255, 224, 150))])
d = np.hypot((x - SUNX) / 1.0, (y - SUNY) * 1.6)
glow = np.exp(-d / 260)[..., None] * np.array([255, 170, 80]) * 0.55 + np.exp(-d / 70)[..., None] * np.array([255, 230, 170]) * 0.8
img = np.clip(sky + glow, 0, 255)

# --- clouds: stretched smooth noise, lit from below
def smooth_noise(scale_x, scale_y, seed):
    r = np.random.default_rng(seed)
    small = r.random((int(H / scale_y) + 3, int(W / scale_x) + 3))
    im = Image.fromarray((small * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
    return np.asarray(im).astype(float) / 255
n = 0.6 * smooth_noise(260, 22, 1) + 0.4 * smooth_noise(90, 10, 2)
band = np.exp(-((y - HZ * 0.55) / (HZ * 0.22)) ** 2) + 0.6 * np.exp(-((y - HZ * 0.25) / (HZ * 0.12)) ** 2)
cl = np.clip((n - 0.52) * 3.2, 0, 1) * band * (y < HZ - 20)
lit = lerp_stops(y / HZ, [(0, (120, 70, 130)), (0.5, (255, 140, 120)), (0.85, (255, 190, 120)), (1, (255, 210, 140))])
side = np.clip(1 - np.abs(x - SUNX) / (W * 0.9), 0.35, 1)[..., None]
img = img * (1 - cl[..., None] * 0.85) + lit * side * cl[..., None] * 0.85

# --- sun disc (half set)
sun = np.clip(1 - (np.hypot(x - SUNX, y - SUNY) - SUNR) / 3, 0, 1) * (y < HZ + 4)
img = img * (1 - sun[..., None]) + np.array([255, 240, 200]) * sun[..., None]

# --- distant hills (two hazy layers)
def ridge(base, amp, freqs, seed):
    r = np.random.default_rng(seed); xs = np.arange(W).astype(float); h = np.zeros(W)
    for f in freqs: h += np.sin(xs / W * np.pi * 2 * f + r.random() * 6.28) * amp / f ** 0.7
    return base - h
for base, amp, col, seed in [(HZ + 6, 26, (150, 80, 95), 3), (HZ + 22, 34, (92, 48, 66), 4)]:
    h = ridge(base, amp, [1, 2.3, 5.1, 11.7], seed)
    m = (y >= h[None, :])
    haze = np.clip((y - h[None, :]) / 60, 0, 1)[..., None]
    c = np.array(col) * (1 - 0.0 * haze)
    img = np.where(m[..., None], c * 0.9 + img * 0.1, img)

# --- field: perspective gradient
fy = np.clip((y - HZ) / (H - HZ), 0, 1)
field = lerp_stops(fy, [(0, (150, 88, 44)), (0.15, (120, 66, 32)), (0.45, (66, 36, 20)), (1, (16, 9, 8))])
sheen = np.exp(-np.hypot((x - SUNX) / 520, (y - HZ) / 140))[..., None] * np.array([255, 170, 80]) * 0.45
fm = (y > HZ + 40)[..., None]
img = np.where(fm, np.clip(field + sheen, 0, 255), img)
# soften the join between hills and field
img = np.where(((y > HZ + 20) & (y <= HZ + 40))[..., None],
               img * (1 - ((y - HZ - 20) / 20)[..., None]) + np.clip(field + sheen, 0, 255) * ((y - HZ - 20) / 20)[..., None], img)

im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
dr = ImageDraw.Draw(im, "RGBA")

# --- lone tree on the left ridge
tx, ty = int(W * 0.17), HZ + 10
dr.line([(tx, ty), (tx, ty - 70)], fill=(40, 20, 28, 255), width=7)
for i in range(260):
    a = rng.random() * np.pi; rr = rng.random() ** 0.5 * 62
    cx, cy = tx + np.cos(a) * rr * 1.25, ty - 96 - np.sin(a) * rr * 0.75 + 20
    s = rng.integers(6, 14)
    dr.ellipse([cx - s, cy - s, cx + s, cy + s], fill=(40, 20, 30, 255))

# --- wheat / grass stalks, bigger towards the viewer, rim-lit by the sun
for i in range(9000):
    t = rng.random() ** 1.7                      # more stalks far away
    by = HZ + 40 + t * (H - HZ - 30)
    bx = rng.random() * W
    hgt = 6 + t * 150 * (0.6 + rng.random() * 0.6)
    lean = (rng.random() - 0.5) * hgt * 0.35 + 12 * t
    shade = 20 + 40 * (1 - t)
    near_sun = np.exp(-abs(bx - SUNX) / 600)
    col = (int(shade + 120 * near_sun * (1 - t * 0.6)), int(shade * 0.6 + 60 * near_sun * (1 - t * 0.6)), int(shade * 0.4), 210)
    wdt = max(1, int(1 + t * 3))
    tipx, tipy = bx + lean, by - hgt
    dr.line([(bx, by), (tipx, tipy)], fill=col, width=wdt)
    if t > 0.35 and rng.random() < 0.28:          # wheat ear
        ew = 1.5 + t * 2.8
        dr.ellipse([tipx - ew, tipy - ew * 3.2, tipx + ew, tipy + ew * 0.6],
                   fill=(int(col[0] * 1.1 + 12), int(col[1] * 1.05 + 6), int(col[2] + 2), 200))

im = im.filter(ImageFilter.GaussianBlur(0.6))
# gentle vignette
a = np.asarray(im).astype(float)
v = 1 - 0.35 * (np.hypot((x - W / 2) / (W * 0.75), (y - H * 0.45) / (H * 0.8)) ** 2)
a = np.clip(a * np.clip(v, 0.45, 1)[..., None], 0, 255)
Image.fromarray(a.astype(np.uint8)).save("Resources/background.jpg", quality=88, optimize=True)
print("ok")
