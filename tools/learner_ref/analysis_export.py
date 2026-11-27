# analysis.json + arrays -> plain files for the C++ stage-4 check
import sys, json, numpy as np
g = sys.argv[1]
d = json.load(open(f"{g}/analysis.json"))
with open(f"{g}/tracks.txt", "w") as fh:
    for t in d["tracks"]:
        fh.write(f"{t['start']} {len(t['f'])} " + " ".join(repr(v) for v in t["f"]) + " " + " ".join(repr(v) for v in t["a"]) + "\n")
kinds = {"decays": 0, "beats": 1, "swells": 2, "sweeps": 3}
with open(f"{g}/desc.txt", "w") as fh:
    for q in d["desc"]:
        if q is None: fh.write("0\n"); continue
        dec = q["decay_db_per_s"]
        fh.write(f"1 {kinds[q['kind']]} {q['freq_hz']!r} {q['born_ms']!r} {q['peak_ms']!r} {q['rise_db']!r} {('nan' if dec is None else repr(dec))} {q['glide_cents']!r} {q['beat_hz']!r} {q['beat_depth_db']!r} {q['energy']!r}\n")
np.load(f"{g}/an_res.npy").astype(np.float64).tofile(f"{g}/an_res.f64")
np.load(f"{g}/an_noise.npy").astype(np.float32).tofile(f"{g}/an_noise.f32")
np.load(f"{g}/an_det.npy").astype(np.float32).tofile(f"{g}/an_det.f32")
with open(f"{g}/bandsfit.txt", "w") as fh:
    fh.write(" ".join(repr(v) for v in d["gains"]) + "\n" + " ".join(repr(v) for v in d["rates"]) + "\n")
print(g, "exported")
