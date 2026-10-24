"""Guided_RCP: self-guided fast guided filter illumination + MSRCP reconstruction."""

import numpy as np
import pytest
import vapoursynth as vs
from scipy.ndimage import gaussian_filter, uniform_filter

from conftest import core, gray, to_array, to_clip


def make_image(h=40, w=56, seed=1):
    rng = np.random.default_rng(seed)
    a = gaussian_filter(rng.random((h, w)), 2) * 0.8 + 0.1
    a = np.clip(a + 0.05 * rng.standard_normal(a.shape), 0.01, 1)
    return np.round(a * 65535).astype(np.uint16)




def box(x, r):
    # BoxFilterHorizontal/Vertical clamp at the edges == per-axis nearest-mode mean filter
    return uniform_filter(x, 2 * r + 1, mode="nearest")


def guided_filter_ref(I, r, eps):
    """He et al. self-guided filter (I == p)."""
    m = box(I, r)
    var = box(I * I, r) - m * m
    a = var / (var + eps)
    b = m * (1 - a)
    return box(a, r) * I + box(b, r)


def guided_rcp_ref(I, r, eps):
    """Gray, full range in/out, lower_thr == upper_thr == 0: single-scale retinex on the
    guided-filter illumination followed by a plain min/max stretch to [0, 1]."""
    q = guided_filter_ref(I, r, eps)
    o = np.log(I / q + 1)
    return (o - o.min()) / (o.max() - o.min())


@pytest.mark.parametrize("radius,eps", [(1, 0.1), (3, 0.01), (8, 0.001)])
def test_matches_numpy_reference(radius, eps):
    i16 = make_image()
    I = i16 / 65535.0
    out = core.retinex.Guided_RCP(
        to_clip(i16), radius=radius, eps=eps, subsample=1, lower_thr=0, upper_thr=0, fulls=1, fulld=1
    )
    o = gray(out) / 65535.0
    assert np.abs(o - guided_rcp_ref(I, radius, eps)).max() < 2 / 65535


@pytest.mark.parametrize("fmt", [vs.GRAY16, vs.YUV444P8, vs.RGB48])
def test_radius_zero_passthrough(fmt):
    fi = core.get_video_format(fmt)
    rng = np.random.default_rng(0)
    mx = (1 << fi.bits_per_sample) - 1
    arr = rng.integers(0, mx + 1, (fi.num_planes, 12, 20)).astype(np.uint8 if mx == 255 else np.uint16)
    np.testing.assert_array_equal(to_array(core.retinex.Guided_RCP(to_clip(arr, fmt), radius=0)), arr)


@pytest.mark.parametrize("eps", [0.0, -0.01])
def test_eps_non_positive_rejected(eps):
    clip = core.std.BlankClip(width=8, height=8, format=vs.GRAY16, length=1)
    with pytest.raises(vs.Error, match="eps"):
        core.retinex.Guided_RCP(clip, eps=eps)


@pytest.mark.parametrize("bad", [{"radius": -1}, {"subsample": 0}])
def test_bad_args_rejected(bad):
    clip = core.std.BlankClip(width=8, height=8, format=vs.GRAY16, length=1)
    with pytest.raises(vs.Error):
        core.retinex.Guided_RCP(clip, **bad)


@pytest.mark.parametrize("size", [(1, 1), (3, 5), (17, 9)])
@pytest.mark.parametrize("subsample", [4, 16, 64])
def test_subsample_larger_than_frame(size, subsample):
    w, h = size
    arr = make_image(h, w, seed=w + h)
    o = to_array(core.retinex.Guided_RCP(to_clip(arr), radius=20, subsample=subsample))
    assert o.shape == (1, h, w)


@pytest.mark.parametrize("fmt", [vs.GRAY8, vs.YUV444P16, vs.RGB24])
def test_formats_and_determinism(fmt):
    fi = core.get_video_format(fmt)
    base = make_image(48, 64) / 65535.0
    mx = (1 << fi.bits_per_sample) - 1
    arr = np.stack([base] * fi.num_planes)
    arr = np.round(arr * mx).astype(np.uint8 if mx == 255 else np.uint16)
    clip = to_clip(arr, fmt)
    a = to_array(core.retinex.Guided_RCP(clip, radius=6, subsample=2))
    b = to_array(core.retinex.Guided_RCP(clip, radius=6, subsample=2))
    assert a.shape == arr.shape and a.dtype == arr.dtype
    np.testing.assert_array_equal(a, b)
    assert a.min() != a.max()


def test_subsample_close_to_full_resolution():
    # the fast guided filter (subsample > 1) approximates the full-resolution one
    clip = to_clip(make_image(96, 128))
    kw = dict(radius=8, eps=0.01, lower_thr=0, upper_thr=0, fulls=1, fulld=1)
    full = gray(core.retinex.Guided_RCP(clip, subsample=1, **kw)) / 65535
    fast = gray(core.retinex.Guided_RCP(clip, subsample=2, **kw)) / 65535
    assert np.abs(full - fast).mean() < 0.03
