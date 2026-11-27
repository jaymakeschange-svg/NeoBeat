# Dumps the learner's analysis of a clip, step by step, for checking the C++ port (stage 4).
# usage: python3 -I analysis_dump.py PKG_DIR GOLDEN_DIR
import sys, json
pkg, g = sys.argv[1:3]
sys.path.insert(0, pkg)
import numpy as np
from materiallab import partials as PT, engine2 as E, fit2 as F
fs = 48000
x = np.load(f"{g}/x.npy").astype(np.float64)
an = PT.analyze(x.astype(np.float32), fs)
tracks = [{"start": t.start, "f": t.freqs, "a": t.amps} for t in an.tracks]
desc = [PT.describe_track(tr, an.hop, fs) for tr in an.tracks]
desc = [{k: v for k, v in d.items() if not k.startswith("track_")} if d else None for d in desc]
sweeps = PT.find_sweeps(x.astype(np.float32), fs)
stop = PT.find_contact_stop(x.astype(np.float32), fs)
gains, rates = PT.body_noise_bands(x.astype(np.float32), fs, an, E.band_edges(fs))
nz = PT.synthesize_noise(an, len(x))
det = PT.synthesize_partials(an.tracks, fs, an.hop, len(x))
np.save(f"{g}/an_res.npy", an.residual_env); np.save(f"{g}/an_noise.npy", nz); np.save(f"{g}/an_det.npy", det)
json.dump({"tracks": tracks, "desc": desc, "sweeps": sweeps, "stop": stop,
           "gains": gains.tolist(), "rates": rates.tolist(), "res_shape": list(an.residual_env.shape)},
          open(f"{g}/analysis.json", "w"))
print(g, "tracks", len(tracks), "sweeps", len(sweeps), "stop", stop, "res", an.residual_env.shape)
