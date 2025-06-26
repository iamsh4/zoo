struct RenderParams {
  num_tiles : vec2<u32>,
  num_triangles : u32,
  fpu_cull_val : f32,
};

struct Vertex {
  pos  : vec4<f32>,
  uv   : vec4<f32>,
  rgba : vec4<f32>,
};
struct Triangle {
  v0 : Vertex,
  v1 : Vertex,
  v2 : Vertex,
};

@group(0) @binding(0) var<storage,read>       guest_triangles:  array<Triangle>;
@group(0) @binding(1) var<storage,read_write> tile_tri_counter: array<atomic<u32>>;
@group(0) @binding(2) var<storage,read_write> tile_tri_indices: array<u32>;

@group(0) @binding(3) var<storage,read_write> fb_fragment_counter: array<atomic<u32>>;
@group(0) @binding(4) var<storage,read_write> fb_fragments: array<u32>;
@group(0) @binding(5) var<storage,read_write> fb_fragment_depths: array<f32>;

@group(0) @binding(6) var<uniform> params: RenderParams;

@compute @workgroup_size(32, 32, 1)
fn cs_clear_tile_triangle_counters(
  @builtin(global_invocation_id) id: vec3<u32>
) {
  let index = id.y * params.num_tiles[0] + id.x;
  atomicStore(&tile_tri_counter[index], 0u);
  return;
}

@compute @workgroup_size(32, 1, 1)
fn cs_triangle_binning(
  @builtin(global_invocation_id) id: vec3<u32>
) {
  if (id.x >= params.num_triangles) {
    return;
  }

  let tri : Triangle = guest_triangles[id.x];

  let min_x = i32(min(tri.v0.pos.x, min(tri.v1.pos.x, tri.v2.pos.x)));
  let min_y = i32(min(tri.v0.pos.y, min(tri.v1.pos.y, tri.v2.pos.y)));
  let max_x = i32(max(tri.v0.pos.x, max(tri.v1.pos.x, tri.v2.pos.x)));
  let max_y = i32(max(tri.v0.pos.y, max(tri.v1.pos.y, tri.v2.pos.y)));

  // Number of tiles being rendered in each direction
  let TX = i32(params.num_tiles[0]);
  let TY = i32(params.num_tiles[1]);

  // Note, need to clamp to 0 to avoid dividing negative positions by 32 which
  // would erroneously toward the origin
  let min_tile_x = max(0, min_x / 32);
  let min_tile_y = max(0, min_y / 32);
  let max_tile_x = min(TX - 1, max(0, max_x / 32));
  let max_tile_y = min(TY - 1, max(0, max_y / 32));

  // If the triangle is completely outside the tile grid, we can skip it
  if (max_tile_x < 0 || max_tile_y < 0 || min_tile_x >= TX || min_tile_y >= TY) {
    return;
  }

  // Triangle culling
  let tri_det = f32(
    tri.v0.pos.x * (tri.v1.pos.y - tri.v2.pos.y) +
    tri.v1.pos.x * (tri.v2.pos.y - tri.v0.pos.y) +
    tri.v2.pos.x * (tri.v0.pos.y - tri.v1.pos.y)
  );
  // TODO : Front and back culling modes
  // TODO : Culling is on a per-polygon (triangle) basis.
  if (abs(tri_det) < params.fpu_cull_val) {
    return;
  }

  // TODO : This should be a uniform
  let kMaxTileTriangles = 1024u;

  for (var tile_y = min_tile_y; tile_y <= max_tile_y; tile_y++) {
    for (var tile_x = min_tile_x; tile_x <= max_tile_x; tile_x++) {
      let tile_index = u32(tile_y * TX + tile_x);
      let tri_index = atomicAdd(&tile_tri_counter[tile_index], 1u);
      if (tri_index < kMaxTileTriangles) {
        tile_tri_indices[tile_index * kMaxTileTriangles + tri_index] = id.x;
      }
    }
  }

  return;
}

// global indexing : (0..num)
@compute @workgroup_size(32, 1, 1)
fn cs_clear_fb_fragment_counters(
  @builtin(global_invocation_id) id: vec3<u32>
) {
  // TODO : 64 is ideally a uniform or something

  // index within super-tile
  let index = id.y*64u + id.x;
  atomicStore(&fb_fragment_counter[index], 0u);
  return;
}

@compute @workgroup_size(32, 1, 1)
fn cs_rasterize_triangles(
  @builtin(global_invocation_id) id: vec3<u32>
) {

  return;
}
