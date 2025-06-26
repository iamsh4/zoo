
struct DrawingArea {
  uint top_left;
  uint bottom_right;
};

layout (push_constant) uniform PushConstantData {
  DrawingArea drawing_area;
  int drawing_offset;
} gpu_state;
