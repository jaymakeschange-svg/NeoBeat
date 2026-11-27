# The learner's starting body (fit2.initial_params, saved by golden.py as init.npz) as init_body.txt,
# in body.txt's format, for the C++ stage-4b check.
import sys, numpy as np
g = sys.argv[1]
d = np.load(f"{g}/init.npz")
with open(f"{g}/init_body.txt", "w") as fh:
    for k in d.files:
        v = np.atleast_1d(d[k]).astype(np.float64)
        fh.write(k + " " + str(len(v)) + " " + " ".join(repr(float(a)) for a in v) + "\n")
print(g, "init_body.txt")
