# Writes one golden point as plain files the C++ test reads: body.txt (physical params), and raw
# float32 arrays: bands, bodyb, y (the learner's render), x (the target), phases (float64).
import sys, numpy as np, json
src, outd = sys.argv[1], sys.argv[2]
import os; os.makedirs(outd, exist_ok=True)
d = np.load(src)
keys = [k[2:] for k in d.files if k.startswith("p_")]
with open(f"{outd}/body.txt", "w") as fh:
    for k in keys:
        v = np.atleast_1d(d["p_" + k]).astype(np.float64)
        fh.write(k + " " + str(len(v)) + " " + " ".join(repr(float(a)) for a in v) + "\n")
d["bands"].astype(np.float32).tofile(f"{outd}/bands.f32")
d["bodyb"][0].astype(np.float32).tofile(f"{outd}/bodyb.f32")
d["y"].astype(np.float32).tofile(f"{outd}/y.f32")
d["phases"].astype(np.float64).tofile(f"{outd}/phases.f64")
open(f"{outd}/loss.txt","w").write(repr(float(d["loss"])))
print(src, "n", d["y"].shape, "k", len(d["p_f"]), "loss", float(d["loss"]))
# the unconstrained parameters and the learner's gradient with respect to them (float32, as it ran)
for pre, name in (("u_", "u.txt"), ("g_", "gu.txt")):
    with open(f"{outd}/{name}", "w") as fh:
        for k in [f[2:] for f in d.files if f.startswith(pre)]:
            v = np.atleast_1d(d[pre + k]).astype(np.float64)
            fh.write(k + " " + str(len(v)) + " " + " ".join(repr(float(a)) for a in v) + "\n")
