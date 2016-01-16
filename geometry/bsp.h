#ifndef BASE_BSP_H_
#define BASE_BSP_H_ 1

#include <memory>
#include <vector>

#include "geometry/vector.h"

class KDTree {
 public:
  KDTree(const std::vector<XYZ>& points);

  std::vector<uint32_t> QuerySphere(const XYZ& center, float radius);

 private:
  struct Node {
    float distance = 0.0f;
    uint8_t axis = 0;

    std::unique_ptr<std::vector<uint32_t>> indices;

    uint32_t left = 0;
    uint32_t right = 0;
  };

  std::vector<Node> nodes_;
};

#endif  // !BASE_BSP_H_
