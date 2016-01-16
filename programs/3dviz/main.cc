#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <unordered_map>
#include <valarray>
#include <vector>

#include <err.h>
#include <sysexits.h>

#include <gsl/gsl_fit.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_matrix_double.h>
#include <gsl/gsl_statistics.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include "base/columnfile.h"
#include "base/file.h"
#include "base/string.h"
#include "geometry/bsp.h"
#include "geometry/marching-cubes.h"
#include "programs/3dviz/x11.h"

namespace {

auto Pow2(auto v) { return v * v; }

static GLuint create_texture(const void* data, unsigned int width,
                             unsigned int height) {
  GLuint result;

  glGenTextures(1, &result);
  glBindTexture(GL_TEXTURE_2D, result);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  return result;
}

/*
static GLuint create_gradient_texture(void) {
  const unsigned int bufferWidth = 256, bufferHeight = 256;
  unsigned int x, y, i;

  GLuint result;

  std::unique_ptr<unsigned char[]> buffer;
  buffer.reset(new unsigned char[bufferWidth * bufferHeight * 4]);

  for (y = 0, i = 0; y < bufferHeight; ++y) {
    for (x = 0; x < bufferWidth; ++x, i += 4) {
      buffer.get()[i] = x * 255 / (bufferWidth - 1);
      buffer.get()[i + 1] = y * 255 / (bufferHeight - 1);
      buffer.get()[i + 2] = 0;
      buffer.get()[i + 3] = 0xff;
    }
  }

  result = create_texture(buffer.get(), bufferWidth, bufferHeight);

  return result;
}
*/

template <typename T>
T To(const ev::StringRef& data) {
  T result;
  KJ_REQUIRE(data.size() == sizeof(result), data.size());
  memcpy(&result, data.data(), sizeof(result));
  return result;
}

struct Image {
  size_t rows = 0;
  size_t cols = 0;

  // Physical distance between centers of pixels, in millimeters.
  float row_spacing = 0.0f;
  float col_spacing = 0.0f;

  int stack_position = 0;

  XYZ position;
  XYZ row_direction;
  XYZ col_direction;

  std::vector<float> data;
};

struct Dataset {
  std::vector<Image> sax;
  std::vector<Image> ch2;
  std::vector<Image> ch4;
};

Dataset LoadDICOMs(std::string prefix) {
  ev::ColumnFileSelect select(
      ev::ColumnFileReader(ev::OpenFile("data/dicoms.col", O_RDONLY)));

  select.AddSelection(0);
  select.AddSelection(1);
  select.AddSelection(2);
  select.AddSelection(3);
  select.AddSelection(4);
  select.AddSelection(0x0018'5100);  // Patient position.
  select.AddSelection(0x0020'0032);  // Image position.
  select.AddSelection(0x0020'0037);  // Image plane cosines.
  select.AddSelection(0x0020'1041);
  select.AddSelection(0x0028'0030);  // Pixel spacing.

  // Filter for paths listed in `inputs`.
  select.AddFilter(0, [&prefix](const ev::StringRefOrNull& path) {
    return ev::HasPrefix(path.StringRef(), prefix);
  });

  Dataset result;

  ev::concurrency::RegionPool region_pool(16, 2048);

  select.Execute(
      region_pool,
      [&result](const std::vector<std::pair<uint32_t, ev::StringRefOrNull>>& row) {
        std::unordered_map<uint32_t, ev::StringRefOrNull> data;
        for (const auto& kv : row) data.emplace(kv.first, kv.second);

        Image image;

        const auto path = data.at(0).StringRef();
        const auto type = data.at(1).StringRef().str();
        image.rows = To<uint32_t>(data.at(2).StringRef());
        image.cols = To<uint32_t>(data.at(3).StringRef());
        const auto& pixel_data = data.at(4).StringRef();

        const auto patient_position = data.at(0x0018'5100).StringRef().str();
        KJ_REQUIRE(patient_position == "HFS", patient_position);

        const auto image_position = data.at(0x0020'0032).StringRef().str();
        KJ_REQUIRE(3 == sscanf(image_position.c_str(), "%f,%f,%f",
                               &image.position.x, &image.position.y,
                               &image.position.z));
        const auto image_orientation = data.at(0x0020'0037).StringRef().str();
        KJ_REQUIRE(6 == sscanf(image_orientation.c_str(), "%f,%f,%f,%f,%f,%f",
                               &image.row_direction.x, &image.row_direction.y,
                               &image.row_direction.z, &image.col_direction.x,
                               &image.col_direction.y, &image.col_direction.z));
        image.row_direction = image.row_direction.normalize();
        image.col_direction = image.col_direction.normalize();

#if 0
        fprintf(stderr, "Position: %.3f, %.3f, %.3f\n", image.position.x,
                image.position.y, image.position.z);
        fprintf(stderr, "Row Direction: %.3f, %.3f, %.3f\n",
                image.row_direction.x, image.row_direction.y,
                image.row_direction.z);
        fprintf(stderr, "Col Direction: %.3f, %.3f, %.3f\n",
                image.col_direction.x, image.col_direction.y,
                image.col_direction.z);
#endif

        image.stack_position =
            std::round(To<double>(data.at(0x0020'1041).StringRef()));

        const auto& pixel_spacing = data.at(0x0028'0030).StringRef().str();
        KJ_REQUIRE(2 == sscanf(pixel_spacing.c_str(), "%f,%f",
                               &image.row_spacing, &image.col_spacing));

        image.data.resize(image.rows * image.cols);

        if (type == "int16") {
          KJ_REQUIRE(
              pixel_data.size() == image.rows * image.cols * sizeof(int16_t),
              pixel_data.size(), image.rows * image.cols * sizeof(int16_t));

          auto pixel_begin =
              reinterpret_cast<const int16_t*>(pixel_data.begin());
          auto pixel_end = pixel_begin + image.rows * image.cols;

          std::copy(pixel_begin, pixel_end, image.data.begin());
        } else if (type == "uint16") {
          KJ_REQUIRE(
              pixel_data.size() == image.rows * image.cols * sizeof(uint16_t),
              pixel_data.size(), image.rows * image.cols * sizeof(uint16_t));

          auto pixel_begin =
              reinterpret_cast<const uint16_t*>(pixel_data.begin());
          auto pixel_end = pixel_begin + image.rows * image.cols;

          std::copy(pixel_begin, pixel_end, image.data.begin());
        } else {
          KJ_FAIL_REQUIRE("Unknown data type", type);
        }

        if (path.contains("/2ch_")) {
          result.ch2.emplace_back(std::move(image));
        } else if (path.contains("/4ch_")) {
          result.ch4.emplace_back(std::move(image));
        } else {
          KJ_REQUIRE(path.contains("/sax_"));
          result.sax.emplace_back(std::move(image));
        }
      });

  KJ_REQUIRE((result.sax.size() % 30) == 0, result.sax.size());

  std::stable_sort(result.sax.begin(), result.sax.end(),
                   [](const auto& lhs, const auto& rhs) {
                     return lhs.position.y < rhs.position.y;
                   });

  return result;
}

void DeduplicateVertices(std::vector<XYZ>& vertices,
                         std::vector<XYZ>& normals,
                         std::vector<uint32_t>& indices,
                         float margin = 0.0001f) {
  KDTree bsp(vertices);

  std::vector<XYZ> output_vertices, output_normals;

  const auto kPlaceholder = std::numeric_limits<uint32_t>::max();

  // Map from input vertex indexes to output vertex indexes.
  std::vector<uint32_t> map;
  map.resize(vertices.size(), kPlaceholder);

  for (size_t i = 0; i < vertices.size(); ++i) {
    if (map[i] != kPlaceholder) continue;
    map[i] = output_vertices.size();
    output_vertices.emplace_back(vertices[i]);
    output_normals.emplace_back(normals[i]);

    const auto& reference = vertices[i];

    for (const auto j :
         bsp.QuerySphere(vertices[i], std::sqrt(3.0f * margin))) {
      if (map[j] != kPlaceholder) continue;

      if (std::fabs(reference.x - vertices[j].x) < margin &&
          std::fabs(reference.y - vertices[j].y) < margin &&
          std::fabs(reference.z - vertices[j].z) < margin) {
        map[j] = map[i];
      }
    }
  }

  vertices.swap(output_vertices);
  normals.swap(output_normals);

  size_t out = 0;

  for (size_t i = 0; i < indices.size(); i += 3) {
    indices[out] = map[indices[i]];
    indices[out + 1] = map[indices[i + 1]];
    indices[out + 2] = map[indices[i + 2]];

    // Skip degenerate triangles.
    if (indices[out] == indices[out + 1] || indices[out] == indices[out + 2] ||
        indices[out + 1] == indices[out + 2])
      continue;

    out += 3;
  }

  indices.resize(out);
}

void ColorObjects(size_t vertex_count, const std::vector<uint32_t>& indices,
                  std::vector<std::vector<uint32_t>>& objects) {
  std::unordered_map<uint32_t, uint32_t> parents;
  std::unordered_map<uint32_t, std::unordered_set<uint32_t>> groups;

  const auto connect = [&parents, &groups](uint32_t a, uint32_t b) {
    {
      auto i = parents.find(a);
      if (i != parents.end()) a = i->second;
      KJ_REQUIRE(parents.end() == parents.find(a));
    }

    {
      auto i = parents.find(b);
      if (i != parents.end()) b = i->second;
      KJ_REQUIRE(parents.end() == parents.find(b));
    }

    if (a > b)
      std::swap(a, b);
    else if (a == b)
      return false;

    auto& group = groups[a];

    if (group.count(b)) return false;

    group.emplace(b);
    parents[b] = a;

    // Reassign children of `b` to `a`.
    auto i = groups.find(b);
    if (i != groups.end()) {
      for (const auto n : i->second) {
        group.emplace(n);
        parents[n] = a;
      }

      groups.erase(i);
    }

    return true;
  };

  for (size_t i = 0; i < indices.size(); i += 3) {
    const auto a = indices[i];
    const auto b = indices[i + 1];
    const auto c = indices[i + 2];
    connect(a, b);
    connect(a, c);
    connect(b, c);
  }

  std::vector<size_t> vertex_to_object;
  vertex_to_object.resize(vertex_count);

  size_t object_index = 0;

  for (auto& group : groups) {
    vertex_to_object[group.first] = object_index;

    for (const auto child : group.second)
      vertex_to_object[child] = object_index;

    ++object_index;
  }

  objects.resize(groups.size());

  for (size_t i = 0; i < indices.size(); i += 3) {
    auto& object = objects[vertex_to_object[indices[i]]];
    object.emplace_back(indices[i]);
    object.emplace_back(indices[i + 1]);
    object.emplace_back(indices[i + 2]);
  }

  fprintf(stderr, "%zu objects ", groups.size());
}

struct LeftVentricle {
  XYZ base;
  XYZ axis;
  float length = 0.0f;
  float top_radius = 0.0f;
};

struct Scene {
  XYZ center;
  XYZ up;

  std::vector<XYZ> vertices;
  std::vector<XYZ> normals;
  std::vector<uint32_t> indices;

  LeftVentricle lv;
  std::vector<uint32_t> lv_model_indices;
};

auto MakeGSLMatrix(size_t rows, size_t cols) {
  return std::unique_ptr<gsl_matrix, decltype(&gsl_matrix_free)>(
      gsl_matrix_calloc(rows, cols), gsl_matrix_free);
}

auto MakeGSLVector(size_t cols) {
  return std::unique_ptr<gsl_vector, decltype(&gsl_vector_free)>(
      gsl_vector_calloc(cols), gsl_vector_free);
}

#if 0
LeftVentricle FitLeftVentricle(const std::vector<XYZ>& vertices, const std::vector<uint32_t>& indices) {
  LeftVentricle result;

  std::unordered_set<uint32_t> unique_points;

  for (const auto index : indices) unique_points.emplace(index);

  auto point_matrix = MakeGSLMatrix(unique_points.size(), 3);

  size_t o = 0;
  for (const auto index : unique_points) {
    gsl_matrix_set(point_matrix.get(), o, 0, vertices[index].x);
    gsl_matrix_set(point_matrix.get(), o, 1, vertices[index].y);
    gsl_matrix_set(point_matrix.get(), o, 2, vertices[index].z);
    ++o;
  }

  auto points_V = MakeGSLMatrix(3, 3);
  auto vector_S = MakeGSLVector(3);
  auto work = MakeGSLVector(3);
  KJ_REQUIRE(0 == gsl_linalg_SV_decomp(point_matrix.get(), points_V.get(), vector_S.get(), work.get()));

  result.axis.x = gsl_matrix_get(points_V.get(), 0, 0);
  result.axis.y = gsl_matrix_get(points_V.get(), 0, 1);
  result.axis.z = gsl_matrix_get(points_V.get(), 0, 2);
  result.axis = result.axis.normalize();

  // Find vectors orthogonal to major axis.
  const auto u = result.axis.cross(XYZ(result.axis.y, result.axis.z, result.axis.x)).normalize();
  const auto v = result.axis.cross(u);

  auto min_distance = 0.0f, max_distance = 0.0f;
  std::vector<float> us, vs;
  bool first = true;

  for (const auto index : unique_points) {
    const auto& point = vertices[index];
    const auto distance = point * result.axis;

    if (first) {
      min_distance = max_distance = distance;
      first = false;
    } else if (distance < min_distance) {
      min_distance = distance;
    } else if (distance > max_distance) {
      max_distance = distance;
    }

    us.emplace_back(point * u);
    vs.emplace_back(point * v);
  }

  std::nth_element(us.begin(), us.begin() + us.size() / 2, us.end());
  std::nth_element(vs.begin(), vs.begin() + vs.size() / 2, vs.end());

  result.base = result.axis * min_distance + u * us[us.size() / 2] + v * vs[vs.size() / 2];

  result.length = max_distance - min_distance;

  std::vector<double> x, y;

  for (const auto index : unique_points) {
    const auto& point = vertices[index];

    const auto distance = point * result.axis - min_distance;

    const auto layer_center = result.base + result.axis * distance;

    x.emplace_back(std::sqrt(distance / result.length));
    y.emplace_back((point - layer_center).magnitude());
  }

  double c1, cov11, sumsq;
  gsl_fit_mul(x.data(), 1, y.data(), 1, x.size(), &c1, &cov11, &sumsq);

  KJ_REQUIRE(c1 > 0, c1);
  result.top_radius = c1;

  return result;
}

double ScoreLeftVentricle(const LeftVentricle& lv, const std::vector<XYZ>& points, std::vector<XYZ>* good = nullptr) {
  double area;

  if (lv.length == lv.top_radius) {
    // Area of hemisphere.
    area = 2.0 * M_PI * Pow2(lv.top_radius);
  } else if (lv.length > lv.top_radius) {
    // Area of half prolate spheroid.
    const auto e = 1.0 - Pow2(lv.top_radius) / Pow2(lv.length);
    area = 1.0 * M_PI * Pow2(lv.top_radius) * (1.0 + lv.length / (lv.top_radius * e) * asin(e));
  } else {
    // Area of half oblate spheroid.
    const auto e = 1.0 - Pow2(lv.length) / Pow2(lv.top_radius);
    area = 1.0 * M_PI * Pow2(lv.top_radius) * (1.0 + (1.0 - e * e) / e * atanh(e));
  }

  KJ_REQUIRE(area > 0.0, area, lv.top_radius, lv.length);

  fprintf(stderr, "Area: %.3f\n", area);
  double result = 0.0;

  for (size_t i = 0; i < indices.size(); i += 3) {
    const auto& v0 = vertices[indices[i]];
    const auto& v1 = vertices[indices[i + 1]];
    const auto& v2 = vertices[indices[i + 2]];

    KJ_REQUIRE(indices[i] != indices[i + 1]);
    KJ_REQUIRE(indices[i] != indices[i + 2]);
    KJ_REQUIRE(indices[i + 1] != indices[i + 2]);

    const auto tri_normal = (((v1 - v0).cross(v2 - v0)));
    const auto tri_area = tri_normal.magnitude();
  }

  return result;
  const auto base_plane_distance = lv.base * lv.axis;

  for (const auto& point : points) {
    const auto distance = std::max(0.0f, std::min(lv.length, point * lv.axis - base_plane_distance));
 
    const auto layer_center = lv.base + lv.axis * distance;
    const auto layer_radius = sqrt(distance / lv.length) * lv.top_radius;

    const auto radius = (point - layer_center).magnitude();

    const auto score = 1.0 / (1.0 + Pow2(radius - layer_radius));

    if (good && score > 0.1) good->emplace_back(point);

    result += score;
  }
}
#endif

Scene LoadScene(const std::vector<Image>& images, const Image& ch2, const Image& ch4, bool foo) {
  Scene result;

  const auto rows = images[0].rows;
  const auto cols = images[0].cols;

  std::vector<float> field;
  field.resize(rows * cols * images.size());

  for (size_t z = 0; z < images.size(); ++z) {
    const auto& image = images[z];
    KJ_REQUIRE(image.rows == rows);
    KJ_REQUIRE(image.cols == cols);
    for (size_t y = 0; y < rows; ++y) {
      for (size_t x = 0; x < cols; ++x) {
        field[((z * rows) + y) * cols + x] = image.data[y * cols + x];
      }
    }
  }

  std::valarray<float> valarray(field.data(), field.size());

  float cutoff = 0.0f;
  {
    auto foo = field;
    std::sort(foo.begin(), foo.end());
    cutoff = foo[foo.size() * 85 / 100];
  }

  std::transform(field.begin(), field.end(), field.begin(),
                 [cutoff](const auto v) { return v - cutoff; });

  fprintf(stderr, "Triangulating... ");
  Triangulate(field.data(), cols, rows, images.size(), result.vertices, result.normals,
              result.indices);
  fprintf(stderr, "done (%zu vertices).\n", result.vertices.size());

  KJ_REQUIRE(result.indices.size() > 0);
  KJ_REQUIRE(result.vertices.size() < std::numeric_limits<uint32_t>::max(),
             result.vertices.size());

  fprintf(stderr, "Deduplicating... ");
  DeduplicateVertices(result.vertices, result.normals, result.indices);
  fprintf(stderr, "done (%zu vertices).\n", result.vertices.size());

  const auto y_scale =
      (images.back().stack_position - images.front().stack_position) /
      (images.size() - 1.0);

  const auto origin = images.front().position;
  const auto row_direction =
      images.front().row_direction.normalize() * images.front().col_spacing;
  const auto col_direction =
      images.front().col_direction.normalize() * images.front().row_spacing;
  const auto axial_direction = (images.back().position - images.front().position) * (1.0f / (images.size() - 1));

  result.center = origin +
      row_direction * images.front().cols * 0.5f + col_direction * images.front().rows * 0.5f/* + axial_direction * images.size() * 0.5f*/;
  result.up = axial_direction.normalize();

  std::transform(result.vertices.begin(), result.vertices.end(),
                 result.vertices.begin(),
                 [rows, cols, y_scale, origin, row_direction, col_direction,
                  axial_direction](auto v) {
                   auto point = origin;
                   point += row_direction * v.x;
                   point += col_direction * v.y;
                   point += axial_direction * v.z;
                   return point;
                 });

  fprintf(stderr, "Coloring... ");
  std::vector<std::vector<uint32_t>> objects;
  ColorObjects(result.vertices.size(), result.indices, objects);
  fprintf(stderr, "done.\n");

  // Image planes for alternate (non-stack) views.
  const auto ch2_plane = ch2.row_direction.cross(ch2.col_direction);
  const auto ch2_distance = ch2_plane * ch2.position;
  const auto ch4_plane = ch4.row_direction.cross(ch4.col_direction);
  const auto ch4_distance = ch4_plane * ch4.position;

  std::vector<double> object_mask;
  object_mask.resize(objects.size(), true);

  for (size_t object_index = 0; object_index < objects.size(); ++object_index) {
    const auto& object = objects[object_index];

    double area = 0.0;

    for (size_t i = 0; i < object.size(); i += 3) {
      const auto& v0 = result.vertices[object[i]];
      const auto& v1 = result.vertices[object[i + 1]];
      const auto& v2 = result.vertices[object[i + 2]];

      area += 0.5f * (v1 - v0).cross(v2 - v0).magnitude();
    }

    // Discard objects with too small surface area (<10cm²) to be a heart
    // ventricle.
    if (area < 1000.0f) {
      object_mask[object_index] = false;
      continue;
    }

    bool sides_2ch[2] = { false, false };
    bool sides_4ch[2] = { false, false };

    for (const auto idx : object) {
      const auto side_2ch = result.vertices[idx] * ch2_plane > ch2_distance;
      const auto side_4ch = result.vertices[idx] * ch4_plane > ch4_distance;

      sides_2ch[side_2ch ? 1 : 0] = true;
      sides_4ch[side_4ch ? 1 : 0] = true;
    }

    // Discard objects that don't have vertices on both sides of the 2 chamber
    // and 4 chambe image planes.
    if (!sides_2ch[0] || !sides_2ch[1] || !sides_4ch[0] || !sides_4ch[1]) {
      object_mask[object_index] = false;
      continue;
    }
  }

  result.indices.clear();

  for (size_t object_index = 0; object_index < objects.size(); ++object_index) {
    if (!object_mask[object_index]) continue;

    result.indices.insert(result.indices.end(), objects[object_index].begin(),
                          objects[object_index].end());
  }

  result.lv.base = result.center;
  result.lv.axis = result.up;
  result.lv.length = (images.back().position - images.front().position).magnitude();
  result.lv.top_radius = 30.0f;

  return result;
}

void DrawLeftVentricle(const LeftVentricle& lv) {
  const auto v0 = lv.axis.cross(XYZ(lv.axis.y, lv.axis.z, lv.axis.x)).normalize();
  const auto v1 = lv.axis.cross(v0);

  for (int i = 0; i <= 10; ++i) {
    const auto layer_radius = std::sqrt(1.0 - Pow2(1.0 - (i / 10.0))) * lv.top_radius;
    const auto layer_center = lv.base + lv.axis * (lv.length * i / 10.0);

    glBegin(GL_LINE_LOOP);

    for (int j = 0; j < 30; ++j) {
      const auto t = j * 2.0 * M_PI / 30;
      auto pos = layer_center;
      pos += v0 * cos(t) * layer_radius;
      pos += v1 * sin(t) * layer_radius;
      glVertex3fv(&pos.x);
    }

    glEnd();
  }

  for (int j = 0; j < 10; ++j) {
    const auto t = j * 2.0 * M_PI / 10;

    glBegin(GL_LINE_STRIP);

    for (int i = 0; i <= 30; ++i) {
      const auto layer_radius = std::sqrt(1.0 - Pow2(1.0 - (i / 30.0))) * lv.top_radius;
      const auto layer_center = lv.base + lv.axis * (lv.length * i / 30.0);

      auto pos = layer_center;
      pos += v0 * cos(t) * layer_radius;
      pos += v1 * sin(t) * layer_radius;
      glVertex3fv(&pos.x);
    }

    glEnd();
  }
}

}  // namespace

int main(int argc, char** argv) try {
  KJ_REQUIRE(argc == 2, argc);

  auto dataset = LoadDICOMs(argv[1]);
  KJ_REQUIRE(dataset.sax.size() >= 2, dataset.sax.size());

  std::vector<Scene> scenes;

  for (size_t frame = 0; frame < 30; ++frame) {
    std::vector<Image> scene_images;

    for (size_t i = frame; i < dataset.sax.size(); i += 30) {
      if (i >= 30 && (dataset.sax[i].position - dataset.sax[i - 30].position).magnitude() < 5.0f) {
        continue;
      }

      scene_images.emplace_back(dataset.sax[i]);
    }

    scenes.emplace_back(LoadScene(scene_images, dataset.ch2[frame], dataset.ch4[frame], frame == 0));
  }

  float sum_x = 0.0f, sum_y = 0.0f;
  size_t count = 0;

  const auto mean_x = sum_x / count;
  const auto mean_y = sum_y / count;

  X11_Init();

  // const auto texture = create_gradient_texture();

  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glFrontFace(GL_CW);

  size_t frame = 0;
  for (;; ++frame) {
    const auto x = frame * 0.01f;

    const auto& scene = scenes[frame % scenes.size()];

    X11_ProcessEvents();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)X11_window_height / X11_window_height, 10.0,
                   10000.0);

    auto eye = scene.center;
    eye.x += 700.0f * cos(x);
    eye.y += -400.0f;
    eye.z += 700.0f * sin(x);

    XYZ up(0.0f, -1.0f, 0.0f);
    //- scene.up * 700.0f;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye.x, eye.y, eye.z, scenes[0].lv.base.x, scenes[0].lv.base.y,
              scenes[0].lv.base.z, up.x, up.y, up.z);

    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINES);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_BLEND);

#if 1
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPointSize(8.0);
    glBegin(GL_POINTS);

    {
      const auto& image = dataset.ch4[frame % 30];
      const auto origin = image.position;
      const auto row_direction = image.row_direction.normalize() * image.col_spacing;
      const auto col_direction = image.col_direction.normalize() * image.row_spacing;

      std::valarray<float> valarray(image.data.data(), image.data.size());
      const auto max = valarray.max();
      const auto min = valarray.min();

      for (size_t y = 0; y < image.rows; ++y) {
        for (size_t x = 0; x < image.cols; ++x) {
          KJ_REQUIRE(y * image.cols + x < image.data.size(), x, y);
          const auto c = (image.data[y * image.cols + x] - min) / (max - min);
          glColor4f(1.0f, 1.0f, 1.0f, c * 0.25f);
          auto point = origin;
          point += row_direction * x;
          point += col_direction * y;
          glVertex3f(point.x, point.y, point.z);
        }
      }
    }

    glEnd();
#endif

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glColor4f(1.0f, 0.4f, 0.5f, 0.1f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, scene.vertices.data());
    glDrawRangeElements(GL_TRIANGLES, 0, scene.vertices.size() - 1,
                        scene.indices.size(), GL_UNSIGNED_INT,
                        scene.indices.data());

    std::unordered_set<uint32_t> verts;
    verts.insert(scene.indices.begin(), scene.indices.end());

    glColor4f(1.0f, 1.0f, 1.0f, 0.1f);
    glBegin(GL_LINES);
    for (const auto i : verts) {
      glVertex3fv(&scene.vertices[i].x);

      const auto pt = (scene.vertices[i] + scene.normals[i] * 10.0f);
      glVertex3fv(&pt.x);
    }
    glEnd();
#if 0
    glBegin(GL_LINES);
    glColor4f(1.0f, 0.0f, 0.0f, 0.8f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(100.0f, 0.0f, 0.0f);

    glColor4f(0.0f, 1.0f, 0.0f, 0.8f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 100.0f, 0.0f);

    glColor4f(0.0f, 0.0f, 1.0f, 0.8f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, 100.0f);
    glEnd();
#endif

#if 0
    glColor4f(0.2f, 0.5f, 1.0f, 1.0f);
    glVertexPointer(3, GL_FLOAT, 0, scenes[0].vertices.data());
    glDrawRangeElements(GL_TRIANGLES, 0, scenes[0].vertices.size() - 1,
                        scenes[0].lv_model_indices.size(), GL_UNSIGNED_INT,
                        scenes[0].lv_model_indices.data());

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    DrawLeftVentricle(scenes[0].lv);
#endif

    X11_SwapBuffers();
  }
} catch (kj::Exception e) {
  KJ_LOG(FATAL, e);
} catch (std::runtime_error e) {
  KJ_LOG(FATAL, e.what());
}
