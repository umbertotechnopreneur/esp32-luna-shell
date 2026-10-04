# Software raster implementation

RGB565 raster, path, bounded ASCII text and control primitives. Public contracts
remain in `../include/`; the component root README describes their ownership.

Surfaces, allocations, clocks, touch and panel transfers stay caller-owned. This
folder contains no LVGL, scheduler, hardware driver or generated artwork.
