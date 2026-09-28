# Upscale material dithering

The native GPU rasterizer now transports the authored two-color pair for
untextured 3D faces. Bit 31 of its indexed pixel marks the pair, byte 0 retains
the original selected index, and byte 1 stores the alternate index. The actual
layer is 3D (zero); native readback must use `gpu_pixel_layer` rather than
interpreting byte 1 directly. Painter overwrites clear or replace the pair.

The scene merge preserves the marker. The final compositor resolves the two
palette RGB values when source render scale exceeds 1, including thin faces,
edges and pairs containing palette zero. It exports ordinary tags without the
marker to all post-processing. At 1x it retains original indexed colors. HUD,
textured art and CPU foreground writes do not inherit a hidden face's pair.
This transport adds no per-pixel buffer allocation.

Validation covers single-pixel-wide faces at 1x/2x/3x/4x, surface metadata,
scene merging, protected tags and foreground coverage on Vulkan and DX12.
The model regression also retains original indexed colors and layer tags.

Software rendering now carries optional per-pixel alternate indices from face
and line writes through ordinary layer composition, cached copies, and RGBA
expansion. Serial and parallel expansion resolve the same pair. Same-color HUD
writes, clear and resize invalidate provenance. GPU readback and CPU command
replay preserve authored pairs as well. Native 1x expansion remains unchanged.

Software gameplay captures at 2x and 4x (EX LEVEL1_1, AA and enhancements off)
were visually checked after connecting the material writers. Flat faces and
narrow edges no longer show the alternating checker pattern in these captures.
The 32-model/384-image indexed-render regression also passes after GPU readback
was updated to preserve the source pairs.

Recorded indexed-layer remapping carries the pair as an optional little-endian
snapshot alongside pixels and tags. CPU replay and GPU raster retain it through
resampling and mosaic, including palette-zero pixels. Regression coverage checks
2x and 4x against direct software composition. Native-scale layers do not allocate
this snapshot. Resident GPU indexed-layer remapping also preserves marked
palette-zero samples at higher scales, rather than turning half of a material
pair into transparent holes. Its regression covers 2x/4x with and without mosaic.
This is not a claim that every backend or all post-processing
combinations have been tested.
