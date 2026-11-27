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
g++ -std=c++17 -O2 -o test_learner ../test_learner.cpp && ./test_learner OUTDIR
```

`ref1.py` re-scores an existing fitted material with the learner's own loss: the first check that
the reference reproduces the learner as it ran for the user (PVC pipe: 0.66861 + regularisation
0.00213 = 0.67074, the recorded loss).
