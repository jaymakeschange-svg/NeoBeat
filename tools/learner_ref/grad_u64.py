# The learner's objective (fit2's loss of engine2's render of physical(u)) and its gradient with
# respect to the unconstrained parameters u, in double precision: the exact reference for the
# port's hand-written gradient (stage 5b). The engine runs with float64 in place of float32 (inside
# this script only); the loss is make_loss's terms as in grad_terms.py, with the window rounded to
# float32 as the learner does.
# usage: python3 -I grad_u64.py PKG_DIR GOLDEN_DIR start|fitted
import sys, json
pkg, g, tag = sys.argv[1:4]
sys.path.insert(0, pkg)
import numpy as np, jax
jax.config.update("jax_enable_x64", True)
import jax.numpy as jnp
from materiallab import engine2 as E, fit2 as F

class _J:                      # engine2's jnp, with float32 meaning float64
    float32 = jnp.float64
    def __getattr__(self, k): return getattr(jnp, k)
E.jnp = _J()

fs = 48000.0
x = np.load(f"{g}/x.npy"); meta = json.load(open(f"{g}/meta.json")); floor_db = meta["noise_floor_db"]
tgt = jnp.asarray(x, jnp.float64)
W, octaves = F._log_bands(2048, fs); Wj = jnp.asarray(W, jnp.float64)
env_hop, env_win = int(0.005 * fs), int(0.01 * fs)
def rms_db(v):
    fr = F._frames(v, env_win, env_hop); return 10 * jnp.log10(jnp.mean(fr**2, axis=1) + 1e-12)
t_env = rms_db(tgt); flo = t_env.max() + (floor_db if floor_db is not None else -70.0); flo = jnp.maximum(flo, t_env.max() - 70.0)
t_mags = [F._mag(tgt, n) for n in F.FFT_SIZES]
a_n, a_win, a_hop = int(0.03 * fs), int(0.001 * fs), int(0.0005 * fs)
def attack_db(v):
    fr = F._frames(v[:a_n], a_win, a_hop); return 10 * jnp.log10(jnp.mean(fr**2, axis=1) + 1e-12)
t_att = attack_db(tgt); att_floor = t_att.max() - 50.0
tb = (F._mag(tgt, 2048) ** 2) @ Wj
def loss(y):
    total = 0.0
    for n, b in zip(F.FFT_SIZES, t_mags):
        a = F._mag(y, n); fl = jnp.max(b) * 10 ** (-70 / 20) + 1e-9
        total = total + jnp.mean(jnp.abs(jnp.log(a + fl) - jnp.log(b + fl))) + jnp.linalg.norm(a - b) / (jnp.linalg.norm(b) + 1e-9)
    total = total / len(F.FFT_SIZES)
    env = jnp.mean(jnp.abs(jnp.maximum(rms_db(y), flo) - jnp.maximum(t_env, flo))) / 10.0
    pa = (F._mag(y, 2048) ** 2) @ Wj
    wa = pa / (pa.sum(1, keepdims=True) + 1e-12); wb = tb / (tb.sum(1, keepdims=True) + 1e-12)
    w1 = jnp.sum(jnp.abs(jnp.cumsum(wa, 1) - jnp.cumsum(wb, 1)), 1) * octaves / pa.shape[1]
    wt = tb.sum(1); wass = jnp.sum(w1 * wt) / (wt.sum() + 1e-12)
    att = jnp.mean(jnp.abs(jnp.maximum(attack_db(y), att_floor) - jnp.maximum(t_att, att_floor))) / 10.0
    return total + env + 3.0 * wass + att

z = np.load(f"{g}/{tag}.npz")
u = {k[2:]: jnp.asarray(z[k], jnp.float64) for k in z.files if k.startswith("u_")}
k = len(u["logf"]); n = len(z["y"])
bands = jnp.asarray(z["bands"], jnp.float64); bodyb = jnp.asarray(z["bodyb"], jnp.float64)
phases = jnp.asarray(z["phases"], jnp.float64); centres = jnp.asarray(E.band_centres(fs), jnp.float64)
def objective(u):
    return loss(E.render_physical(E.physical(u), n, fs, bands, bodyb, phases, jnp.ones((1, k)), centres)[0])
val, gu = jax.value_and_grad(objective)(u)
with open(f"{g}/{tag}/gu64.txt", "w") as fh:
    for key in E.MODE_KEYS + E.GLOBAL_KEYS:
        v = np.atleast_1d(np.asarray(gu[key], np.float64))
        fh.write(key + " " + str(len(v)) + " " + " ".join(repr(float(a)) for a in v) + "\n")
open(f"{g}/{tag}/loss64.txt", "w").write(repr(float(val)))
print(tag, "objective (double)", float(val), "learner (float32)", float(z["loss"]))
