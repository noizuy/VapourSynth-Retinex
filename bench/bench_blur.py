"""Throughput benchmark for the Retinex plugin's CPU blurs, MSRCP and Guided_RCP.

Usage: python bench/bench_blur.py [--width 1920 --height 1080 --frames 40 --repeats 3 --threads N]
Prints a markdown table of median fps (frames drained in parallel via clip.frames()).
"""

import argparse
import os
import statistics
import time

import vapoursynth as vs

core = vs.core

DEVICES = ("cpu_box", "cpu_iir", "cpu_ass")
SIGMAS = (5, 25, 80, 250)

def source(fmt, w, h, length, distinct=4):
    """Noise + gradient clip; `distinct` frames are rendered once with numpy and then looped (no per-frame copy)."""
    import numpy as np

    rng = np.random.default_rng(1)
    blank = core.std.BlankClip(width=w, height=h, format=fmt, length=length, keep=True)
    ramp = np.linspace(0.0, 1.0, w, dtype=np.float32)[None, :]
    pre = []
    for _ in range(distinct):
        f = blank.get_frame(0).copy()
        for p in range(f.format.num_planes):
            v = rng.random((h, w), dtype=np.float32) * 0.6 + ramp * 0.4
            a = np.asarray(f[p])
            a[...] = v if fmt == vs.GRAYS else (v * 65535).astype(np.uint16)
        pre.append(f)
    return core.std.ModifyFrame(blank, blank, lambda n, f: pre[n % distinct])


def cpu_limit():
    """Usable CPUs: affinity mask capped by a cgroup v2 quota (VS oversubscribes badly under a CFS quota)."""
    n = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else os.cpu_count() or 1
    try:
        quota, period = open("/sys/fs/cgroup/cpu.max").read().split()
        if quota != "max":
            n = min(n, max(1, int(quota) // int(period)))
    except (OSError, ValueError):
        pass
    return n


def fps(clip, frames, repeats):
    clip = clip[:frames]
    for _ in clip[: min(frames, core.num_threads)].frames():  # warm-up (node init, thread spin-up)
        pass
    runs = []
    for _ in range(repeats):
        t = time.perf_counter()
        for _ in clip.frames():
            pass
        runs.append(frames / (time.perf_counter() - t))
    return statistics.median(runs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--frames", type=int, default=40)
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--threads", type=int, default=0, help="core.num_threads (0 = usable CPUs, honouring a cgroup quota)")
    ap.add_argument("--filter", default="", help="only run cases whose name contains this substring")
    a = ap.parse_args()
    core.num_threads = a.threads if a.threads > 0 else cpu_limit()

    w, h, n = a.width, a.height, a.frames
    gs = source(vs.GRAYS, w, h, n)
    yuv = source(vs.YUV444P16, w, h, n)
    r = core.retinex
    cases = [("source GrayS", gs), ("source YUV444P16", yuv)]
    for d in DEVICES:
        cases += [(f"GaussBlur {d} sigma={s}", r.GaussBlur(gs, sigma=s, device=d)) for s in SIGMAS]
    cases += [(f"MSRCP {d}", r.MSRCP(yuv, device=d)) for d in DEVICES]
    cases += [("Guided_RCP", r.Guided_RCP(yuv))]

    print(f"{w}x{h}, {n} frames, median of {a.repeats}, threads={core.num_threads}\n")
    print("| case | fps |\n|---|---:|")
    for name, clip in cases:
        if a.filter in name:
            print(f"| {name} | {fps(clip, n, a.repeats):.1f} |", flush=True)


if __name__ == "__main__":
    main()
