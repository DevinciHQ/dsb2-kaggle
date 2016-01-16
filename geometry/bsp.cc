#include <deque>

#include <kj/debug.h>

#include "geometry/bsp.h"

KDTree::KDTree(const std::vector<XYZ>& points) {
  nodes_.emplace_back();

  nodes_[0].indices = std::make_unique<std::vector<uint32_t>>();
  nodes_[0].indices->reserve(points.size());

  for (size_t i = 0; i < points.size(); ++i) nodes_[0].indices->emplace_back(i);

  std::deque<size_t> queue;
  queue.emplace_back(0);

  while (!queue.empty()) {
    auto& node = nodes_[queue.front()];
    queue.pop_front();

    if (node.indices->size() < 2) continue;

    auto max = points[(*node.indices)[0]];
    auto min = max;

    for (size_t i = 1; i < node.indices->size(); ++i) {
      const auto& point = points[(*node.indices)[i]];

      if (point.x > max.x)
        max.x = point.x;
      else if (point.x < min.x)
        min.x = point.x;
      if (point.y > max.y)
        max.y = point.y;
      else if (point.y < min.y)
        min.y = point.y;
      if (point.z > max.z)
        max.z = point.z;
      else if (point.z < min.z)
        min.z = point.z;
    }

    const auto xmag = max.x - min.x;
    const auto ymag = max.y - min.y;
    const auto zmag = max.z - min.z;

    if (xmag > ymag && xmag > zmag) {
      if (xmag < 1.0e-3f) continue;
      node.axis = 0;
      node.distance = 0.5f * (max.x + min.x);
    } else if (ymag > zmag) {
      if (ymag < 1.0e-3f) continue;
      node.axis = 1;
      node.distance = 0.5f * (max.y + min.y);
    } else {
      if (zmag < 1.0e-3f) continue;
      node.axis = 2;
      node.distance = 0.5f * (max.z + min.z);
    }

    const auto left_idx = node.left = nodes_.size();
    const auto right_idx = node.right = nodes_.size() + 1;

    auto indices = std::move(node.indices);
    node.indices = nullptr;

    const auto axis = node.axis;
    const auto distance = node.distance;

    // This invalidates `node`.
    nodes_.emplace_back();
    nodes_.emplace_back();

    auto& left = nodes_[left_idx];
    left.indices = std::move(indices);

    auto& right = nodes_[right_idx];
    right.indices = std::make_unique<std::vector<uint32_t>>();

    size_t out = 0;
    for (const auto idx : *left.indices) {
      if (points[idx].Get(axis) < distance) {
        (*left.indices)[out++] = idx;
      } else {
        right.indices->emplace_back(idx);
      }
    }

    left.indices->resize(out);

    KJ_REQUIRE(!left.indices->empty());
    KJ_REQUIRE(!right.indices->empty());

    queue.push_back(left_idx);
    queue.push_back(right_idx);
  }
}

std::vector<uint32_t> KDTree::QuerySphere(const XYZ& center, float radius) {
 std::vector<uint32_t> result;

  std::deque<size_t> queue;
  queue.emplace_back(0);

  while (!queue.empty()) {
    const auto& node = nodes_[queue.front()];
    queue.pop_front();

    if (node.indices) {
      result.insert(result.end(), node.indices->begin(), node.indices->end());
      continue;
    }

    const auto distance = center.Get(node.axis);

    if (distance - radius < node.distance)
      queue.push_back(node.left);

    if (distance + radius >= node.distance)
      queue.push_back(node.right);
  }

  return result;
}
