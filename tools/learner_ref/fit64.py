# The learner's own fit2.fit in double precision (float32 read as float64 in engine2 and fit2, inside
# this script only). AS_PORT=1 adds the port's two choices (Hann window and starting point rounded to
# float32), so the run is the port's fit step for step until roundoff parts them (stage 6a uses
# STEPS=6). Without AS_PORT, a check of how much precision alone moves the outcome.
# usage: [AS_PORT=1] [STEPS=n] python3 -I fit64.py PKG_DIR GOLDEN_DIR
import sys, json
pkg, g = sys.argv[1:3]
sys.path.insert(0, pkg)
import numpy as np, jax, os
jax.config.update("jax_enable_x64", True)
import jax.numpy as jnp
from materiallab import engine2 as E, fit2 as F
class _J:
    float32 = jnp.float64
    def __getattr__(self, k): return getattr(jnp, k)
E.jnp = _J(); F.jnp = _J()
import os
if os.environ.get("AS_PORT"):   # the port's choices: window rounded to float32, start rounded to float32
    F._mag = lambda v, n: jnp.abs(jnp.fft.rfft(F._frames(v, n, n // 4) * jnp.asarray(np.hanning(n).astype(np.float32), jnp.float64), axis=-1))
    _unc = E.unconstrained
    E.unconstrained = lambda p: {kk: jnp.asarray(np.asarray(vv).astype(np.float32), jnp.float64) for kk, vv in _unc(p).items()}
x = np.load(f"{g}/x.npy"); meta = json.load(open(f"{g}/meta.json"))
steps = int(os.environ.get("STEPS", "300"))
res = F.fit(x, 48000.0, steps=steps, noise_floor_db=meta["noise_floor_db"],
            progress=lambda i, s, l: print(f"fit {i}/{s} {l:.4f}", flush=True))
print("double: start", res["start_loss"], "best", res["loss"], "seconds", res["seconds"])
open(f"{g}/fit64" + ("port" if os.environ.get("AS_PORT") else "") + (str(steps) if steps != 300 else "") + "_history.txt", "w").write(" ".join(repr(float(v)) for v in res["history"]))
