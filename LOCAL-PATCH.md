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

Rebuild and install:

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -GNinja
    cmake --build build
    sudo cmake --install build --prefix /usr

The files in `/usr` belong to the `obs-backgroundremoval` package: a package upgrade or
`pacman -S obs-backgroundremoval` reverts them. Originals: `/tmp/obs-bgremoval-backup`.
