# The learner as the reference for its C++ port

These scripts run the user's own MaterialLab learner (`materiallab/engine2.py`, `fit2.py`,
`partials.py`), unchanged, to produce the numbers the C++ port in
`Source/neobeat/shared/learner/` is checked against. The learner itself is not in this repository:
pass the folder it was unpacked to (from `MaterialLab.zip` in the user's Learner folder).

```
pip install --break-system-packages "jax>=0.7" soundfile scipy
python3 -I golden.py PKG_DIR CLIP.wav TARGET.json FITTED_MATERIAL.json OUTDIR   # start, fitted, a full fit
python3 -I export.py OUTDIR/start.npz OUTDIR/start                             # and for fitted
python3 -I -c "import numpy as np; np.load('OUTDIR/x.npy').astype(np.float32).tofile('OUTDIR/x.f32')"
python3 -I analysis_dump.py PKG_DIR OUTDIR && python3 -I analysis_export.py OUTDIR    # stage 4a
python3 -I init_export.py OUTDIR                                                      # stage 4b
for t in start fitted; do                                                             # stages 2 and 5a
  python3 -I render64.py PKG_DIR OUTDIR $t
  python3 -I grad_y.py PKG_DIR OUTDIR $t
  X64=1 python3 -I grad_terms.py PKG_DIR OUTDIR $t
done
g++ -std=c++17 -O2 -o test_learner ../test_learner.cpp && ./test_learner OUTDIR
```

Precision. The learner runs in single precision; the port runs in double. Where that matters the
checks compare against the learner run in double precision (`render64.py` swaps `jnp.float32` for
`float64` inside that script only; `grad_terms.py` with `X64=1`), which the port must match to
rounding, and report the single-precision numbers beside them:

- Bodies with glides (the tube clank's four sweeps) accumulate phase in long float32 sums: the
  learner's own float32 render is 2.7e-3 (relative rms) from its double render; the port is
  2.6e-8 from the double render.
- The spectral term's gradient changes sign in bins near the noise level, so float32 STFT noise
  moves it: the port's gradient points the same way as the learner's float32 gradient (cosine
  0.99989-1.0) and equals its double-precision gradient to 3e-9. The port casts the Hann window to
  single precision as the learner does, because that rounding alone moves the gradient by 0.5%.

`ref1.py` re-scores an existing fitted material with the learner's own loss: the first check that
the reference reproduces the learner as it ran for the user (PVC pipe: 0.66861 + regularisation
0.00213 = 0.67074, the recorded loss).
