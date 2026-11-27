# Reference run of the user's MaterialLab learner (engine2 / fit2), unchanged, for the C++ port.
# usage: python3 -I ref1.py PKG_DIR CLIP.wav TARGET.json MATERIAL.json OUT.npz
import sys, json, time
pkg, clip, tgt, mat, out = sys.argv[1:6]
sys.path.insert(0, pkg)
import numpy as np, soundfile as sf
import jax, jax.numpy as jnp
from materiallab import engine2 as E, fit2 as F

FS = 48000.0
def trim_to_strike(x, fs, keep_ms=1.5):          # as tools/fit_v2.py
    w = int(0.001 * fs); n = len(x) // w
    env = 20 * np.log10(np.sqrt(np.mean(x[: n * w].reshape(n, w) ** 2, axis=1)) + 1e-9)
    first = int(np.argmax(env > env.max() - 30))
    return x[max(0, first * w - int(keep_ms / 1000 * fs)):]

x, sr = sf.read(clip, dtype="float32"); assert sr == FS
if x.ndim > 1: x = x[:, 0]
x = trim_to_strike(x, int(FS))
floor = json.load(open(tgt)).get("noise_floor_db")
rec = json.load(open(mat))
p = {k: np.asarray(v, dtype=np.float64) for k, v in rec["params"].items()}
n = len(x)
print("clip", clip.split('/')[-1], "samples", n, "recorded loss", rec["loss"], "recorded n_samples", rec.get("n_samples"))
loss_fn = F.make_loss(x, FS, floor)
y = E.render_np(E.complete(p), n, FS, 4)
print("render peak", float(np.abs(y).max()))
print("loss of the user's fitted material, recomputed here:", float(loss_fn(jnp.asarray(y, jnp.float32))))
np.savez(out, x=x, y=y)
