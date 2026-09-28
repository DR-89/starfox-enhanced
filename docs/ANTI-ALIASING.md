# Anti-aliasing

In 3D Options, **AA Type** selects the algorithm and **AA Quality** selects
OFF / LOW / MEDIUM / HIGH. Existing settings default to FXAA.

- **FXAA:** existing fast directional contrast filter.
- **Sharp Edge:** chooses the lower-curvature axis to preserve straight edges.
- **Soft Edge:** blends four neighboring samples at detected contrast edges.
- **SSAA:** renders at least 2×, 3× or 4× the native dimensions, then averages
  samples into native-resolution cells. These correspond to 4, 9 or 16 samples
  per source pixel. An independently selected higher Render Upscale is retained.
  iOS stays capped at 2×. SSAA is substantially more expensive than spatial AA.

- **TAA:** jittered native model samples, reprojected history and depth rejection.
  Low/Medium/High use 65%/80%/90% history weight. Requires the native GPU path
  and mono output; the menu identifies unavailable configurations.
- **SMAA:** upstream SMAA 1x color-edge detection, area/search lookup blending
  weights, and neighborhood blending. Low/Medium/High increase search length;
  High also handles diagonal patterns and corners. Requires the native GPU path.

- **MSAA:** 2/4/8 coverage samples for Low/Medium/High, with pixel-frequency
  material shading and retained samples across model draws. Requires native GPU
  mono output without DLSS/FSR1. HUD and legacy sprite layers retain their native
  pixel coverage. Polygon, line, model-sprite, wireframe, cel, wave, colour-warp
  and wobble coverage is implemented and verified on Vulkan and DirectX 12;
  see `MSAA-INTEGRATION.md` for validation and platform limits.

HUD/portrait layers are excluded from AA filtering. Unsupported
TAA/SMAA/MSAA configurations do not silently substitute another AA algorithm.

## TAA integration and validation

`STARFOX_TEST_TAA=1` forces the native GPU temporal resolve for diagnostic
captures. It uses jittered model samples, native
motion/depth, previous-camera depth rejection, neighbourhood history clipping,
and exact HUD restoration. It does not stack with DLSS or FSR1, and stereo
views are not enabled for this path yet.

The resolved history remains on its jittered sampling grid, but a separate
presentation reconstruction cancels the current projection offset. This avoids
exposing the sampling sequence as preview wobble without feeding the extra
filter back into history. Non-geometry regions and protected HUD pixels remain
unshifted. An alternating-offset stationary-ramp regression verifies this on
Vulkan and DX12; the EX main-menu preview also executes the corrected path.

`starfox_gpu_temporal_aa_check vulkan` and
`starfox_gpu_temporal_aa_check direct3d12` compare the GPU resolve against its
CPU reference for motion, jitter, disocclusion, camera depth, HUD/alpha,
resizing, scene resets and cancellation. An in-game capture also confirms
resident execution. Original and EX intro geometry and EX gameplay captures
have been visually checked, including thin moving ship edges. This does not
establish validation on every device or in every stage.

The same GPU checker verifies SMAA's three quality levels, flat-color stability,
diagonal smoothing, HUD/alpha protection and intermediate-buffer clearing on
resize and reuse. The composition checker also compares SMAA's resident input
against its uploaded-input path.
