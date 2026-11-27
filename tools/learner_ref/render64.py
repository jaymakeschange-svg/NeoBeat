# engine2.render_physical in double precision at a golden point: the exact reference for the port's
# render (the learner itself runs in float32, which drifts on long gliding phases).
# usage: python3 -I render64.py PKG_DIR GOLDEN_DIR start|fitted
import sys
pkg, g, tag = sys.argv[1:4]
sys.path.insert(0, pkg)
import numpy as np, jax
jax.config.update("jax_enable_x64", True)
import jax.numpy as jnp
jnp.float32 = jnp.float64   # diagnostic only: run the learner's render in double precision
from materiallab import engine2 as E
z = np.load(f"{g}/{tag}.npz")
p = {k[2:]: jnp.asarray(z[k], jnp.float64) for k in z.files if k.startswith("p_")}
k = len(p["f"]); n = len(z["y"])
y = E.render_physical(p, n, 48000.0, jnp.asarray(z["bands"], jnp.float64), jnp.asarray(z["bodyb"], jnp.float64)[None] if z["bodyb"].ndim == 2 else jnp.asarray(z["bodyb"], jnp.float64),
                      jnp.asarray(z["phases"], jnp.float64), jnp.ones((1, k)), jnp.asarray(E.band_centres(48000.0), jnp.float64))[0]
y = np.asarray(y, np.float64); y.tofile(f"{g}/{tag}/y64.f64")
y32 = z["y"].astype(np.float64)
print(tag, "float32 learner vs double learner: relative rms", np.linalg.norm(y32 - y) / np.linalg.norm(y), "peak", np.abs(y32 - y).max())
