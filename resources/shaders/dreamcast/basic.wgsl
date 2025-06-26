struct VertexInput {
  @location(0) pos  : vec4<f32>,
  @location(1) uv   : vec4<f32>,
  @location(2) rgba : vec4<f32>,
};

struct VertexOutput {
  @builtin(position) position: vec4f,
  @location(0) color: vec4f,
};

@vertex
fn vs_main(
    in : VertexInput
) -> VertexOutput {
    var out : VertexOutput;
    var pos = in.pos;

    // Real point (x,y,z)
    // HW submitted point 
    // HWX, HWY, HWZ = (x*z, y*z, (ez - sz) / (ez - z))

    let HWX = in.pos.x;
    let HWY = in.pos.y;
    let HWZ = in.pos.z;
    
    pos.x =  HWX / 320.0 - 1.0;
    pos.y = (HWY / 240.0 - 1.0) * -1.0;
    pos.z = 1.0 - HWZ;
    pos.w = 1.0;

    out.position = pos;
    out.color = in.rgba;
    return out;
}

@fragment
fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {
  
    return in.color;
}
