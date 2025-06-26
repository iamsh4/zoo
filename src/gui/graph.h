#pragma once

namespace gui {

class LinePlotter {
public:
  LinePlotter(const std::string &title, const u32 size)
    : m_title(title),
      size(size),
      offset(0)
  {
    values.resize(size, 0.0f);
  }

  void push(const float val)
  {
    values[offset] = val;
    offset = (offset + 1) % size;
  }

  void draw()
  {
    float average = 0.0f;
    for (u32 n = 0; n < size; n++) {
      average += values[n];
    }
    average /= float(size);

    char overlay[32];
    snprintf(overlay, 32, "avg %.2f", average);
    ImGui::PlotLines(m_title.c_str(),
                     &values[0],
                     size,
                     offset,
                     overlay,
                     0.1f,
                     200.0f,
                     ImVec2(150.0f, 0));
  }

private:
  const std::string m_title;
  const u32 size;
  std::vector<float> values;
  u32 offset;
};

}
