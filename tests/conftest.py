"""Shared fixtures/helpers for the Retinex plugin tests.

The plugin is expected to be pip-installed (autoloaded from site-packages/vapoursynth/plugins).
"""

import numpy as np
import pytest
import vapoursynth as vs

core = vs.core

if not hasattr(core, "retinex"):
    pytest.skip(
        "vs.core.retinex is not available; pip-install the plugin first (pip install .)",
        allow_module_level=True,
    )

CPU_DEVICES = ("cpu_box", "cpu_iir", "cpu_ass")

_GRAY_FORMATS = {
    (np.dtype(np.float32), 1): vs.GRAYS,
    (np.dtype(np.uint8), 1): vs.GRAY8,
    (np.dtype(np.uint16), 1): vs.GRAY16,
    (np.dtype(np.float32), 3): vs.YUV444PS,
    (np.dtype(np.uint8), 3): vs.YUV444P8,
    (np.dtype(np.uint16), 3): vs.YUV444P16,
}


def to_clip(planes, fmt=None):
    """Build a 1-frame clip from a 2-D array (one plane) or a (P, H, W) array / list of planes."""
    arr = np.asarray(planes)
    if arr.ndim == 2:
        arr = arr[None]
    nplanes, h, w = arr.shape
    if fmt is None:
        fmt = _GRAY_FORMATS[(arr.dtype, nplanes)]
    blank = core.std.BlankClip(width=w, height=h, format=fmt, length=1, keep=True)

    def _fill(n, f):
        fout = f.copy()
        for p in range(fout.format.num_planes):
            np.asarray(fout[p])[...] = arr[p]
        return fout

    return core.std.ModifyFrame(blank, blank, _fill)


def to_array(clip, n=0):
    """Return frame n of clip as a (P, H, W) numpy array (copied)."""
    f = clip.get_frame(n)
    return np.stack([np.array(f[p], copy=True) for p in range(f.format.num_planes)])


def gray(clip, n=0):
    """Plane 0 of frame n as a 2-D float64 array."""
    return to_array(clip, n)[0].astype(np.float64)


def gauss(arr, sigma, device="cpu_box"):
    """Run retinex.GaussBlur on a 2-D float array and return a 2-D float64 array."""
    clip = to_clip(np.asarray(arr, dtype=np.float32))
    return gray(core.retinex.GaussBlur(clip, sigma=sigma, device=device))


@pytest.fixture(params=CPU_DEVICES)
def device(request):
    return request.param
