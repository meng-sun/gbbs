#include "benchmarks/SCAN/IndexBased/scan.h"
#include "benchmarks/SCAN/IndexBased/scan_helpers.h"

#include <math.h>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ligra/graph.h"
#include "ligra/graph_test_utils.h"
#include "ligra/undirected_edge.h"
#include "ligra/vertex.h"
#include "pbbslib/seq.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

using ClusterList = std::set<std::set<uintE>>;
using VertexList = std::vector<uintE>;

namespace gt = graph_test;
namespace i = indexed_scan;
namespace ii = indexed_scan::internal;

namespace {

ii::EdgeSimilarity MakeEdgeSimilarity(
    const uintE source,
    uintE neighbor,
    const double similarity) {
  return {
    .source = source,
    .neighbor = neighbor,
    .similarity = static_cast<float>(similarity)
  };
}

ii::CoreThreshold
MakeCoreThreshold(const uintE vertex_id, const double threshold) {
  return {.vertex_id = vertex_id, .threshold = static_cast<float>(threshold)};
}

// Checks that `clustering` has the expected clusters and returns true if the
// check passes.
//
// The implementation may be inefficient, so don't expect this function to work
// well on large graphs.
//
// Arguments:
//   clustering
//     Clustering to check.
//   expected_clusters
//     List of distinct clusters that we expect to see, where each cluster is
//     given as a list of vertex IDs.
bool CheckClustering(
    const i::Clustering& clustering,
    const ClusterList& expected_clusters) {
  std::unordered_map<uintE, std::set<uintE>> actual_clusters_map;
  actual_clusters_map.reserve(expected_clusters.size());
  for (size_t v = 0; v < clustering.size(); v++) {
    const uintE cluster_id{clustering[v]};
    if (cluster_id != i::kUnclustered) {
      actual_clusters_map[cluster_id].emplace(v);
    }
  }
  ClusterList actual_clusters;
  for (auto& cluster_kv : actual_clusters_map) {
    actual_clusters.emplace(std::move(cluster_kv.second));
  }

  if (actual_clusters != expected_clusters) {
    std::cerr << "Clusters don't match. Actual clustering:\n" <<
      i::ClusteringToString(clustering) << '\n';
    return false;
  } else {
    return true;
  }
}

// Checks that `DetermineUnclusteredType` identifies hubs and outliers as
// expected.
//
// Arguments:
//   graph
//     Input graph.
//   clustering
//     Valid clustering on the graph.
//   expected_hubs
//     List of vertices that we expect to be hubs.
//   expected_outliers
//     List of vertices that we expect to be outliers.
void CheckUnclusteredVertices(
    symmetric_graph<symmetric_vertex, pbbslib::empty>* graph,
    const i::Clustering& clustering,
    const VertexList& expected_hubs,
    const VertexList& expected_outliers) {
  for (const auto v : expected_hubs) {
    EXPECT_EQ(clustering[v], i::kUnclustered);
    EXPECT_EQ(
        i::DetermineUnclusteredType(clustering, graph->get_vertex(v), v),
        i::UnclusteredType::kHub);
  }
  for (const auto v : expected_outliers) {
    EXPECT_EQ(clustering[v], i::kUnclustered);
    EXPECT_EQ(
        i::DetermineUnclusteredType(clustering, graph->get_vertex(v), v),
        i::UnclusteredType::kOutlier);
  }
}

}  // namespace

TEST(ScanSubroutines, NullGraph) {
  const size_t kNumVertices{0};
  const std::unordered_set<UndirectedEdge> kEdges{};
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};

  const ii::NeighborOrder neighbor_order{&graph};
  EXPECT_THAT(neighbor_order, IsEmpty());

  const auto core_order{ii::ComputeCoreOrder(neighbor_order)};
  EXPECT_THAT(core_order, IsEmpty());
}

TEST(ScanSubroutines, EmptyGraph) {
  const size_t kNumVertices{6};
  const std::unordered_set<UndirectedEdge> kEdges{};
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};

  const ii::NeighborOrder neighbor_order{&graph};
  EXPECT_EQ(neighbor_order.size(), kNumVertices);
  for (const auto& vertex_order : neighbor_order) {
    EXPECT_THAT(vertex_order, IsEmpty());
  }

  const auto core_order{ii::ComputeCoreOrder(neighbor_order)};
  ASSERT_EQ(core_order.size(), 2);
  EXPECT_THAT(core_order[0], IsEmpty());
  EXPECT_THAT(core_order[1], IsEmpty());
}

TEST(ScanSubroutines, BasicUsage) {
  // Graph diagram:
  //     0 --- 1 -- 2 -- 5
  //           |   /|
  //           | /  |
  //           3 -- 4
  const size_t kNumVertices{6};
  const std::unordered_set<UndirectedEdge> kEdges{
    {0, 1},
    {1, 2},
    {1, 3},
    {2, 3},
    {2, 4},
    {2, 5},
    {3, 4},
  };
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};

  const ii::NeighborOrder neighbor_order{&graph};
  ASSERT_EQ(neighbor_order.size(), kNumVertices);
  EXPECT_THAT(
      neighbor_order[0],
      ElementsAre(MakeEdgeSimilarity(0, 1, 2.0 / sqrt(8))));
  EXPECT_THAT(
      neighbor_order[1],
      ElementsAre(
        MakeEdgeSimilarity(1, 3, 3.0 / sqrt(16)),
        MakeEdgeSimilarity(1, 0, 2.0 / sqrt(8)),
        MakeEdgeSimilarity(1, 2, 3.0 / sqrt(20))));
  EXPECT_THAT(
      neighbor_order[2],
      ElementsAre(
        MakeEdgeSimilarity(2, 3, 4.0 / sqrt(20)),
        MakeEdgeSimilarity(2, 4, 3.0 / sqrt(15)),
        MakeEdgeSimilarity(2, 1, 3.0 / sqrt(20)),
        MakeEdgeSimilarity(2, 5, 2.0 / sqrt(10))));
  EXPECT_THAT(
      neighbor_order[3],
      ElementsAre(
        MakeEdgeSimilarity(3, 2, 4.0 / sqrt(20)),
        MakeEdgeSimilarity(3, 4, 3.0 / sqrt(12)),
        MakeEdgeSimilarity(3, 1, 3.0 / sqrt(16))));
  EXPECT_THAT(
      neighbor_order[4],
      ElementsAre(
        MakeEdgeSimilarity(4, 3, 3.0 / sqrt(12)),
        MakeEdgeSimilarity(4, 2, 3.0 / sqrt(15))));
  EXPECT_THAT(
      neighbor_order[5],
      ElementsAre(MakeEdgeSimilarity(5, 2, 2.0 / sqrt(10))));

  {
    const auto core_order{ii::ComputeCoreOrder(neighbor_order)};
    EXPECT_EQ(core_order.size(), 6);
    EXPECT_THAT(core_order[0], IsEmpty());
    EXPECT_THAT(core_order[1], IsEmpty());
    ASSERT_EQ(core_order[2].size(), 6);
    EXPECT_THAT(
        core_order[2].slice(0, 2),
        UnorderedElementsAre(
          MakeCoreThreshold(2, 4.0 / sqrt(20)),
          MakeCoreThreshold(3, 4.0 / sqrt(20))));
    EXPECT_THAT(
        core_order[2].slice(2, core_order[2].size()),
        ElementsAre(
          MakeCoreThreshold(4, 3.0 / sqrt(12)),
          MakeCoreThreshold(1, 3.0 / sqrt(16)),
          MakeCoreThreshold(0, 2.0 / sqrt(8)),
          MakeCoreThreshold(5, 2.0 / sqrt(10))));
    ASSERT_EQ(core_order[3].size(), 4);
    EXPECT_EQ(core_order[3][0], MakeCoreThreshold(3, 3.0 / sqrt(12)));
    EXPECT_THAT(
        core_order[3].slice(1, 3),
        UnorderedElementsAre(
          MakeCoreThreshold(2, 3.0 / sqrt(15)),
          MakeCoreThreshold(4, 3.0 / sqrt(15))));
    EXPECT_THAT(
        core_order[3].slice(3, core_order[3].size()),
        ElementsAre(MakeCoreThreshold(1, 2.0 / sqrt(8))));
    ASSERT_EQ(core_order[4].size(), 3);
    EXPECT_EQ(core_order[4][0], MakeCoreThreshold(3, 3.0 / sqrt(16)));
    EXPECT_THAT(
        core_order[4].slice(1, core_order[4].size()),
        UnorderedElementsAre(
          MakeCoreThreshold(1, 3.0 / sqrt(20)),
          MakeCoreThreshold(2, 3.0 / sqrt(20))));
    EXPECT_THAT(
        core_order[5], ElementsAre(MakeCoreThreshold(2, 2.0 / sqrt(10))));
  }

  {
    const ii::CoreOrder core_order{neighbor_order};
    {
      const uint64_t kMu{2};
      const float kEpsilon{0.5};
      const pbbs::sequence<uintE> cores{core_order.GetCores(kMu, kEpsilon)};
      EXPECT_THAT(cores, UnorderedElementsAre(0, 1, 2, 3, 4, 5));
    }
    {
      const uint64_t kMu{2};
      const float kEpsilon{0.8};
      const pbbs::sequence<uintE> cores{core_order.GetCores(kMu, kEpsilon)};
      EXPECT_THAT(cores, UnorderedElementsAre(2, 3, 4));
    }
    {
      const uint64_t kMu{2};
      const float kEpsilon{0.88};
      const pbbs::sequence<uintE> cores{core_order.GetCores(kMu, kEpsilon)};
      EXPECT_THAT(cores, UnorderedElementsAre(2, 3));
    }
    {
      const uint64_t kMu{2};
      const float kEpsilon{0.9};
      const pbbs::sequence<uintE> cores{core_order.GetCores(kMu, kEpsilon)};
      EXPECT_THAT(cores, IsEmpty());
    }
  }
}

TEST(ScanSubroutines, DisconnectedGraph) {
  // Graph diagram:
  //     0 -- 1    2    3 -- 4 -- 5
  const size_t kNumVertices{6};
  const std::unordered_set<UndirectedEdge> kEdges{
    {0, 1},
    {3, 4},
    {4, 5},
  };
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};

  const ii::NeighborOrder neighbor_order{&graph};
  ASSERT_EQ(neighbor_order.size(), kNumVertices);
  EXPECT_THAT(neighbor_order[0], ElementsAre(MakeEdgeSimilarity(0, 1, 1.0)));
  EXPECT_THAT(neighbor_order[1], ElementsAre(MakeEdgeSimilarity(1, 0, 1.0)));
  EXPECT_THAT(neighbor_order[2], IsEmpty());
  EXPECT_THAT(
      neighbor_order[3],
      ElementsAre(MakeEdgeSimilarity(3, 4, 2.0 / sqrt(6))));
  EXPECT_THAT(
      neighbor_order[4],
      UnorderedElementsAre(
        MakeEdgeSimilarity(4, 3, 2.0 / sqrt(6)),
        MakeEdgeSimilarity(4, 5, 2.0 / sqrt(6))));
  EXPECT_THAT(
      neighbor_order[5],
      ElementsAre(MakeEdgeSimilarity(5, 4, 2.0 / sqrt(6))));

  const auto core_order{ii::ComputeCoreOrder(neighbor_order)};
  EXPECT_EQ(core_order.size(), 4);
  EXPECT_THAT(core_order[0], IsEmpty());
  EXPECT_THAT(core_order[1], IsEmpty());
  ASSERT_EQ(core_order[2].size(), 5);
  EXPECT_THAT(
      core_order[2].slice(0, 2),
      UnorderedElementsAre(
        MakeCoreThreshold(0, 1.0),
        MakeCoreThreshold(1, 1.0)));
  EXPECT_THAT(
      core_order[2].slice(2, core_order[2].size()),
      UnorderedElementsAre(
        MakeCoreThreshold(3, 2.0 / sqrt(6)),
        MakeCoreThreshold(4, 2.0 / sqrt(6)),
        MakeCoreThreshold(5, 2.0 / sqrt(6))));
  EXPECT_THAT(core_order[3], ElementsAre(MakeCoreThreshold(4, 2.0 / sqrt(6))));
}

TEST(Cluster, NullGraph) {
  const size_t kNumVertices{0};
  const std::unordered_set<UndirectedEdge> kEdges{};
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};

  const i::Index index{&graph};
  constexpr float kMu{2};
  constexpr float kEpsilon{0.5};
  i::Clustering clustering{index.Cluster(kMu, kEpsilon)};
  EXPECT_THAT(clustering, IsEmpty());
}

TEST(Cluster, EmptyGraph) {
  const size_t kNumVertices{6};
  const std::unordered_set<UndirectedEdge> kEdges{};
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};

  const i::Index index{&graph};
  constexpr float kMu{2};
  constexpr float kEpsilon{0.5};
  i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

  const ClusterList kExpectedClusters{};
  const VertexList kExpectedHubs{};
  const VertexList kExpectedOutliers{0, 1, 2, 3, 4, 5};
  EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
  CheckUnclusteredVertices(
      &graph, clustering, kExpectedHubs, kExpectedOutliers);
}

TEST(Cluster, BasicUsage) {
  // Graph diagram with structural similarity scores labeled:
  //       .71    .67  .63
  //     0 --- 1 ---- 2 -- 5
  //           |    / |
  //       .75 |  .89 | .77
  //           | /    |
  //           3 ---- 4
  //             .87
  const size_t kNumVertices{6};
  const std::unordered_set<UndirectedEdge> kEdges{
    {0, 1},
    {1, 2},
    {1, 3},
    {2, 3},
    {2, 4},
    {2, 5},
    {3, 4},
  };
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};
  const i::Index index{&graph};

  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.5};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{0, 1, 2, 3, 4, 5}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.7};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{0, 1, 2, 3, 4}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{5};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.73};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{1, 2, 3, 4}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{0, 5};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.88};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{2, 3}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{0, 1, 4, 5};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.95};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{0, 1, 2, 3, 4, 5};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{4};
    constexpr float kEpsilon{0.7};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{1, 2, 3, 4}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{0, 5};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
}

TEST(Cluster, DisconnectedGraph) {
  // Graph diagram with structural similarity scores labeled:
  //       1.0            .82  .82
  //     0 -- 1    2    3 -- 4 -- 5
  const size_t kNumVertices{6};
  const std::unordered_set<UndirectedEdge> kEdges{
    {0, 1},
    {3, 4},
    {4, 5},
  };
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};
  const i::Index index{&graph};

  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.95};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{0, 1}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{2, 3, 4, 5};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.8};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{0, 1}, {3, 4, 5}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{2};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{3};
    constexpr float kEpsilon{0.8};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{3, 4, 5}};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{0, 1, 2};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
  {
    constexpr uint64_t kMu{4};
    constexpr float kEpsilon{0.8};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{};
    const VertexList kExpectedHubs{};
    const VertexList kExpectedOutliers{0, 1, 2, 3, 4, 5};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
}

TEST(Cluster, TwoClusterGraph) {
  // Graph diagram with structural similarity scores labeled:
  //           2                   6
  //     .87 /   \ .87       .87 /   \ .87
  //        /     \             /     \.
  // 0 --- 1 ----- 3 --- 4 --- 5 ----- 7 --- 8
  //   .71    .75    .58   .58    .75   .71
  const size_t kNumVertices{9};
  const std::unordered_set<UndirectedEdge> kEdges{
    {0, 1},
    {1, 2},
    {1, 3},
    {2, 3},
    {3, 4},
    {4, 5},
    {5, 6},
    {5, 7},
    {6, 7},
    {7, 8},
  };
  auto graph{gt::MakeUnweightedSymmetricGraph(kNumVertices, kEdges)};
  const i::Index index{&graph};
  {
    constexpr uint64_t kMu{2};
    constexpr float kEpsilon{0.73};
    const i::Clustering clustering{index.Cluster(kMu, kEpsilon)};

    const ClusterList kExpectedClusters{{1, 2, 3}, {5, 6, 7}};
    const VertexList kExpectedHubs{4};
    const VertexList kExpectedOutliers{0, 8};
    EXPECT_TRUE(CheckClustering(clustering, kExpectedClusters));
    CheckUnclusteredVertices(
        &graph, clustering, kExpectedHubs, kExpectedOutliers);
  }
}
