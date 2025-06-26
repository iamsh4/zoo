# Basic polygons
`HOLLY` is the graphics/interface chip which adopts Power VR architecture.

## Shapes
Holly supports the following SHAPES
- Single Triangle
- Single Quad
- Triangle Strip

Note: The Z/U/V of the fourth vertex of a quad are inferred from the first 3 vertices.

## Types
Holly supports...
- (Non-Textured) + (Flat OR Gouraud Shaded)
- (Textured) + (Flat OR Gouraud Shaded) + (With/Without Offset Color)

So, every polygon can be textured or not, flat or gouraud shaded, and textured polygons can also have an offset color.

# Display Lists

There are two main `HOLLY` Graphics blocks: `TA` (Tile Accelerator), and `CORE`.

User code creates Polygon lists with "TA Parameters" which the TA then transforms into a CORE display list, which is used for actual drawing. CORE display lists are stored in texture memory.

Users prepare TA parameters are prepared by the code, but the user code must also prepare a portion of the CORE display list (via TA parameters). 

# Tile Partitioning and Rendering
HOLLY partitions the graphics screen (of up to 2048 x 2048), into 32 x 32 pixel regions, called "tiles". When a polygon is rendered, a determination of which tiles the polygon overlaps is made, and that polygon is stored in a list for that each of those tiles. User code is oblivious to all of this. When rendering is later done, each pixel within a tile sorts polygons to find the nearest polygon and then draws it. This means there is no z-buffer.

The `ISP` (Image Synthesis Processor) performs depth sorting in hardware for all triangles. All 1024 pixels in a tile are processed in parallel. Processing time scales with number of lines in one triangle. Processed pixels are compressed (for faster transfer in hardware) then sent to the `TSP` (Texture and Shading Processor). The TSP performs shading and texturing, and draws to a tile accumulation buffer. the buffer, when complete, is written to texture memory. The TSP performances perspective compensation for all texture and shading elements.

### Polygon Lists
There are five lists in HOLLY:
1. Opaque
2. Punch-through
3. Opaque Modifier
4. Translucent
5. Translucent Modifier Volume

The lists are drawn from (1) updwards.

### Per-Vertex Fog Handling
Per-vertex fog was very simple to implement. If FOG_CONTROL is set appropriately, you simply use the offset color alpha to get a fog coefficient and then use that to mix the fog color with the pre-fog color. Important to only mix the RGB components :)

### Look-up Table Fog Handling
Look-up table fog is much more obtuse. Section 3.4.8.1 gives some crucial details, but is still hard to follow. The process used for LUT fog is:

1. Take the depth for the current fragment, multiply by FOG_DENSITY, which applies some scaling before the next steps. The result of this scaling we'll call `s`.
2. Extract the high and low bytes of an 'address' into the fog data look-up table. The math here is not trivial: see next section. 
3. By using this address as a look-up into the fog data, you get the fog coefficient which is used to mix the pre-fog and fog colors, just like in per-vertex fog.

One important note for our implementation is that the actual fog data sent to Holly resides in 128 32-bit words. Only the bottom 16-bits (which is really two 8-bit fog coefficients kept side-by-side) is supposed to be meaningful. The stated behavior in the documentation is that the a given depth value will map to one of these 16-bit words, and then depending on some fractional part of the computed address would tell you how to interpolate between these two values. We observed that almost all games repeat the second value in one word as the first value in the following word, so we opted in Penguin to just keep one byte for each 32-bit word around. The interpolation then is automatically handled by bilinear interpolation when reading from the texture.

### Look-up Table Index Computation

First, the `FOG_DENSITY` register encodes a floating-point value in 1 8-bit (unsigned) mantissa, and 1 8-bit (signed) exponent. If the register is interpreted as the high byte `H` and low byte `L`, then the resulting floating point value is `(H / 128.0) * pow(2, signed(L))`. For example, 0xFF07 would be (0xFF/128) * 2^7 = 255.0 and 0x800e would be (0x80/128) * 2^14 = 16384.0.

The next crucial part is this equation given in section 3.4.8.1:

```1/W = ( pow(2.0, Index>>4) × ( (Index & 0xF)+16) / 16.0) ) / FogDensity ;```

This relates the FogDensity that we just calculated with 1/w. Note that '1/W' in the stated equation is = 1/vertex_position.z, where vertex_position.z is the Z value passed in from the software. In our shaders, 'w' is calculated to be the same value as what the document calls '1/W', so everywhere below where we say `w`, we really mean the vertex_position.z. 

Our goal at this point to compute 'Index' in their equation given that we know `FogDensity` and `w`. Here is a derivation to calculate "Index>>4", which we'll just call H for high nibble, and "Index & 0xf" which we'll call L for low nibble. When we combine these two (16*H + L), that will be our address into the fog table.

```
Original equation from the docs (note 'w' === original vertexPosition.z). For brevity, we'll call FogDensity FD.

  w      = pow(2, H) * (L + 16) / 16 / FD

  w * FD = pow(2, H) * (L + 16) / 16

Let FD*w = s. Then, applying log2 to both sides...

  log(s) = H + log2( (L + 16) / 16 )

         = H + log2( L/16 + 1 )

L can only be 0-15 because it's a single nibble. That means log2(...) can only be in the range log2(1) -> log2(31/16),
both of which are less than 1. This means we can take the floor of both sides, and because H is a whole number while log2(...)
will be some fraction <1.0, we have

  H = floor(log(s))

We can plug this back in to get L.

  s = pow(2, H) * (L + 16) / 16

  L = s * 16 / pow(2, H) - 16

Combining the two:

  adddress = H*16 + L
```

### Gotchas
- Soul Calibur's character select screen features a large portrait for the currently selected character. The portrait
  is rendered with palette texture, but the palette for a portrait is not loaded until after the portrait is submitted.
  You must be sure to render with the correct palette as the palette is not always correct when a texture is first
  utilized.
- Additionally on the same character select screen, a golden indicator around the current small character portrait in 
  the list utilizes palette swapping to animate the indicator. The palette is 10 colors being changed every frame, so 
  the emulator must correctly handle this. 