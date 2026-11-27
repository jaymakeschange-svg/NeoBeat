# Gradient of each term of fit2.make_loss separately (the same code as make_loss, split), for the
# C++ stage-5a check. With X64=1: in double precision, target included - the exact reference.
# usage: [X64=1] python3 -I grad_terms.py PKG_DIR GOLDEN_DIR start|fitted
import sys, json
pkg, g, tag = sys.argv[1:4]
sys.path.insert(0, pkg)
import numpy as np, jax, os
if os.environ.get("X64"): jax.config.update("jax_enable_x64", True)
import jax.numpy as jnp
from materiallab import fit2 as F
x = np.load(f"{g}/x.npy"); meta = json.load(open(f"{g}/meta.json"))
fs = 48000.0; floor_db = meta["noise_floor_db"]
tgt = jnp.asarray(x, jnp.float64) if os.environ.get("X64") else jnp.asarray(x)
W, octaves = F._log_bands(2048, fs); Wj = jnp.asarray(W)
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
def spec(y):
    total = 0.0
    for n, b in zip(F.FFT_SIZES, t_mags):
        a = F._mag(y, n); fl = jnp.max(b) * 10 ** (-70 / 20) + 1e-9
        total = total + jnp.mean(jnp.abs(jnp.log(a + fl) - jnp.log(b + fl))) + jnp.linalg.norm(a - b) / (jnp.linalg.norm(b) + 1e-9)
    return total / len(F.FFT_SIZES)
def env(y): return jnp.mean(jnp.abs(jnp.maximum(rms_db(y), flo) - jnp.maximum(t_env, flo))) / 10.0
def wass(y):
    pa = (F._mag(y, 2048) ** 2) @ Wj
    wa = pa / (pa.sum(1, keepdims=True) + 1e-12); wb = tb / (tb.sum(1, keepdims=True) + 1e-12)
    w1 = jnp.sum(jnp.abs(jnp.cumsum(wa, 1) - jnp.cumsum(wb, 1)), 1) * octaves / pa.shape[1]
    wt = tb.sum(1); return 3.0 * jnp.sum(w1 * wt) / (wt.sum() + 1e-12)
def att(y): return jnp.mean(jnp.abs(jnp.maximum(attack_db(y), att_floor) - jnp.maximum(t_att, att_floor))) / 10.0
y = jnp.asarray(np.load(f"{g}/{tag}.npz")["y"], jnp.float64 if os.environ.get("X64") else jnp.float32)
suf = "_x64" if os.environ.get("X64") else ""
for name, fn in (("spec", spec), ("env", env), ("wass", wass), ("att", att)):
    v, gr = jax.value_and_grad(fn)(y)
    np.asarray(gr, np.float64).tofile(f"{g}/{tag}/gy_{name}{suf}.f64")
    print(tag, name, float(v), float(jnp.linalg.norm(gr)))
