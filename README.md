# VapourSynth-Retinex2

A bad fork of [VapourSynth-Retinex](https://github.com/HomeOfVapourSynthEvolution/VapourSynth-Retinex) with vibe coded features for alternative Gaussian blurs.

## Installation

Build and install from a source checkout (the hatch build hook compiles the plugin with [Meson](https://mesonbuild.com/) and installs it into `site-packages/vapoursynth/plugins`, where VapourSynth autoloads it):

```
pip install .
```

A C++14 compiler is required.

## About Retinex

The Retinex theory and algorithm mainly aims at simulating the color constancy feature of HVS(Human Visual System).

The light perceived by visual receptors can be separated into illuminance and reflectance. Retinex estimates the illuminance and derive the reflectance from the light, the filtered result of which is an image represents the reflectance characteristics of the scene, regardless of the illuminance.

Retinex is a very powerful filter in dynamic range compression, local contrast enhancement, color constancy, de-fog, etc.

## MSRCP

### Description

MSR(Multi Scale Retinex) is the most successful implementation of Retinex, based on center/surround theory.

MSRCP(Multi Scale Retinex with Chromaticity Preservation) is based on MSR. It applies MSR on intensity channel, and adjust UV/RGB according to the filtering result of intensity channel to preserve chromaticity.

As MSRCP preserves chromaticity, it is excellent for dynamic range compression and local contrast enhancement, while it doesn't eliminate color cast. To implement the full color constancy feature of Retinex, it is recommended to use MSRCR(Multi Scale Retinex with Color Restoration) instead.

This function accept 8-16bit integer Gray/YUV/RGB input. Sub-sampled format is not supported. If you want to process YUV420/YUV422 clip, convert it to YUV444 or RGB first.

For processing in YUV444 and RGB, the filtering results are different. The intensity channel on which MSR is applied, is Y for YUV444 input and (R+G+B)/3 for RGB input. Since Y is a weighted average of R, G, B, processing in YUV444 may produce imbalanced chromaticity preservation. Also when chroma_protect is larger than 1 (default 1.2), the saturation of YUV444 processing result will be different from that of RGB processing result.

### Usage

```python
retinex.MSRCP(clip input, float[] sigma=[25,80,250], float lower_thr=0.001, float upper_thr=0.001, bool fulls, bool fulld=fulls, float chroma_protect=1.2, data[] device=["cpu_box"], int[] device_id=[0], int[] num_streams=[1], int[] tune=[])
```

- input:<br />
    clip to process

- sigma: (Default: [25,80,250])<br />
    sigma of Gaussian function to apply Gaussian filtering.<br />
    Assign an array of multiple sigma to apply MSR.<br />
    Basically, in SSR(Single Scale Retinex), small sigma result in stronger dynamic range compression and local contrast enhancement, while large sigma result in better color rendition. To afford an acceptable trade-off between these features, MSR combines different scales to compute the final Retinex output.<br />
    It is recommended by most researches to use 3 different scales (balance between speed and quality).

- lower_thr: (Default: 0.001)<br />
    After applying MSR, a normalization is applied to the value range [Floor,Ceil] of the Retinex output.<br />
    This parameter define the ratio of pixel number to determine the Floor of value range. 0 means using the minimum value in the Retinex output.<br />
    Valid range is [0,1), and the sum of lower_thr and upper_thr should be less than 1.<br />
    Increase it to enhance the global contrast of the output image.

- upper_thr: (Default: 0.001)<br />
    After applying MSR, a normalization is applied to the value range [Floor,Ceil] of the Retinex output.<br />
    This parameter define the ratio of pixel number to determine the Ceil of value range. 0 means using the maximum value in the Retinex output.<br />
    Valid range is [0,1), and the sum of lower_thr and upper_thr should be less than 1.<br />
    Increase it if there are some extreme bright parts in the Retinex output which makes the whole image too dark.

- fulls: (Default: True for RGB input, False for YUV/Gray input)<br />
    Determine the value range of input clip. True means full range/PC range, and False means limited range/TV range.

- fulld: (Default: fulls)<br />
    Determine the value range of output clip. True means full range/PC range, and False means limited range/TV range.<br />
    Set different value for fulls and fulld will result in range conversion.

- chroma_protect: (Default: 1.2)<br />
    The base of log function to attenuate chroma adjustment. It could avoid extreme chroma amplifying, while the saturation of the result is changed.<br />
    Available range is [1, +inf), 1 means no attenuation.<br />
    It is only available for YUV input.

- device: (Default: ["cpu_box"])<br />
    Which implementation performs the Gaussian blur for each scale in `sigma`, one of "cpu_box", "cpu_iir", "cpu_ass", "gpu" or "cuda":<br />
    "cpu_ass" uses the [libass cascade blur](https://github.com/libass/libass/blob/master/libass/ass_blur.c), which provides a nice speed up with similar visual results for larger scales.<br />
    "cpu_box" approximates the Gaussian with a 3-pass box blur (sliding-window sums), entirely in this plugin — no extra dependency, and its cost is independent of sigma.<br />
    "cpu_iir" approximates the Gaussian with a single-pass Young/van Vliet 4th-order recursive filter, also entirely in this plugin and independent of sigma. Similar speed to "cpu_box"; either is a reasonable default.<br />
    "gpu" uses [vapoursynth-zipcl](https://github.com/dnjulek/vapoursynth-zipcl) (`com.julek.vszipcl`, OpenCL).<br />
    "cuda" uses [vapoursynth-zipcu](https://github.com/dnjulek/vapoursynth-zipcu) (`com.julek.vszipcu`, CUDA).<br />
    The gpu/cuda plugin must be installed for whichever of those devices are selected.<br />
    If fewer entries than `sigma` are given, the last entry is repeated for the remaining scales, so a single value applies to every scale.

- device_id: (Default: [0])<br />
    Which GPU/CUDA device to use, per scale (same repeat-last-if-shorter rule as `device`). Ignored for scales using a "cpu_*" device.

- num_streams: (Default: [1])<br />
    Number of concurrent streams for the "gpu"/"cuda" backends, per scale (same repeat-last-if-shorter rule as `device`). Ignored for scales using a "cpu_*" device.

- tune: (Default: unset)<br />
    OpenCL tuning parameter passed through to vapoursynth-zipcl, per scale (same repeat-last-if-shorter rule as `device`). Only used for scales using "gpu"; left at the plugin's own default when not given.

## MSRCR

### Description

MSRCR(Multi Scale Retinex with Color Restoration) is based on MSR. It applies MSR to each spectral channel (e.g. R, G and B), and modify the MSR output by multiplying it by a color restoration function of the chromaticity.

When MSR is applied to each spectral channel, it assumes the image obey gray world assumption. Otherwise, if the image violates gray world assumption, the MSR will produce grayish image by decreasing the color saturation, thus the color restoration step is proposed to solve this problem. However, for images with nice color balance, MSRCR still produces a desaturated look. Hence it is recommended to use MSRCP in most cases, and only apply MSRCR to the images with color cast. Also since MSRCR applies MSR to each spectral channel instead of intensity channel, it is slower than MSRCP.

This function only accept 8-16bit integer RGB input.

### Usage

```python
retinex.MSRCR(clip input, float[] sigma=[25,80,250], float lower_thr=0.001, float upper_thr=0.001, bool fulls=True, bool fulld=fulls, float restore=125, data[] device=["cpu_box"], int[] device_id=[0], int[] num_streams=[1], int[] tune=[])
```

- input:<br />
    clip to process

- sigma: (Default: [25,80,250])<br />
    The same as MSRCP.

- lower_thr: (Default: 0.001)<br />
    The same as MSRCP.

- upper_thr: (Default: 0.001)<br />
    The same as MSRCP.

- fulls: (Default: True)<br />
    The same as MSRCP.

- fulld: (Default: fulls)<br />
    The same as MSRCP.

- restore: (Default: 125)<br />
    The strength of the nonlinearity for color restoration function, larger value result in stronger restoration, available range is [0, +inf).<br />
    It is a multiplier in a log function, so try to adjust it in a large scale (e.g. multiply it by a power of 10) if you want to see any difference.

- device, device_id, num_streams, tune:<br />
    The same as MSRCP.

## Guided_RCP

### Description

Guided_RCP is MSRCP with its illumination estimate computed by a [Fast Guided Filter](https://arxiv.org/abs/1505.00996) (He & Sun) instead of a Gaussian/box/IIR blur, applied to the same intensity channel and reconstructed into RGB/YUV/Gray output the same chromaticity-preserving way MSRCP does (same `lower_thr`/`upper_thr`/`fulls`/`fulld`/`chroma_protect` behavior — see MSRCP above).

The guided filter is edge-aware: unlike a Gaussian blur, it doesn't smooth across strong edges, so the illumination estimate it produces has far less halo/ringing near hard edges (see [the reference implementation](https://github.com/accessify/guided-filter) this is based on). Because of that, a single guided filter application already gives a halo-free result — unlike MSRCP's `sigma`, there's no need for multiple scales or averaging, so `radius`/`eps` are single values, not arrays.

The "fast" part of the Fast Guided Filter computes the filter's local statistics at a downsampled resolution (by a factor of `subsample`) and upsamples the result, since the guided filter's per-pixel cost is independent of `radius` (like the `cpu_box`/`cpu_iir` devices above) — the only way to speed it up further is processing fewer pixels.

### Usage

```python
retinex.Guided_RCP(clip input, int radius=20, float eps=0.01, int subsample=4, float lower_thr=0.001, float upper_thr=0.001, bool fulls, bool fulld=fulls, float chroma_protect=1.2)
```

- input:<br />
    clip to process

- radius: (Default: 20)<br />
    The guided filter's local window radius, in pixels. Larger values smooth over a wider area (similar role to MSRCP's `sigma`, but a single value since only one application is needed).<br />
    0 disables filtering: the input is returned unchanged.

- eps: (Default: 0.01)<br />
    The guided filter's regularization parameter, in the same [0,1]-normalized units as the intensity channel. Smaller values preserve edges more strongly (less smoothing across them); larger values behave more like an ordinary blur. Must be positive.

- subsample: (Default: 4)<br />
    Downsampling factor used by the fast guided filter algorithm to compute local statistics at reduced resolution before upsampling back to full size. 1 disables the speedup and computes at full resolution (slower, marginally more accurate). Should generally not exceed `radius`: the window radius at the reduced resolution is `radius / subsample`, clamped to at least 1, so a larger `subsample` silently widens the effective window.

- lower_thr, upper_thr, fulls, fulld, chroma_protect:<br />
    The same as MSRCP.

## GaussBlur (diagnostic)

A test/diagnostic function exposing the Gaussian blur backends that MSRCP/MSRCR use internally, so they can be checked and benchmarked directly. Not intended for general use.

```python
retinex.GaussBlur(clip clip, float sigma, data device="cpu_box")
```

- clip: 32-bit float Gray (GrayS) only.
- sigma: Gaussian standard deviation in pixels; must be finite and non-negative. 0 returns the input unchanged.
- device: `"cpu_box"`, `"cpu_iir"`, `"cpu_ass"`, `"gpu"` or `"cuda"`, as for MSRCP/MSRCR.

## Testing and benchmarks

With the plugin installed into the current environment (`pip install .`):

```
pip install pytest numpy scipy
pytest
python bench/bench_blur.py
```

`meson test -C build` runs an exhaustive accuracy check of the vectorized float logarithm used by MSRCP/MSRCR (every positive normal float, about 20 seconds).

`bench/bench_blur.py` reports frames per second for `GaussBlur` per device and sigma, MSRCP per device and Guided_RCP (default 1920x1080; see `--help`). Results on 4 CPU threads (`python bench/bench_blur.py --threads 4 --frames 80 --repeats 5`; 1080p, fps):

| | sigma 5 | 25 | 80 | 250 |
|---|---:|---:|---:|---:|
| GaussBlur cpu_box | 754 | 737 | 703 | 658 |
| GaussBlur cpu_iir | 484 | 468 | 490 | 495 |
| GaussBlur cpu_ass | 1220 | 1617 | 1565 | 1524 |

| YUV444P16, defaults | fps |
|---|---:|
| MSRCP cpu_box | 141 |
| MSRCP cpu_iir | 113 |
| MSRCP cpu_ass | 206 |
| Guided_RCP | 273 |

Accuracy differs per device: `cpu_iir` matches a true Gaussian most closely in shape (its effective width is about 10% above `sigma`), `cpu_ass` is closest in width, and `cpu_box` treats very small sigma (below about 0.8) as no blur.
