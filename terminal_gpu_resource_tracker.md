# Terminal GPU Resource Tracker

## Objective

Move graphics paths toward a Windows-efficient model where persistent assets and pane front/back surfaces remain GPU-resident, CPU uploads happen only on load or mutation, and shared buffer flipping stays synchronized across pane types.

## Constraints

- Keep Win32 and D3D11 ownership on the UI thread.
- Preserve the shared active/background buffer protocol already added to `SuperTerminal`.
- Avoid per-frame CPU-retained graphics copies for RGBA and indexed panes.
- Add a general GPU blit path for copy and composition work.
- Keep each phase independently testable and small enough for one work session.

## Phases

### Phase 1: Low-Level Resource Contract

Status: in progress

Goal:
Define the low-level GPU resource API in `wingui` so higher layers can target resident textures, region uploads, and GPU blits without inventing a second ownership model.

Deliverables:
- Define opaque handles or typed ids for resident RGBA textures and blit-capable surfaces.
- Define upload APIs for full texture and sub-rectangle updates.
- Define GPU copy/blit APIs for same-format rect copies and shader-driven blits.
- Define the minimum metadata needed for buffer count, dimensions, and format assumptions.
- Keep the first API slice small enough to support RGBA panes before broadening to indexed surfaces.

Exit criteria:
- `include/wingui/wingui.h` exposes a stable first-pass API for resident RGBA resources and GPU blits.
- The new API compiles cleanly without changing runtime behavior yet.

### Phase 2: RGBA Pane Direct-to-GPU Path

Status: in progress

Goal:
Remove the extra retained CPU copy from `SuperTerminal` RGBA panes by writing directly into leased GPU back buffers.

Deliverables:
- Add per-pane RGBA surface objects in `wingui` so multiple panes can own independent GPU-resident buffer sets while sharing one draw pipeline.
- Replace CPU-retained RGBA pane storage in `src/terminal.cpp` with GPU-resident pane buffers.
- Route `super_terminal_frame_rgba_upload(...)` to the leased back buffer on the GPU.
- Add optional region upload support for partial updates.

Exit criteria:
- RGBA panes no longer require host-retained CPU frame blobs for steady-state rendering.
- Existing buffer lease semantics remain intact.

### Phase 3: General GPU Blit Utilities

Status: pending

Goal:
Add reusable GPU copy and shader blit primitives for pane updates, scrolling, and composition.

Deliverables:
- Add same-format rect copy support.
- Add shader blit support for scale, tint, and alpha blend.
- Add a simple path for copying resident textures into pane back buffers.

Exit criteria:
- Pane update code can perform common copy/composite operations without round-tripping through CPU memory.

### Phase 4: Indexed Pane GPU-Resident Backing

Status: pending

Goal:
Move indexed pane backing from CPU-retained blobs to GPU-resident per-buffer resources.

Deliverables:
- Add indexed pixel and palette resource update paths.
- Keep sprite atlas and sprite palettes resident on the GPU.
- Render indexed panes from GPU-resident backing tied to the shared buffer ids.

Exit criteria:
- Indexed panes no longer depend on host-retained CPU frame storage in the steady state.

### Phase 5: Dirty Regions and Metrics

Status: pending

Goal:
Reduce bandwidth and make costs visible.

Deliverables:
- Dirty-rect upload support for RGBA and indexed panes.
- Metrics for uploaded bytes, upload count, blit count, and surface reallocations.
- Lightweight profiling hooks for mixed-pane scenarios.

Exit criteria:
- We can measure whether 60 fps mixed-pane updates stay within acceptable bandwidth.

### Phase 6: End-to-End Sample and Validation

Status: pending

Goal:
Validate the model with a realistic hosted app that uses mixed pane types and resident assets.

Deliverables:
- Add a small hosted example using text, indexed, and RGBA panes.
- Exercise resident assets, region uploads, and GPU blits.
- Document performance observations and remaining bottlenecks.

Exit criteria:
- The runtime has one realistic mixed-pane sample proving the intended resource model.

## Current Step

Current step: Phase 1, API design only.

Bounded scope for the next session chunk:
- Wire `SuperTerminal` RGBA panes to per-pane `WinguiRgbaSurface` objects.
- Remove host-retained CPU RGBA frame blobs from the runtime.
- Compile after the runtime slice lands.

## Notes

- The current shared buffer flip model is correct and should remain the coordination mechanism.
- The current graphics-pane CPU-retained storage is transitional and should be retired phase by phase.
- Indexed asset upload support already exists for sprite atlases; extend that direction rather than introducing a parallel model.
- The shared `WinguiRgbaPaneRenderer` buffer set is not sufficient for multi-pane residency; per-pane `WinguiRgbaSurface` objects now exist as the low-level prerequisite.