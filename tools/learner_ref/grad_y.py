# The learner's loss gradient with respect to the rendered samples, single precision as it runs.
# usage: python3 -I grad_y.py PKG_DIR GOLDEN_DIR start|fitted

import sys, json
pkg, g, tag = sys.argv[1:4]
sys.path.insert(0, pkg)
import numpy as np, jax, jax.numpy as jnp
from materiallab import fit2 as F
x = np.load(f"{g}/x.npy"); meta = json.load(open(f"{g}/meta.json"))
loss_fn = F.make_loss(x, 48000.0, meta["noise_floor_db"])
y = jnp.asarray(np.load(f"{g}/{tag}.npz")["y"], jnp.float32)
gy = jax.grad(loss_fn)(y)
np.asarray(gy, dtype=np.float64).tofile(f"{g}/{tag}/gy.f64")
print(tag, "dL/dy norm", float(jnp.linalg.norm(gy)))
