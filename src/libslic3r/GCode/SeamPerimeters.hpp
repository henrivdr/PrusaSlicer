#ifndef libslic3r_SeamPerimeters_hpp_
#define libslic3r_SeamPerimeters_hpp_

#include <tcbspan/span.hpp>

#include "libslic3r/GCode/SeamPainting.hpp"
#include "libslic3r/KDTreeIndirect.hpp"

#include "libslic3r/GCode/SeamShells.hpp"

namespace Slic3r {
    class Layer;
}

namespace Slic3r::Seams::ModelInfo {
class Painting;
}

namespace Slic3r::Seams::Perimeters {
enum class AngleType;
enum class PointType;
enum class PointClassification;
struct Perimeter;
struct PerimeterParams;

struct LayerInfo
{
    static LayerInfo create(
        const Slic3r::Layer &object_layer, std::size_t index, const double elephant_foot_compensation
    );

    AABBTreeLines::LinesDistancer<Linef> distancer;
    std::optional<AABBTreeLines::LinesDistancer<Linef>> previous_distancer;
    std::size_t index;
    double height{};
    double slice_z{};
    double elephant_foot_compensation;
};

using LayerInfos = std::vector<LayerInfo>;

/**
 * @brief Construct LayerInfo for each of the provided layers.
 */
LayerInfos get_layer_infos(
    tcb::span<const Slic3r::Layer* const> object_layers, const double elephant_foot_compensation
);
} // namespace Slic3r::Seams::Perimeters

namespace Slic3r::Seams::Perimeters::Impl {


/**
 * @brief Split edges between points into multiple points if there is a painted point anywhere on
 * the edge.
 *
 * The edge will be split by points no more than max_distance apart.
 * Smaller max_distance -> more points.
 *
 * @return All the points (original and added) in order along the edges.
 */
std::vector<Vec2d> oversample_painted(
    const std::vector<Vec2d> &points,
    const std::function<bool(Vec3f, double)> &is_painted,
    const double slice_z,
    const double max_distance
);

/**
 * @brief Call Duglas-Peucker for consecutive points of the same type.
 *
 * It never removes the first point and last point.
 *
 * @param tolerance Douglas-Peucker epsilon.
 */
std::pair<std::vector<Vec2d>, std::vector<PointType>> remove_redundant_points(
    const std::vector<Vec2d> &points,
    const std::vector<PointType> &point_types,
    const double tolerance
);

} // namespace Slic3r::Seams::Perimeters::Impl

namespace Slic3r::Seams::Perimeters {

enum class AngleType { convex, concave, smooth };

enum class PointType { enforcer, blocker, common };

enum class PointClassification { overhang, embedded, common };

struct PerimeterParams
{
    double elephant_foot_compensation{};
    double oversampling_max_distance{};
    double embedding_threshold{};
    double overhang_threshold{};
    double convex_threshold{};
    double concave_threshold{};
    double painting_radius{};
    double simplification_epsilon{};
    double smooth_angle_arm_length{};
    double sharp_angle_arm_length{};
};

struct Perimeter
{
    struct IndexToCoord
    {
        double operator()(const size_t index, size_t dim) const;

        tcb::span<const Vec2d> positions;
    };

    using PointTree = KDTreeIndirect<2, double, IndexToCoord>;
    using OptionalPointTree = std::optional<PointTree>;

    struct PointTrees
    {
        OptionalPointTree embedded_points;
        OptionalPointTree common_points;
        OptionalPointTree overhanging_points;
    };

    Perimeter() = default;

    Perimeter(
        const double slice_z,
        const std::size_t layer_index,
        std::vector<Vec2d> &&positions,
        std::vector<double> &&angles,
        std::vector<PointType> &&point_types,
        std::vector<PointClassification> &&point_classifications,
        std::vector<AngleType> &&angle_types
    );

    static Perimeter create(
        const Polygon &polygon,
        const ModelInfo::Painting &painting,
        const LayerInfo &layer_info,
        const PerimeterParams &params
    );

    static Perimeter create_degenerate(
        std::vector<Vec2d> &&points, const double slice_z, const std::size_t layer_index
    );

    bool is_degenerate{false};
    double slice_z{};
    std::size_t layer_index{};
    std::vector<Vec2d> positions{};
    std::vector<double> angles{};
    IndexToCoord index_to_coord{};
    std::vector<PointType> point_types{};
    std::vector<PointClassification> point_classifications{};
    std::vector<AngleType> angle_types{};

    PointTrees enforced_points{};
    PointTrees common_points{};
    PointTrees blocked_points{};
};

/**
 * @brief Create a Perimeter for each polygon in each of the shells.
 */
Shells::Shells<Perimeter> create_perimeters(
    const std::vector<Shells::Shell<Polygon>> &shells,
    const std::vector<LayerInfo> &layer_infos,
    const ModelInfo::Painting &painting,
    const PerimeterParams &params
);

inline std::size_t get_layer_count(
    const Shells::Shells<> &shells
) {
    std::size_t layer_count{0};
    for (const Shells::Shell<> &shell : shells) {
        for (const Shells::Slice<>& slice : shell) {
            if (slice.layer_index >= layer_count) {
                layer_count = slice.layer_index + 1;
            }
        }
    }
    return layer_count;
}
} // namespace Slic3r::Seams::Perimeters

#endif // libslic3r_SeamPerimeters_hpp_
