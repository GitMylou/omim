#pragma once

#include "drape_frontend/batchers_pool.hpp"
#include "drape_frontend/color_constants.hpp"
#include "drape_frontend/tile_key.hpp"

#include "drape/color.hpp"
#include "drape/glsl_types.hpp"
#include "drape/glstate.hpp"
#include "drape/render_bucket.hpp"
#include "drape/texture_manager.hpp"

#include "traffic/traffic_info.hpp"

#include "indexer/feature_decl.hpp"

#include "geometry/polyline2d.hpp"

#include "std/array.hpp"
#include "std/map.hpp"
#include "std/set.hpp"
#include "std/string.hpp"
#include "std/vector.hpp"
#include "std/unordered_map.hpp"

namespace df
{

enum class RoadClass : uint8_t
{
  Class0,
  Class1,
  Class2
};

int constexpr kRoadClass0ZoomLevel = 10;
int constexpr kRoadClass1ZoomLevel = 12;
int constexpr kRoadClass2ZoomLevel = 15;

struct TrafficSegmentID
{
  MwmSet::MwmId m_mwmId;
  traffic::TrafficInfo::RoadSegmentId m_segmentId;

  TrafficSegmentID(MwmSet::MwmId const & mwmId,
                   traffic::TrafficInfo::RoadSegmentId const & segmentId)
    : m_mwmId(mwmId)
    , m_segmentId(segmentId)
  {}

  inline bool operator<(TrafficSegmentID const & r) const
  {
    if (m_mwmId == r.m_mwmId)
      return m_segmentId < r.m_segmentId;
    return m_mwmId < r.m_mwmId;
  }

  inline bool operator==(TrafficSegmentID const & r) const
  {
    return (m_mwmId == r.m_mwmId && m_segmentId == r.m_segmentId);
  }

  inline bool operator!=(TrafficSegmentID const & r) const { return !(*this == r); }
};

struct TrafficSegmentGeometry
{
  m2::PolylineD m_polyline;
  RoadClass m_roadClass;

  TrafficSegmentGeometry(m2::PolylineD && polyline, RoadClass const & roadClass)
    : m_polyline(move(polyline))
    , m_roadClass(roadClass)
  {}
};

using TrafficSegmentsGeometry = map<MwmSet::MwmId, vector<pair<traffic::TrafficInfo::RoadSegmentId,
                                                               TrafficSegmentGeometry>>>;
using TrafficSegmentsColoring = map<MwmSet::MwmId, traffic::TrafficInfo::Coloring>;

struct TrafficRenderData
{
  dp::GLState m_state;
  drape_ptr<dp::RenderBucket> m_bucket;
  TileKey m_tileKey;
  MwmSet::MwmId m_mwmId;
  RoadClass m_roadClass;

  TrafficRenderData(dp::GLState const & state) : m_state(state) {}
};

struct TrafficStaticVertex
{
  using TPosition = glsl::vec3;
  using TNormal = glsl::vec4;
  using TTexCoord = glsl::vec2;

  TrafficStaticVertex() = default;
  TrafficStaticVertex(TPosition const & position, TNormal const & normal,
                      TTexCoord const & colorTexCoord)
    : m_position(position)
    , m_normal(normal)
    , m_colorTexCoord(colorTexCoord)
  {}

  TPosition m_position;
  TNormal m_normal;
  TTexCoord m_colorTexCoord;
};

struct TrafficLineStaticVertex
{
  using TPosition = glsl::vec3;
  using TTexCoord = glsl::vec2;

  TrafficLineStaticVertex() = default;
  TrafficLineStaticVertex(TPosition const & position, TTexCoord const & colorTexCoord)
    : m_position(position)
    , m_colorTexCoord(colorTexCoord)
  {}

  TPosition m_position;
  TTexCoord m_colorTexCoord;
};

using TrafficTexCoords = unordered_map<size_t, glsl::vec2>;

class TrafficGenerator final
{
public:
  using TFlushRenderDataFn = function<void (TrafficRenderData && renderData)>;

  explicit TrafficGenerator(TFlushRenderDataFn flushFn)
    : m_flushRenderDataFn(flushFn)
    , m_providerTriangles(1 /* stream count */, 0 /* vertices count*/)
    , m_providerLines(1 /* stream count */, 0 /* vertices count*/)
  {}

  void Init();
  void ClearGLDependentResources();

  void FlushSegmentsGeometry(TileKey const & tileKey, TrafficSegmentsGeometry const & geom,
                             ref_ptr<dp::TextureManager> textures);
  void UpdateColoring(TrafficSegmentsColoring const & coloring);

  void ClearCache();
  void ClearCache(MwmSet::MwmId const & mwmId);
  void InvalidateTexturesCache();

  static df::ColorConstant GetColorBySpeedGroup(traffic::SpeedGroup const & speedGroup);

private:
  struct TrafficBatcherKey
  {
    TrafficBatcherKey() = default;
    TrafficBatcherKey(MwmSet::MwmId const & mwmId, TileKey const & tileKey, RoadClass const & roadClass)
      : m_mwmId(mwmId)
      , m_tileKey(tileKey)
      , m_roadClass(roadClass)
    {}

    MwmSet::MwmId m_mwmId;
    TileKey m_tileKey;
    RoadClass m_roadClass;
  };

  struct TrafficBatcherKeyComparator
  {
    bool operator() (TrafficBatcherKey const & lhs, TrafficBatcherKey const & rhs) const
    {
      if (lhs.m_mwmId == rhs.m_mwmId)
      {
        if (lhs.m_tileKey.EqualStrict(rhs.m_tileKey))
          return lhs.m_roadClass < rhs.m_roadClass;
        return lhs.m_tileKey.LessStrict(rhs.m_tileKey);
      }
      return lhs.m_mwmId < rhs.m_mwmId;
    }
  };

  void GenerateSegment(dp::TextureManager::ColorRegion const & colorRegion,
                       m2::PolylineD const & polyline, m2::PointD const & tileCenter,
                       bool generateCaps, float depth, vector<TrafficStaticVertex> & staticGeometry);
  void GenerateLineSegment(dp::TextureManager::ColorRegion const & colorRegion,
                           m2::PolylineD const & polyline, m2::PointD const & tileCenter, float depth,
                           vector<TrafficLineStaticVertex> & staticGeometry);
  void FillColorsCache(ref_ptr<dp::TextureManager> textures);

  void FlushGeometry(TrafficBatcherKey const & key, dp::GLState const & state,
                     drape_ptr<dp::RenderBucket> && buffer);

  TrafficSegmentsColoring m_coloring;

  array<dp::TextureManager::ColorRegion, static_cast<size_t>(traffic::SpeedGroup::Count)> m_colorsCache;
  bool m_colorsCacheValid = false;

  drape_ptr<BatchersPool<TrafficBatcherKey, TrafficBatcherKeyComparator>> m_batchersPool;
  TFlushRenderDataFn m_flushRenderDataFn;

  dp::AttributeProvider m_providerTriangles;
  dp::AttributeProvider m_providerLines;
};

} // namespace df
