#include <array>

inline constexpr uint32_t kRawTileNumber = 9;
inline constexpr uint32_t kClusterSize = 6;
inline constexpr uint32_t kChannelsInPixel = 3;
inline constexpr uint32_t kTestCount = 3;
inline constexpr uint32_t kImageSize = 128 * 128;
inline constexpr uint32_t kClusterImageSize = 64 * 64;

using ClusterData = std::array<uint8_t, kClusterSize>;

struct ChannelLimits {
  uint8_t min_red;
  uint8_t max_red;
  uint8_t min_green;
  uint8_t max_green;
  uint8_t min_blue;
  uint8_t max_blue;
  uint8_t min_alpha;
  uint8_t max_alpha;
};

inline constexpr std::array<std::pair<uint32_t, ClusterData>, kTestCount>
    kClusters = {{{0, {0, 0, 2, 0, 138, 139}},
                  {64, {0, 0, 9, 6, 134, 119}},
                  {128, {44, 40, 63, 59, 230, 95}}}};

inline constexpr std::array<std::pair<uint32_t, ChannelLimits>, kTestCount>
    kLimits = {{{0, {15, 18, 0, 0, 18, 41, 255, 255}},
                {64, {0, 0, 0, 0, 0, 2, 255, 255}},
                {512, {5, 6, 34, 36, 182, 196, 255, 255}}}};
