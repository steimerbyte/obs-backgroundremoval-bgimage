# Local patch: background image

On top of upstream 1.4.1: the filter can composite a still image behind the person.

- `src/background-filter.cpp`: `background_image` path property. The image is loaded with
  OpenCV, cover-fitted to the frame size once per size change, and used instead of the blur.
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
