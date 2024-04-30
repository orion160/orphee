#pragma once

namespace orphee {
class App {
public:
  virtual ~App() = default;
  virtual void run() = 0;
};
} // namespace orphee
