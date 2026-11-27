# Golden data from the user's learner, unchanged, for checking the C++ port stage by stage.
# usage: python3 -I golden.py PKG_DIR CLIP.wav TARGET.json MATERIAL.json OUTDIR
import sys, json, time, os
pkg, clip, tgt, mat, outd = sys.argv[1:6]
sys.path.insert(0, pkg)
import numpy as np, soundfile as sf
import jax, jax.numpy as jnp
from materiallab import engine2 as E, fit2 as F
os.makedirs(outd, exist_ok=True)
FS = 48000.0
def trim_to_strike(x, fs, keep_ms=1.5):
    w = int(0.001 * fs); n = len(x) // w
    env = 20 * np.log10(np.sqrt(np.mean(x[: n * w].reshape(n, w) ** 2, axis=1)) + 1e-9)
    first = int(np.argmax(env > env.max() - 30))
    return x[max(0, first * w - int(keep_ms / 1000 * fs)):]
x, sr = sf.read(clip, dtype="float32")
if x.ndim > 1: x = x[:, 0]
x = trim_to_strike(x, int(FS)); n = len(x)
floor = json.load(open(tgt)).get("noise_floor_db")
loss_fn = F.make_loss(x, FS, floor)

def grads_at(p, tag):
    p = E.complete({k: np.asarray(v, np.float64) for k, v in p.items()})
    k = len(p["f"])
    phases = E.strike_phases(k); bands = jnp.asarray(E.noise_bands(n, FS)); bodyb = jnp.asarray(E.noise_bands(n, FS, seed=101))[None]
    ones = jnp.ones((1, k), jnp.float32); centres = jnp.asarray(E.band_centres(FS), dtype=jnp.float32)
    u = E.unconstrained(p)
    def objective(u):
        q = E.physical(u)
        y = E.render_physical(q, n, FS, bands, bodyb, phases, ones, centres)[0]
        return loss_fn(y), y
    (val, y), g = jax.value_and_grad(objective, has_aux=True)(u)
    np.savez(f"{outd}/{tag}.npz", y=np.asarray(y), loss=float(val),
             **{"u_" + key: np.asarray(v) for key, v in u.items()}, **{"g_" + key: np.asarray(v) for key, v in g.items()},
             **{"p_" + key: np.asarray(v) for key, v in p.items()},
             phases=np.asarray(phases), bands=np.asarray(bands), bodyb=np.asarray(bodyb))
    print(tag, "loss", float(val), "params", sum(np.size(v) for v in u.values()), flush=True)

np.save(f"{outd}/x.npy", x)
json.dump({"n": n, "fs": FS, "noise_floor_db": floor}, open(f"{outd}/meta.json", "w"))
t = time.time(); p0, info = F.initial_params(x, FS)
print("initial_params", round(time.time() - t, 1), "s, modes", len(p0["f"]), flush=True)
np.savez(f"{outd}/init.npz", **{k: np.asarray(v) for k, v in p0.items()})
grads_at(p0, "start")
rec = json.load(open(mat))
grads_at(rec["params"], "fitted")
t = time.time()
res = F.fit(x, FS, steps=300, noise_floor_db=floor, progress=lambda i, s, l: print(f"fit {i}/{s} {l:.4f}", flush=True))
print("full fit: start", res["start_loss"], "best", res["loss"], "seconds", round(time.time() - t, 1), flush=True)
np.savez(f"{outd}/fit.npz", history=np.asarray(res["history"]), **{k: np.asarray(v) for k, v in res["params"].items()})
