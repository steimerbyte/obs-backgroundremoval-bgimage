# Local patch: background image

On top of upstream 1.4.1: the filter can composite a still image behind the person.

- `src/background-filter.cpp`: `background_image` path property plus placement controls
  (`background_image_scale` 0.2–2.0, `background_image_offset_x/y` −1.0–1.0 as fractions of the
  frame). The image is loaded with OpenCV and rendered into a frame-sized texture once per
  change (`scale 1.0` = cover, smaller = zoom out with empty edges), then used instead of the blur.
- `data/effects/mask_alpha_filter.effect`: new `DrawWithImage` technique, using the existing
  (previously unused) `PSAlphaMaskRGBAWithBlur` pixel shader.
- `CMakeLists.txt`: link `opencv_imgcodecs`.
- `data/locale/en-US.ini`: `BackgroundImage` label.

## Local patch: edge feather and edge blur

Two extra sliders for the background image, both default 0.0 (0.0 = the old behaviour, nothing
runs):

- `background_edge_feather` ("Edge Feather"): after the threshold/erode/dilate/feather chain the
  full-resolution mask is box-filtered with a radius of
  `clamp(round(feather * 0.03 * maskRows), 1, 40)` pixels, so the person fades into the image
  instead of ending in a hard contour. The filter runs on the CPU in `video_tick` and only when
  `enableThreshold` is on. The result goes into the extra `maskFeather` scratch Mat, because
  `cv::boxFilter` must not write into its own input.
- `background_edge_blur` ("Edge Blur (camera)"): when a background image is used, the camera is
  kawase-blurred with `2 + round(value * 6)` passes and `DrawWithImageEdge` mixes it in with
  `smoothstep(0.0, 0.5, mask) * value`: sharp in the solid person (`mask = 0`), fully blurred at
  the silhouette (`mask >= 0.5`), so the seam where the camera is 50 % visible carries no sharp
  room detail. Without the feather the mask is binary and this slider has no visible effect.

`data/effects/mask_alpha_filter.effect` gained `uniform texture2d cameraBlur`, `uniform float
edgeBlur`, `PSAlphaMaskRGBAWithBlurEdge` and the `DrawWithImageEdge` technique; the texture and
the float are only set when the technique is used.

## Local patch: logo layer

`logo_image` (transparent PNG/WebP), `logo_scale` (0.01..2.0 of the logo's own pixels) and
`logo_offset_x`/`logo_offset_y` (-1..1 of the frame, same convention as the background image)
composite a logo over the finished frame. The logo is blended inside `mask_alpha_filter.effect`
(`ApplyLogo` in every pixel shader, straight alpha "over"), so it survives all four techniques and
does not depend on filter draw order. With no logo set the output is pixel-identical to before
(measured: 0 differing pixels of 2,073,600 on a 1920x1080 frame).

Measured on the isolated harness (1920x1080, still source, logo 400x200 with an opaque red half and
a 50 % blue half, scale 0.5, offset 0.30/-0.40): the red half lands at x 1436..1535, y 58..157
(10,000 px, exactly the predicted rect), the 50 % half composites to (127,0,255) over a magenta
background, i.e. 0.5 * blue + 0.5 * background. Verified in the background-image, blur and
transparent techniques.

## How far the cutout sits from the silhouette (measured)

Threshold 0.5, no expansion, no feather, no contour filter = the model's own sub-pixel edge = the
reference. Every setting below was measured against that reference on the same frame
(`mask_expansion` is reported by the plugin as "Mask Expansion", the log line used %f on an int and
always printed 0 - now %d):

| setting | cutout moves | area | measured on |
|---|---|---|---|
| `mask_expansion 30` | 18.3 px outward (p90 35.7) | +94,808 px | 1920x1080 still frame |
| `threshold 1.0` | 3.7 px inward (p90 9.0) | -25,989 px | same |
| `feather 1.0` | 10.3 px inward (p90 18.7) | -45,464 px | same |
| `contour_filter 0.25` | 14.3 px outward (p90 41.0) plus 1.0 px inward | +13,277 / -1,684 px | same |

So a ring of the real room around the person is mostly `mask_expansion` (positive dilates, contrary
to the slider's help text) and `contour_filter`; `threshold 1.0` and a large `feather` instead eat
into the person.

## Local patch: sub-pixel mask edge

With a threshold the mask used to become binary at the model resolution (256x144 for MediaPipe) and
was then resized to the frame, so the silhouette carried the model's 7.5 px grid as a blocky
staircase and its transition band showed the real room at up to 50 %.

Now the probability stays intact (`255 - probability` in `maskScratch`) and only the full-resolution
upscale is thresholded: `cv::resize(..., INTER_CUBIC)` followed by `compare(mask, 255 - thr*255,
CMP_GT)`. The edge follows the 0.5 crossing of the interpolated probability instead of the
model grid. Measured at 1920x1080, MediaPipe, threshold 0.5, injected 256x144 test mask on a
person frame: transition band (pixels with 0.1 < camera share < 0.9) 25,492 -> 4,087 px (-84 %),
person area 380,518 -> 377,692 px, cost +1.24 ms per tick (bilinear 0.18 ms -> cubic 1.42 ms,
measured standalone with OpenCV 5.0.0 on this machine).

The contour filter (`contour_filter` between 0 and 1) rewrites the mask as filled contours, which is
binary by nature; with it enabled the old mask-resolution path stays and the log says so in
`Mask Edge:`.

## Performance (same output)

Per frame the filter used to allocate and copy several full frames on the graphics thread and
reloaded both shaders on every settings change. Now:

- The stage surface is copied into a reused staging buffer and swapped into `inputBGRA`; the tick
  swaps it into its own buffer, so the two 1920x1080 allocations per frame are gone.
- The model input is resized to the network size *before* BGRA->RGB. Resize is per channel, so
  the result is bit-identical, but the full-resolution conversion and its buffer are gone; the
  inference Mats are reused (`ORTModelData`).
- The mask is built in reused scratch Mats (`compare`/`subtract`/`resize` instead of MatExprs that
  allocate), and `cv::split` in the MediaPipe postprocess became `cv::extractChannel`.
- The alpha and blur textures are created once per frame size and updated with
  `gs_texture_set_image` / `gs_copy_texture` instead of create+destroy each frame.
- Both effects are loaded once; a slider change no longer destroys and recompiles them.
- ORT input/output name pointers are cached per session instead of rebuilt every frame.

`numThreads` still defaults to 1, as upstream: 1/2/4 threads produce bit-identical tensors, but
end to end on a loaded CPU 4 threads were slower (25.5 ms vs 23.7 ms frame time) and used four
times the CPU time, so there is nothing to gain by raising the default.

Rebuild and install:

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -GNinja
    cmake --build build
    sudo cmake --install build --prefix /usr

The files in `/usr` belong to the `obs-backgroundremoval` package: a package upgrade or
`pacman -S obs-backgroundremoval` reverts them. Originals: `/tmp/obs-bgremoval-backup`.
