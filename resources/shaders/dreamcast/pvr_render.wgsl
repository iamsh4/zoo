
const VRAM_SIZE: u32 = 8u * 1024u * 1024u;
const VRAM_MASK: u32 = VRAM_SIZE - 1u;

const kInvalidTag: u32 = 0xffffffffu;

// All registers are 32-bit and offset from 0x005f8000
const PARAM_BASE: u32 = 0x0020;
const REGION_BASE: u32 = 0x002c;
const FB_W_CTRL: u32 = 0x0048;
const FB_W_LINESTRIDE : u32 = 0x004c;
const FB_W_SOF1: u32 = 0x0060;
const FB_W_SOF2: u32 = 0x0064;
const ISP_BACKGND_D: u32 = 0x0088;
const ISP_BACKGND_T: u32 = 0x008c;

struct RegionArrayEntry {
  header : u32,
  opaque : u32,
  opaque_mod : u32,
  translucent : u32,
  translucent_mod : u32,
  punchthrough : u32,
};

struct PixelState {
  color : vec4<f32>,
  isp_tag : u32,
  depth : f32,
  u : f32,
  v : f32,
}

/* Dreamcast VRAM, 32-bit region addressing */
@group(0) @binding(0) var<storage,read_write> vram: array<u32>;

/* Dreamcast PVR registers, based at 0x005f8000 */
@group(0) @binding(1) var<storage,read> regs: array<u32>;

/* Dispatch details for the workgroup */
@group(0) @binding(2) var<storage,read> dispatch_details: array<u32>;

/* While the real GPU only has internal state for a single tile, we support processing
 * multiple tiles in parallel. The PixelState array is used to store the internal state
 * for all possible tiles on the screen. Indexed with the tiles coordinates (in units of
 * 32 pixels). Indexed as [y * 40 + x] as the PVR in the DC supports a maximum of 600
 * tiles arranged as 40x15 tiles. */
@group(0) @binding(3) var<storage,read_write> p_state: array<PixelState>;

/* Pixel state index */
var<private> p_index : u32;

/* Current dispatch location within the tile [0,32] x [0,32] */
var<private> tile_ij : vec2<u32>;

/* Current tile position on screen in multiples of 32x32 */
var<workgroup> tile_pos : vec2<u32>;

var<workgroup> param_base : u32;

fn reg_read(offset: u32) -> u32 {
  return regs[offset >> 2];
}

/* Read a 32-bit value from VRAM, 32-bit region, bottom two address bits are ignored. */
fn vram32_read32(offset: u32) -> u32 {
  return vram[(offset & VRAM_MASK) >> 2];
}

/* Write a 32-bit value from VRAM, 32-bit region, bottom two address bits are ignored. */
fn vram32_write32(offset: u32, value: u32) {
  vram[(offset & VRAM_MASK) >> 2] = value;
}

/* Read a Region Array entry from VRAM */
fn read_region_array_entry(offset: u32) -> RegionArrayEntry {
  return RegionArrayEntry(
    vram32_read32(offset + 0),
    vram32_read32(offset + 4),
    vram32_read32(offset + 8),
    vram32_read32(offset + 12),
    vram32_read32(offset + 16),
    vram32_read32(offset + 20),
  );
}

struct Fragment {
  isp_tag : u32,
  u : f32,
  v : f32,
  depth : f32,
}

fn tsp_shade_opaque() {
  // TODO : Implement the shading pipeline
  if (p_state[p_index].isp_tag == kInvalidTag) {
    return;
  }

  let u = p_state[p_index].u;
  let v = p_state[p_index].v;
  p_state[p_index].color = vec4<f32>(u, v, 0.0, 1.0);
}

/* This is the typical cross(r1,r2).z, but negated so that clockwise rotations give
 * a positive value. This is necessary because polygon coordinates are given with X/Y
 * at the top left and extending right/down which is a left-handed coordinate system. */
fn area_signed(r1: vec2<f32>, r2: vec2<f32>) -> f32 {
  let left_handed_correction = -1.0;
  return (r1.x * r2.y - r1.y * r2.x) * left_handed_correction;
}

fn rasterize(isp_tag: u32, p: vec2<f32>, r1: vec3<f32> , r2: vec3<f32>, r3: vec3<f32>) -> Fragment {
  var r23 = r3.xy - r2.xy;
  var r21 = r2.xy - r1.xy;

  var area = area_signed(r23, r21);
  if (abs(area) < 0.00001) { // XXX : use configred clipping info
    return Fragment( kInvalidTag, 0.0, 0.0, 0.0 );
  }

  var r31 = r1.xy - r3.xy;
  var r32 = r2.xy - r3.xy;
  var r3p = p     - r3.xy;

  let area_inverse = 1.0 / abs(area);
  var u = area_signed(r3p, r32) * area_inverse;
  var v = area_signed(r31, r3p) * area_inverse;

  if (u < 0.0 || v < 0.0 || (u + v) > 1.0) {
    return Fragment( kInvalidTag, 0.0, 0.0, 0.0 );
  }

  let depth = r1.z + u * (r2.z - r1.z) + v * (r3.z - r1.z);
  return Fragment( isp_tag, u, v, depth );
}

///////////////////////////////////

const OBJ_ITER_UNINITIALIZED: u32 = 0;
const OBJ_ITER_TRIANGLE_STRIP: u32 = 1;
const OBJ_ITER_TRIANGLE_ARRAY: u32 = 2;
const OBJ_ITER_END_OF_LIST: u32 = 3;

var<workgroup> iter_state       : u32;
var<workgroup> iter_obj_address : u32; // pointer to current object
var<workgroup> iter_obj         : u32;
var<workgroup> iter_obj_index   : u32;

var<workgroup> tri_valid    : bool;
var<workgroup> isp          : u32;
var<workgroup> isp_tsp      : u32;
var<workgroup> tex          : u32;
var<workgroup> verts        : array<vec3<f32>, 3>;
var<workgroup> tex_uv       : array<vec2<f32>, 3>;
var<workgroup> base_color   : array<vec4<f32>, 3>;
var<workgroup> offset_color : array<vec4<f32>, 3>;

fn is_tri_strip (obj : u32) -> bool { return (obj >> 31u) == 0x0u; }
fn is_tri_array (obj : u32) -> bool { return (obj >> 29u) == 0x4u; }
fn is_block_link(obj : u32) -> bool { return (obj >> 29u) == 0x7u; }
fn is_eol_obj   (obj : u32) -> bool { return (obj >> 28u) == 0xfu; }

fn is_lead_invocation() -> bool {
  return tile_ij.x == 0u && tile_ij.y == 0u;
}

fn solo_obj_iterator_init(opb_address_init : u32) {
  iter_state = OBJ_ITER_UNINITIALIZED;

  /* Phase 1: Follow any initial block link pointers until we find the first
   *          object or the end of the list */
  iter_obj_address = opb_address_init;
  iter_obj         = vram32_read32(iter_obj_address);

  while (is_block_link(iter_obj)) {
    if (is_eol_obj(iter_obj)) {
      iter_state = OBJ_ITER_END_OF_LIST;
      return;
    }

    // Follow the link to the next opb/object
    iter_obj_address = iter_obj & 0x00fffffcu;
    iter_obj = vram32_read32(iter_obj_address);
  }

  /* Phase 2: Determine the type of the first object */
  if (is_tri_strip(iter_obj)) {
    iter_state = OBJ_ITER_TRIANGLE_STRIP;
  } else if (is_tri_array(iter_obj)) {
    iter_state = OBJ_ITER_TRIANGLE_ARRAY;
  } else {
    // XXX : Handle Quad case
    iter_state = OBJ_ITER_END_OF_LIST;
  }

  iter_obj_index = 0u;
}

fn solo_obj_iterator_next() {
  if (is_tri_strip(iter_obj)) {
    iter_obj_index = iter_obj_index + 1u;
    if (iter_obj_index == 6u) { // End of strip object
      iter_obj_address = iter_obj_address + 4u;
      iter_obj = vram32_read32(iter_obj_address);
      iter_obj_index = 0u;
    }
  } else if (is_tri_array(iter_obj)) {
    // XXX 
    return;
  } else {
    // XXX : Handle Quad case
    return;
  }

  // Need to follow any block links at this point
  if (is_block_link(iter_obj)) {
    if (is_eol_obj(iter_obj)) {
      iter_state = OBJ_ITER_END_OF_LIST;
      return;
    }

    iter_obj_address = iter_obj & 0x00fffffcu;
    iter_obj = vram32_read32(iter_obj_address);
  }
}

fn solo_read_obj_triangle_strip() -> bool {

  // Check if this indexed triangle is present in the strip
  let tri_strip_bit = 30u - iter_obj_index;
  let present = (iter_obj >> tri_strip_bit) & 1u;
  if (present == 0) {
    return false;
  }

  // ISP TSP TEX X Y Z (...) X Y Z (...) X Y Z (...)

  // Read the vertices
  let param_offset = iter_obj & 0x001ffffcu;
  let isp     = vram32_read32(param_base + param_offset + 0u);
  let isp_tsp = vram32_read32(param_base + param_offset + 4u);
  let tex     = vram32_read32(param_base + param_offset + 8u);

  let skip = (iter_obj >> 21u) & 7;
  var vertex_data = param_base + param_offset + 12u;
  verts[0].x = bitcast<f32>(vram32_read32(vertex_data + 0u));
  verts[0].y = bitcast<f32>(vram32_read32(vertex_data + 4u));
  verts[0].z = bitcast<f32>(vram32_read32(vertex_data + 8u));

  vertex_data = vertex_data + (3u + skip) * 4u;
  verts[1].x = bitcast<f32>(vram32_read32(vertex_data + 0u));
  verts[1].y = bitcast<f32>(vram32_read32(vertex_data + 4u));
  verts[1].z = bitcast<f32>(vram32_read32(vertex_data + 8u));

  vertex_data = vertex_data + (3u + skip) * 4u;
  verts[2].x = bitcast<f32>(vram32_read32(vertex_data + 0u));
  verts[2].y = bitcast<f32>(vram32_read32(vertex_data + 4u));
  verts[2].z = bitcast<f32>(vram32_read32(vertex_data + 8u));

  // Triangle strip triangles alternate winding order, so need to correct for this
  if (iter_obj_index % 2u == 1u) {
    let temp = verts[1];
    verts[1] = verts[2];
    verts[2] = temp;
  }

  // TODO
  return true;
}

fn solo_read_obj_triangle_array() -> bool {
  // TODO
  return false;
}

fn tile_draw_opaque(opb_start_address : u32) {

  /* Initialize the iterator over OPBs/Objects */
  if (is_lead_invocation()) {
    solo_obj_iterator_init(opb_start_address);
  }
  workgroupBarrier();

  /* Iterate over all triangles */
  var closest = Fragment(p_state[p_index].isp_tag, 0.0, 0.0, p_state[p_index].depth);

  // let pix_xy = vec2<f32>(f32(tile_pos.x * 32.0, tile_pos.y * 32.0) + vec2<f32>(f32(tile_ij.x), f32(tile_ij.y));
  let pix_xy = vec2<f32>(
    f32(tile_pos.x * 32u + tile_ij.x),
    f32(tile_pos.y * 32u + tile_ij.y)
  );

  // TODO : Load geometry from OPB / VRAM
  for (var i = 0; i < 4096 && !is_eol_obj(iter_obj); i = i + 1)
  {
    /* Read the currently pointed-to object */
    if (is_lead_invocation()) {
      if (is_tri_strip(iter_obj)) {
        tri_valid = solo_read_obj_triangle_strip();
      } else if (is_tri_array(iter_obj)) {
        tri_valid = solo_read_obj_triangle_array();
      }
    }
    workgroupBarrier();

    if (tri_valid) {
      let isp_tag = iter_obj & 0x1ffffcu;
      let fragment = rasterize(isp_tag, pix_xy, verts[0], verts[1], verts[2]); // TODO move verts to workgroup scope
      
      if (fragment.isp_tag != kInvalidTag && fragment.depth < closest.depth) {
        closest = fragment;
      }
    }

    /* Read the next object */
    if (is_lead_invocation()) {
      solo_obj_iterator_next();
    }
    workgroupBarrier();
  }

  /* Update tile state */
  p_state[p_index].depth   = closest.depth;
  p_state[p_index].u       = closest.u;
  p_state[p_index].v       = closest.v;
  p_state[p_index].isp_tag = closest.isp_tag;
  tsp_shade_opaque();
}

fn tile_z_clear() {
  p_state[p_index].isp_tag = kInvalidTag;
  // p_state[p_index].isp_tag = reg_read(ISP_BACKGND_T);

  // TODO : Shade background via background tag
  // TODO : Does z clear + empty lists still actually shade the background? Or does it only set the opaque list?

  p_state[p_index].color = vec4<f32>(0.0, 0.0, 0.0, 1.0);
  if ((tile_pos.x + tile_pos.y) % 2 == 0) {
    p_state[p_index].color = vec4<f32>(1.0, 1.0, 1.0, 1.0);
  }
  p_state[p_index].depth = 100.0; // XXX bitcast<f32>(reg_read(ISP_BACKGND_D));
}

// These pack functions pack pixel data into the framebuffer format
// Note that colors are permuted so that they appear in the correct
// order when written to memory in little-endian format.

fn pack_color_krgb0555(color: vec4<f32>) -> u32 {
  let r = u32(clamp(color.r * 31.0, 0.0, 31.0));
  let g = u32(clamp(color.g * 31.0, 0.0, 31.0));
  let b = u32(clamp(color.b * 31.0, 0.0, 31.0));

  return (b << 10) | (g << 5) | r;
}

fn pack_color_rgb565(color: vec4<f32>) -> u32 {
  let r = u32(clamp(color.r * 31.0, 0.0, 31.0));
  let g = u32(clamp(color.g * 63.0, 0.0, 63.0));
  let b = u32(clamp(color.b * 31.0, 0.0, 31.0));

  return (b << 11) | (g << 5) | r;
}

fn pack_color_argb4444(color: vec4<f32>) -> u32 {
  let r = u32(clamp(color.r * 15.0, 0.0, 15.0));
  let g = u32(clamp(color.g * 15.0, 0.0, 15.0));
  let b = u32(clamp(color.b * 15.0, 0.0, 15.0));
  let a = u32(clamp(color.a * 15.0, 0.0, 15.0));

  return (a << 12) | (r << 8) | (g << 4) | b;
}

fn pack_color_argb1555(color: vec4<f32>) -> u32 {
  let r = u32(clamp(color.r * 31.0, 0.0, 31.0));
  let g = u32(clamp(color.g * 31.0, 0.0, 31.0));
  let b = u32(clamp(color.b * 31.0, 0.0, 31.0));
  let a = u32(clamp(color.a * 1.0, 0.0, 1.0));

  return (a << 15) | (r << 10) | (g << 5) | b;
}

fn pack_color_rgb888(color: vec4<f32>) -> u32 {
  let r = u32(clamp(color.r * 255.0, 0.0, 255.0));
  let g = u32(clamp(color.g * 255.0, 0.0, 255.0));
  let b = u32(clamp(color.b * 255.0, 0.0, 255.0));

  // return (r << 16) | (g << 8) | b;
  return (b << 16) | (g << 8) | r;
}

fn pack_color_krgb0888(color: vec4<f32>) -> u32 {
  let r = u32(clamp(color.r * 255.0, 0.0, 255.0));
  let g = u32(clamp(color.g * 255.0, 0.0, 255.0));
  let b = u32(clamp(color.b * 255.0, 0.0, 255.0));
  let K = 0xffu; // TODO

  return (K << 24) | (r << 16) | (g << 8) | b;
}

fn pack_color_argb8888(color: vec4<f32>) -> u32 {
  let r = u32(clamp(color.r * 255.0, 0.0, 255.0));
  let g = u32(clamp(color.g * 255.0, 0.0, 255.0));
  let b = u32(clamp(color.b * 255.0, 0.0, 255.0));
  let a = u32(clamp(color.a * 255.0, 0.0, 255.0));

  return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Flush the pixel state for two adjacent pixels to the framebuffer */
fn tile_flush_to_framebuffer() {

  // TODO : This is quite a lot of code just to resolve all the different framebuffer
  //        formats into guest VRAM. If this ends up being super expensive, this could
  //        be a deferred operation that is only done if a guest framebuffer read is
  //        detected. Otherwise we could just write to a host-native surface/texture
  //        for display.

  let pack_mode = reg_read(FB_W_CTRL) & 0x7u;
  let line_stride_bytes = reg_read(FB_W_LINESTRIDE) * 8u;

  /* Because we can only perform 4-byte wide stores atomically, and need to support
   * 16-bit, 24-bit, and 32-bit framebuffer formats, one in every four pixels will
   * do the work of writing the 4 pixels to the framebuffer. */
  if ((tile_ij.x % 4u) == 0) {

    if (pack_mode == 1) {
      // 565
      let addr = (reg_read(FB_W_SOF1) & 0xfffffffc) +
        (tile_pos.y * 32u + tile_ij.y) * line_stride_bytes +
        (tile_pos.x * 32u + tile_ij.x) * 2u;

      let packed_0 = pack_color_rgb565(p_state[p_index    ].color);
      let packed_1 = pack_color_rgb565(p_state[p_index + 1].color);
      let packed_2 = pack_color_rgb565(p_state[p_index + 2].color);
      let packed_3 = pack_color_rgb565(p_state[p_index + 3].color);

      let packed_01 : u32 = (packed_1 << 16) | packed_0;
      let packed_23 : u32 = (packed_3 << 16) | packed_2;

      vram32_write32(addr    , packed_01);
      vram32_write32(addr + 4, packed_23);

    } else if (pack_mode == 3) {

      let addr = (reg_read(FB_W_SOF1) &  0xfffffffc) +
        (tile_pos.y * 32u + tile_ij.y) * line_stride_bytes +
        (tile_pos.x * 32u + tile_ij.x) * 2u;

      let packed_0 = pack_color_argb1555(p_state[p_index    ].color);
      let packed_1 = pack_color_argb1555(p_state[p_index + 1].color);
      let packed_2 = pack_color_argb1555(p_state[p_index + 2].color);
      let packed_3 = pack_color_argb1555(p_state[p_index + 3].color);
      
      let packed_01 : u32 = (packed_1 << 16) | packed_0;
      let packed_23 : u32 = (packed_3 << 16) | packed_2;

      vram32_write32(addr    , packed_01);
      vram32_write32(addr + 4, packed_23);

    } else if (pack_mode == 4) {

      // Pack mode rgb888
      let addr = (reg_read(FB_W_SOF1) & 0xfffffffc) +
        (tile_pos.y * 32u + tile_ij.y) * line_stride_bytes +
        (tile_pos.x * 32u + tile_ij.x) * 3u;
      
      let packed_0 = pack_color_rgb888(p_state[p_index    ].color);
      let packed_1 = pack_color_rgb888(p_state[p_index + 1].color);
      let packed_2 = pack_color_rgb888(p_state[p_index + 2].color);
      let packed_3 = pack_color_rgb888(p_state[p_index + 3].color);

      let packed_01 : u32 = (packed_1 << 24) | packed_0;
      let packed_12 : u32 = (packed_2 << 16) | (packed_1 >> 8);
      let packed_23 : u32 = (packed_3 << 8) | (packed_2 >> 16);

      vram32_write32(addr    , packed_01);
      vram32_write32(addr + 4, packed_12);
      vram32_write32(addr + 8, packed_23);      

    } else if (pack_mode == 5) {

      // Pack mode krgb0888
      let addr = (reg_read(FB_W_SOF1) &  0xfffffffc) +
        (tile_pos.y * 32u + tile_ij.y) * line_stride_bytes +
        (tile_pos.x * 32u + tile_ij.x) * 4u;

      let packed_0 = pack_color_krgb0888(p_state[p_index    ].color);
      let packed_1 = pack_color_krgb0888(p_state[p_index + 1].color);
      let packed_2 = pack_color_krgb0888(p_state[p_index + 2].color);
      let packed_3 = pack_color_krgb0888(p_state[p_index + 3].color);

      vram32_write32(addr     , packed_0);
      vram32_write32(addr +  4, packed_1);
      vram32_write32(addr +  8, packed_2);
      vram32_write32(addr + 12, packed_3);

    } else {

      let addr = (reg_read(FB_W_SOF1) &  0xfffffffc) +
        (tile_pos.y * 32u + tile_ij.y) * line_stride_bytes +
        (tile_pos.x * 32u + tile_ij.x) * 4u;

      vram32_write32(addr    ,  0xCAFEBABAu);
      vram32_write32(addr + 4,  0xCAFEBABAu);
      vram32_write32(addr + 8,  0xCAFEBABAu);
      vram32_write32(addr + 12, 0xCAFEBABAu);
    }

  }
}

/* The host will dispatch a batch of tiles at a time. Each batch will contain at most
 * one of each tile so that the workgroup can process the entire batch in parallel. Each
 * workgroup will process a single tile from the Region Array. The number of batches will
 * be the maximum number of passes to any particular tile in the region array.
 *
 * For WebGPU, the batch details of which region array entries to process will be passed
 * via dynamic uniform buffer as push constants are not available in WGSL. The workgroup_id.x
 * will be the index into the dispatch_details array.
 */
@compute
@workgroup_size(32, 32, 1)
fn pvr_render_tile(
  @builtin(num_workgroups)      num_groups: vec3<u32>,
  @builtin(workgroup_id)        group_id:   vec3<u32>,
  @builtin(local_invocation_id) local_id:   vec3<u32>,
) {
    /* Get the region array entry for the tile */
    let group_index = num_groups.x * group_id.y + group_id.x;
    let rae_address = reg_read(REGION_BASE) + 24u * group_index;
    let rae = read_region_array_entry(rae_address);

    tile_pos.x = ((rae.header >> 2) & 0x3f);
    tile_pos.y = ((rae.header >> 8) & 0x3f);
    tile_ij    = vec2<u32>(local_id.x, local_id.y);
    param_base = reg_read(PARAM_BASE);

    workgroupBarrier();

    let tile_index: u32 = tile_pos.y * 40u + tile_pos.x;
    p_index = (tile_index * 32u * 32u) + (tile_ij.y * 32u) + tile_ij.x;

    // if (((rae.header >> 30) & 1) == 0) {
      tile_z_clear();
    // }
    
    if ((rae.opaque & 0x80000000) == 0) {
      let opb_address = rae.opaque & 0xfffffcu;
      tile_draw_opaque(opb_address);
    }

    // Wait for all threads to finish before we resolve to the framebuffer
    storageBarrier();

    // if (((rae.header >> 28) & 1) == 0) {
      tile_flush_to_framebuffer();
    // }
}
