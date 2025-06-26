# Penguin Project Status

This file contains a general overview of the state of the project, with ideas
for the general direction it's going and major tasks that are either in progress
or planned.

## Completed
- Sprite Support
- Handle mipmap+twiddle textures
- Fix UV issue on powerstone gems
- MSAA rendering

## Work-in-Progress

* JIT backend for SH4a to amd64
* Implementing additional rendering primitives, texturing modes, and other PowerVR-related improvements.
  * Fog

## Planned Features
### Work starting soon

* Performance Counters / GUI Reporting
  * Host and Guest FPS
  * CPU instructions/sec, %interpreted vs %native
  * GDROM 'Loading' indicator / icon
- VMUs!
* Finish the APU emulation
  * Currently, the APU is using an ARM interpreter pulled from reicast (GPL).
    This needs to be replaced with a penguin native implementation that has
    an IR backend to take advantage of our JIT.
  * The audio hardware itself isn't implemented at all. No channel mixing
    or time tracking.
  * Once that's done, it still needs to be hooked up to output audio on the
    host system..
* Stateless Rendering
  * Currently, there is a RenderQueue object which separates draw commands from the Tile Accelerator from our current backend (OpenGL 3.3). Want to get RenderQueue to be fully stateless and decoupled so that it encapsulates everything needed to render a frame. From this, we can use different backends depending on platform, or even create standalone viewers.
* Ability to save and restore emulator state from a file
* Graphics
  * Bump Mapped Textures
  * Palette4/8 color formats
  * YUV Texture Transfers
  * Auto-sort triggers polygon-level sorting (not per-pixel correct, but probably a big improvement)

### Longer term

* Support for RPi, with an ARM JIT backend
* Graphics
  * Modifier Volumes
  * Order-Independent Transparency (effectively pixel-level sorting)

### Known Issues
- Audio looping in many games

### Fixed Issues (Previously in Known Issues)

- BIOS boot animation flickers. This is strongly believed to be a timing-related issue between DMA and STARTRENDER. If DMAWrite is started to the TA and then STARTRENDER is triggered immediately after while the DMA is still taking place in another thread, then there is a race to which portions of the DMA will get pulled into rendering. Suggestion: Lock while a PVR DMA is taking place before rendering starts.
  - Solved. This was because we didn't properly support TA_ISP_BASE / PARAM_BASE for double buffered display lists.
