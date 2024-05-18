#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

#include <glm/glm.hpp>

#include <orphee/orphee.hpp>

namespace mesh {
struct Mesh {
  std::vector<glm::vec3> vertices;
  std::vector<uint32_t> indices;
};

pxr::VtArray<int> triangulate(const pxr::VtArray<int> &faceVertexCount,
                              const pxr::VtArray<int> &faceVertexIndices) {
  pxr::VtArray<int> indices;
  indices.reserve(faceVertexIndices.size() * 1.5);

  size_t offset = 0;

  for (const auto count : faceVertexCount) {
    if (count == 3) {
      for (uint32_t j = 0; j < count; ++j) {
        indices.push_back(faceVertexIndices[offset + j]);
      }
    } else if (count == 4) {
      indices.push_back(faceVertexIndices[offset + 0]);
      indices.push_back(faceVertexIndices[offset + 1]);
      indices.push_back(faceVertexIndices[offset + 2]);

      indices.push_back(faceVertexIndices[offset + 0]);
      indices.push_back(faceVertexIndices[offset + 2]);
      indices.push_back(faceVertexIndices[offset + 3]);
    }

    offset += count;
  }

  return indices;
}

// For the moment just find the first mesh available
std::optional<Mesh> load(std::filesystem::path path) {
  const auto stage = pxr::UsdStage::Open(path.string());

  for (const auto &prim : stage->Traverse()) {
    if (prim.IsA<pxr::UsdGeomMesh>()) {
      const auto geomMesh = pxr::UsdGeomMesh(prim);

      pxr::VtArray<int> faceVertexCounts;
      geomMesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);

      pxr::VtArray<int> faceVertexIndices;
      geomMesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

      if (!faceVertexCounts.empty()) {
        faceVertexIndices = triangulate(faceVertexCounts, faceVertexIndices);
      }

      pxr::VtArray<pxr::GfVec3f> points;
      geomMesh.GetPointsAttr().Get(&points);

      Mesh m;
      m.vertices.reserve(points.size());
      for (const auto &point : points) {
        m.vertices.emplace_back(point[0], point[1], point[2]);
      }

      m.indices.reserve(faceVertexIndices.size());
      for (const auto index : faceVertexIndices) {
        m.indices.push_back(index);
      }

      return m;
    }
  }

  return {};
}
} // namespace mesh
