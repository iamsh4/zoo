
# Overview
- All register updates performed instantly on host, ranges are batched for eventual GPU upload
- All VRAM writes performed instantly on host, ranges batched for eventual GPU upload
- TA FIFO operations
    1. Flush pending host GPU writes
    2. 

# Required Functionality
- Data Operations (enqueued)
    - GPU Register writes (including Fog + Palettes)
    - TA FIFO Direct Texture
- Rendering-related
    - TA FIFO Geometry
    - TA FIFO YUV Conversion
    - STARTRENDER
        - Iterate Region Array Entries, enqueue compute work per region array entry
- CPU R/W
    -  PVR-IF (Forces host-guest VRAM synchronization!)
- Host Display
    - FB_R_CTRL + FB_R_SOF1/2 - describe framebuffer view area -> Copy to surface Image/Texture

# Implementation
- Storage Buffer for Registers
- Storage Buffer for guest VRAM
- TA Direct Texture Upload
    1. Enqueue copy operation
- TA_LIST_INIT
    1. Enqueue Copy of internal/register state so that future binning operations will work. (probably clear tile state, etc.)
- TA Binning
    1. Enqueue Copy TA FIFO work to back of work list
    2. Enqueue TA Binning (one tile per host gpu thread)
    3. Host-side blocking on completion
- STARTRENDER
    1. Set render pipeline
    2. Enqueue single tile work
        - Clear to BACKGROUND
        - 
    3. (Repeat for all region array entries)


# TA 
```
- Iterate over queued geometry
- Find overlapping tiles
- Update OPBs in each tile
```

# Steps for rendering
1. TA binning
2. For Each Region Array Entry RA[i]
3. DoEntry( RA[i] )


# Dispatch modes
1. Sequential, one-at-a-time (slowest, no possibility to miss an edge case)
2. Parallel, operate on as many spatially-distinct tiles as possible at once, run each pass in sequence. (fast, but misses any case where the results of a tile rendered somewhere on screen has its results used for another place elsewhere on another part of the screen)

