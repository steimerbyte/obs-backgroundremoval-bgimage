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
