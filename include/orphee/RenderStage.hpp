#pragma once

namespace orphee {
class RenderStage {
public:
  virtual ~RenderStage() = default;
  virtual void draw() = 0;
  virtual void resize() = 0;
};
} // namespace orphee
