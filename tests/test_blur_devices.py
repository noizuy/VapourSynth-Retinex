"""MSRCP/MSRCR through each CPU blur device: formats, shapes, edge cases, argument errors and
cross-device agreement. Blur accuracy itself is covered directly in test_gaussblur.py."""

import numpy as np
import pytest
import vapoursynth as vs
from scipy.ndimage import gaussian_filter

from conftest import CPU_DEVICES, core, to_array, to_clip

FORMATS = {
    "MSRCP": [vs.GRAY8, vs.GRAY16, vs.YUV444P8, vs.YUV444P16, vs.RGB24, vs.RGB48],
    "MSRCR": [vs.RGB24, vs.RGB48],
}
CASES = [(fn, fmt) for fn, fmts in FORMATS.items() for fmt in fmts]


def random_clip(fmt, w, h, seed=0, smooth=0):
    fi = core.get_video_format(fmt)
    rng = np.random.default_rng(seed)
    a = rng.random((fi.num_planes, h, w))
    if smooth:
        a = np.stack([gaussian_filter(p, smooth) for p in a])
        a = (a - a.min()) / max(a.max() - a.min(), 1e-12)
    mx = (1 << fi.bits_per_sample) - 1
    arr = np.round(a * mx).astype(np.uint8 if fi.bits_per_sample == 8 else np.uint16)
    return to_clip(arr, fmt), arr


def run(fn, clip, **kw):
    return getattr(core.retinex, fn)(clip, **kw)


@pytest.mark.parametrize("fn,fmt", CASES, ids=lambda v: v if isinstance(v, str) else vs.PresetVideoFormat(v).name)
def test_formats(device, fn, fmt):
    clip, arr = random_clip(fmt, 48, 32, smooth=2)
    out = run(fn, clip, sigma=[4, 12, 30], device=device)
    assert out.format.id == clip.format.id
    o = to_array(out)
    assert o.shape == arr.shape and o.dtype == arr.dtype
    assert o.min() != o.max(), "output is flat"


@pytest.mark.parametrize("fn,fmt", [("MSRCP", vs.YUV444P16), ("MSRCP", vs.GRAY8), ("MSRCR", vs.RGB24)])
@pytest.mark.parametrize("size", [(1, 1), (1, 20), (20, 1), (8, 8), (17, 9)])
def test_tiny_and_odd_frames(device, fn, fmt, size):
    # default sigmas (25, 80, 250) are much larger than the frame
    w, h = size
    clip, arr = random_clip(fmt, w, h, seed=w * 31 + h)
    o = to_array(run(fn, clip, device=device))
    assert o.shape == arr.shape


@pytest.mark.parametrize("fn,fmt", [("MSRCP", vs.YUV444P16), ("MSRCR", vs.RGB48)])
def test_flat_input_finite(device, fn, fmt):
    clip = core.std.BlankClip(width=24, height=16, format=fmt, length=1, color=[30000] * 3)
    o = to_array(run(fn, clip, sigma=[3, 10], device=device))
    assert o.shape == (3, 16, 24)


@pytest.mark.parametrize("fn,fmt", [("MSRCP", vs.YUV444P16), ("MSRCR", vs.RGB48)])
def test_sigma_zero_passthrough(fn, fmt):
    clip, arr = random_clip(fmt, 16, 8)
    np.testing.assert_array_equal(to_array(run(fn, clip, sigma=[0])), arr)
    np.testing.assert_array_equal(to_array(run(fn, clip, sigma=[0, 0, 0])), arr)


def test_zero_scale_among_others(device):
    # a 0 scale is skipped (contributes a constant), so it must not break the other scales
    clip, _ = random_clip(vs.YUV444P16, 32, 24, smooth=2)
    o = to_array(core.retinex.MSRCP(clip, sigma=[0, 8], device=device))
    assert o.min() != o.max()


@pytest.mark.parametrize("fn,fmt", [("MSRCP", vs.YUV444P16), ("MSRCR", vs.RGB48)])
def test_deterministic(device, fn, fmt):
    clip, _ = random_clip(fmt, 40, 24, smooth=1)
    a = to_array(run(fn, clip, sigma=[5, 15], device=device))
    b = to_array(run(fn, clip, sigma=[5, 15], device=device))
    np.testing.assert_array_equal(a, b)


@pytest.mark.parametrize("fn,fmt,tol", [("MSRCP", vs.RGB48, 0.02), ("MSRCR", vs.RGB48, 0.03)], ids=["MSRCP", "MSRCR"])
def test_cross_device_agreement(fn, fmt, tol):
    # the backends approximate the same Gaussian, so final outputs should be close (mean abs
    # difference, normalised to [0,1]); differences concentrate at the frame borders where
    # each backend extends the image differently.
    clip, _ = random_clip(fmt, 128, 96, seed=5, smooth=3)
    outs = {d: to_array(run(fn, clip, sigma=[6, 20, 50], device=d)) / 65535.0 for d in CPU_DEVICES}
    for d in ("cpu_box", "cpu_ass"):
        assert np.abs(outs[d] - outs["cpu_iir"]).mean() < tol, d


def test_repeat_last_device_rule():
    clip, _ = random_clip(vs.YUV444P16, 64, 48, smooth=2)
    sig = [3, 10, 30]
    a = to_array(core.retinex.MSRCP(clip, sigma=sig, device=["cpu_box", "cpu_iir"]))
    b = to_array(core.retinex.MSRCP(clip, sigma=sig, device=["cpu_box", "cpu_iir", "cpu_iir"]))
    c = to_array(core.retinex.MSRCP(clip, sigma=sig, device=["cpu_box", "cpu_box", "cpu_box"]))
    np.testing.assert_array_equal(a, b)
    assert not np.array_equal(a, c)
    np.testing.assert_array_equal(
        to_array(core.retinex.MSRCP(clip, sigma=sig, device="cpu_ass")),
        to_array(core.retinex.MSRCP(clip, sigma=sig, device=["cpu_ass"] * 3)),
    )


def test_default_device_is_cpu_box():
    clip, _ = random_clip(vs.YUV444P16, 32, 24, smooth=2)
    np.testing.assert_array_equal(
        to_array(core.retinex.MSRCP(clip, sigma=[5, 20])),
        to_array(core.retinex.MSRCP(clip, sigma=[5, 20], device="cpu_box")),
    )


@pytest.mark.parametrize("fn,fmt", [("MSRCP", vs.YUV444P8), ("MSRCR", vs.RGB24)])
def test_unknown_device_error(fn, fmt):
    clip = core.std.BlankClip(width=8, height=8, format=fmt, length=1)
    with pytest.raises(vs.Error) as exc:
        run(fn, clip, device=["cpu_box", "nope"])
    for name in ("cpu_box", "cpu_iir", "cpu_ass", "gpu", "cuda"):
        assert f'"{name}"' in str(exc.value)


@pytest.mark.parametrize("fn,fmt", [("MSRCP", vs.YUV444P8), ("MSRCR", vs.RGB24)])
@pytest.mark.parametrize("sigma", [float("nan"), float("inf"), -1.0])
def test_bad_sigma_rejected(fn, fmt, sigma):
    clip = core.std.BlankClip(width=8, height=8, format=fmt, length=1)
    with pytest.raises(vs.Error, match="sigma"):
        run(fn, clip, sigma=[10, sigma])


@pytest.mark.parametrize(
    "dev,identifier,package",
    [("gpu", "com.julek.vszipcl", "vapoursynth-zipcl"), ("cuda", "com.julek.vszipcu", "vapoursynth-zipcu")],
)
def test_gpu_without_plugin_clear_error(dev, identifier, package):
    if any(p.identifier == identifier for p in core.plugins()):
        pytest.skip(f"{identifier} is installed")
    clip = core.std.BlankClip(width=8, height=8, format=vs.YUV444P8, length=1)
    with pytest.raises(vs.Error, match=package):
        core.retinex.MSRCP(clip, sigma=[5, 10], device=["cpu_box", dev])


@pytest.mark.parametrize("fn", ["MSRCP", "MSRCR"])
def test_subsampled_rejected(fn):
    clip = core.std.BlankClip(width=8, height=8, format=vs.YUV420P8, length=1)
    with pytest.raises(vs.Error):
        run(fn, clip)
