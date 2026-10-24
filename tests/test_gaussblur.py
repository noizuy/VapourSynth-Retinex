"""Direct tests of the Gaussian blur backends via the diagnostic retinex.GaussBlur function."""

import math

import numpy as np
import pytest
import vapoursynth as vs
from scipy.ndimage import gaussian_filter

from conftest import CPU_DEVICES, core, gauss, gray, to_clip

# Relative tolerance on the impulse response's standard deviation vs. sigma (sigma >= 2).
#   cpu_box: Kovesi 3-box widths are quantised to odd integers -> std slightly low (~3-9 %).
#   cpu_iir: the Young/van Vliet 1995 q(sigma) formula fits the Gaussian's shape, but the
#            filter's heavier tails give a std ~9-13 % above sigma (property of the method).
#   cpu_ass: least-squares fitted cascade, essentially exact.
STD_RTOL = {"cpu_box": 0.10, "cpu_iir": 0.15, "cpu_ass": 0.02}

# Max abs error vs scipy.ndimage.gaussian_filter(mode="nearest") on U[0,1) noise, away from
# the borders (border handling differs per backend, see test_border_* below).
INTERIOR_ATOL = {"cpu_box": 0.02, "cpu_iir": 0.02, "cpu_ass": 0.005}
# Mean abs error over the full frame (borders included) vs mode="nearest".
FULL_MAE = {"cpu_box": 0.01, "cpu_iir": 0.005, "cpu_ass": 0.02}


def impulse(h, w, y=None, x=None):
    a = np.zeros((h, w))
    a[h // 2 if y is None else y, w // 2 if x is None else x] = 1
    return a


def moments(o):
    yy, xx = np.mgrid[: o.shape[0], : o.shape[1]]
    tot = o.sum()
    cy, cx = (o * yy).sum() / tot, (o * xx).sum() / tot
    vy = (o * (yy - cy) ** 2).sum() / tot
    vx = (o * (xx - cx) ** 2).sum() / tot
    return tot, cy, cx, vy, vx


@pytest.mark.parametrize("sigma", [1, 2, 5, 10])
def test_impulse_centroid(device, sigma):
    # regression guard: cpu_ass used to shift by ~2/6/14 px at sigma 5/10/20 (expand taps k-2..k)
    h, w = 129, 129
    tot, cy, cx, _, _ = moments(gauss(impulse(h, w), sigma, device))
    assert tot == pytest.approx(1, abs=1e-3)
    assert cy == pytest.approx(h // 2, abs=0.05)
    assert cx == pytest.approx(w // 2, abs=0.05)


@pytest.mark.parametrize("pos", [(12, 20), (50, 13)])  # >= 4 sigma from the borders
def test_impulse_off_centre_not_shifted(device, pos):
    tot, cy, cx, _, _ = moments(gauss(impulse(64, 64, *pos), 2.5, device))
    assert (cy, cx) == pytest.approx(pos, abs=0.05)


@pytest.mark.parametrize("sigma", [2, 5, 10])
def test_impulse_variance(device, sigma):
    _, _, _, vy, vx = moments(gauss(impulse(129, 129), sigma, device))
    assert math.sqrt(vy) == pytest.approx(sigma, rel=STD_RTOL[device])
    assert math.sqrt(vx) == pytest.approx(sigma, rel=STD_RTOL[device])


@pytest.mark.parametrize("shape", [(1, 1), (1, 37), (37, 1), (8, 8), (17, 9), (64, 48)])
@pytest.mark.parametrize("sigma", [0.3, 3, 50])
def test_flat_field_preserved(device, shape, sigma):
    a = np.full(shape, 0.37)
    o = gauss(a, sigma, device)
    assert o.shape == shape
    np.testing.assert_allclose(o, 0.37, atol=1e-5)


@pytest.mark.parametrize("sigma", [2, 5, 10])
def test_matches_scipy(device, sigma):
    rng = np.random.default_rng(1234)
    a = rng.random((96, 128))
    ref = gaussian_filter(a, sigma, mode="nearest")
    o = gauss(a, sigma, device)
    m = int(3 * sigma) + 2
    assert np.abs(o - ref)[m:-m, m:-m].max() < INTERIOR_ATOL[device]
    assert np.abs(o - ref).mean() < FULL_MAE[device]


def test_border_iir_is_exact_replicate():
    # cpu_iir uses Triggs-Sdika boundary init: equals mode="nearest" right up to the edges,
    # even when sigma is large compared to the frame.
    rng = np.random.default_rng(7)
    for shape, sigma in [((96, 128), 5), ((17, 9), 25), ((1, 40), 60)]:
        a = rng.random(shape)
        o = gauss(a, sigma, "cpu_iir")
        assert np.abs(o - gaussian_filter(a, sigma, mode="nearest")).max() < 0.01


def test_border_ass_even_size_is_reflect():
    # cpu_ass's pyramid has half-sample-symmetric ("reflect") borders for even dimensions.
    rng = np.random.default_rng(7)
    a = rng.random((96, 128))
    for sigma in (8, 25):
        o = gauss(a, sigma, "cpu_ass")
        assert np.abs(o - gaussian_filter(a, sigma, mode="reflect")).max() < 0.03


@pytest.mark.parametrize("shape", [(96, 128), (17, 9)])
def test_flip_symmetry(device, shape):
    # cpu_ass is only approximately symmetric on odd sizes (pyramid rounding at the far edge)
    rng = np.random.default_rng(3)
    a = rng.random(shape)
    o = gauss(a, 4, device)
    of = gauss(a[::-1, ::-1], 4, device)[::-1, ::-1]
    tol = 0.1 if (device == "cpu_ass" and shape[0] % 2) else 1e-5
    assert np.abs(o - of).max() < tol


@pytest.mark.parametrize("sigma", [0.05, 0.1, 0.2, 0.25, 0.3, 0.4, 0.5])
def test_small_sigma_stable(device, sigma):
    a = impulse(15, 15)
    o = gauss(a, sigma, device)
    assert np.isfinite(o).all()
    assert o.min() >= -1e-6, "negative lobes"
    tot, cy, cx, vy, vx = moments(o)
    assert tot == pytest.approx(1, abs=1e-4)
    assert (cy, cx) == pytest.approx((7, 7), abs=1e-4)
    # never blurs (much) more than requested
    assert math.sqrt(vx) <= sigma * 1.25 + 1e-6


def test_small_sigma_monotone(device):
    stds = []
    for sigma in [0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.7, 1.0, 1.5]:
        stds.append(math.sqrt(moments(gauss(impulse(21, 21), sigma, device))[4]))
    assert all(b >= a - 1e-6 for a, b in zip(stds, stds[1:])), stds


def test_sigma_zero_is_identity(device):
    a = np.random.default_rng(0).random((9, 13)).astype(np.float32)
    clip = to_clip(a)
    out = core.retinex.GaussBlur(clip, sigma=0, device=device)
    np.testing.assert_array_equal(gray(out), a)


def test_default_device_is_cpu_box():
    a = np.random.default_rng(0).random((20, 30)).astype(np.float32)
    clip = to_clip(a)
    np.testing.assert_array_equal(
        gray(core.retinex.GaussBlur(clip, sigma=3)),
        gray(core.retinex.GaussBlur(clip, sigma=3, device="cpu_box")),
    )


@pytest.mark.parametrize("sigma", [float("nan"), float("inf"), float("-inf"), -1.0, -1e-9])
def test_bad_sigma_rejected(sigma):
    with pytest.raises(vs.Error, match="sigma"):
        core.retinex.GaussBlur(to_clip(np.zeros((8, 8), np.float32)), sigma=sigma)


@pytest.mark.parametrize("fmt", [vs.GRAY8, vs.GRAY16, vs.GRAYH, vs.YUV444PS, vs.RGBS])
def test_non_grays_rejected(fmt):
    clip = core.std.BlankClip(width=8, height=8, format=fmt, length=1)
    with pytest.raises(vs.Error, match="GrayS"):
        core.retinex.GaussBlur(clip, sigma=1)


def test_unknown_device_lists_names():
    with pytest.raises(vs.Error) as exc:
        core.retinex.GaussBlur(to_clip(np.zeros((8, 8), np.float32)), sigma=1, device="ass")
    for name in ("cpu_box", "cpu_iir", "cpu_ass", "gpu", "cuda"):
        assert name in str(exc.value)


def _has_plugin(identifier):
    return any(p.identifier == identifier for p in core.plugins())


@pytest.mark.parametrize(
    "dev,identifier,package",
    [("gpu", "com.julek.vszipcl", "vapoursynth-zipcl"), ("cuda", "com.julek.vszipcu", "vapoursynth-zipcu")],
)
def test_gpu_without_plugin_clear_error(dev, identifier, package):
    if _has_plugin(identifier):
        pytest.skip(f"{identifier} is installed")
    with pytest.raises(vs.Error, match=package):
        core.retinex.GaussBlur(to_clip(np.zeros((8, 8), np.float32)), sigma=1, device=dev)


def test_multiframe_and_props_passthrough():
    clip = core.std.BlankClip(width=16, height=8, format=vs.GRAYS, length=3, color=0.5)
    clip = core.std.SetFrameProps(clip, Foo=7)
    out = core.retinex.GaussBlur(clip, sigma=2, device="cpu_iir")
    assert out.num_frames == 3
    f = out.get_frame(2)
    assert f.props["Foo"] == 7
    np.testing.assert_allclose(np.asarray(f[0]), 0.5, atol=1e-6)
