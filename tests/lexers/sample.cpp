#include <iostream>
#include "sample.hpp"

namespace demo {
class Widget {
public:
  explicit Widget(int count) : count_(count) {}
  bool ready() const { return count_ > 0; }
private:
  int count_ = 42;
};
}
