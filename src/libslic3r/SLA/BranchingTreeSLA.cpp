#include "BranchingTreeSLA.hpp"

#include "libslic3r/Execution/ExecutionTBB.hpp"

#include "libslic3r/KDTreeIndirect.hpp"

#include "SupportTreeUtils.hpp"
#include "BranchingTree/PointCloud.hpp"

#include "Pad.hpp"

#include <map>

namespace Slic3r { namespace sla {

class BranchingTreeBuilder: public branchingtree::Builder {
    SupportTreeBuilder &m_builder;
    const SupportableMesh  &m_sm;
    const branchingtree::PointCloud &m_cloud;

    std::set<int /*ground node_id that was already processed*/> m_ground_mem;

    // Scaling of the input value 'widening_factor:<0, 1>' to produce resonable
    // widening behaviour
    static constexpr double WIDENING_SCALE = 0.02;

    double get_radius(const branchingtree::Node &j)
    {
        double w = WIDENING_SCALE * m_sm.cfg.pillar_widening_factor * j.weight;

        return std::min(m_sm.cfg.base_radius_mm, double(j.Rmin) + w);
    }

    std::vector<size_t>  m_unroutable_pinheads;

    void build_subtree(size_t root)
    {
        traverse(m_cloud, root, [this](const branchingtree::Node &node) {
            if (node.left >= 0 && node.right >= 0) {
                auto nparent = m_cloud.get(node.id);
                auto nleft = m_cloud.get(node.left);
                auto nright = m_cloud.get(node.right);
                Vec3d from1d = nleft.pos.cast<double>();
                Vec3d from2d = nright.pos.cast<double>();
                Vec3d tod    = nparent.pos.cast<double>();
                double mergeR = get_radius(nparent);
                double leftR  = get_radius(nleft);
                double rightR = get_radius(nright);

                m_builder.add_diffbridge(from1d, tod, leftR, mergeR);
                m_builder.add_diffbridge(from2d, tod, rightR, mergeR);
                m_builder.add_junction(tod, mergeR);
            } else if (int child = node.left + node.right + 1; child >= 0) {
                auto from = m_cloud.get(child);
                auto to   = m_cloud.get(node.id);
                auto tod  = to.pos.cast<double>();
                double toR = get_radius(to);
                m_builder.add_diffbridge(from.pos.cast<double>(),
                                         tod,
                                         get_radius(from),
                                         toR);
                m_builder.add_junction(tod, toR);
            }
        });
    }

    void discard_subtree(size_t root)
    {
        // Discard all the support points connecting to this branch.
        traverse(m_cloud, root, [this](const branchingtree::Node &node) {
            int suppid_parent = m_cloud.get_leaf_id(node.id);
            int suppid_left   = m_cloud.get_leaf_id(node.left);
            int suppid_right  = m_cloud.get_leaf_id(node.right);
            if (suppid_parent >= 0)
                m_unroutable_pinheads.emplace_back(suppid_parent);
            if (suppid_left >= 0)
                m_unroutable_pinheads.emplace_back(suppid_left);
            if (suppid_right >= 0)
                m_unroutable_pinheads.emplace_back(suppid_right);
        });
    }

public:
    BranchingTreeBuilder(SupportTreeBuilder          &builder,
                     const SupportableMesh       &sm,
                     const branchingtree::PointCloud &cloud)
        : m_builder{builder}, m_sm{sm}, m_cloud{cloud}
    {}

    bool add_bridge(const branchingtree::Node &from,
                    const branchingtree::Node &to) override;

    bool add_merger(const branchingtree::Node &node,
                    const branchingtree::Node &closest,
                    const branchingtree::Node &merge_node) override;

    bool add_ground_bridge(const branchingtree::Node &from,
                           const branchingtree::Node &/*to*/) override;

    bool add_mesh_bridge(const branchingtree::Node &from,
                         const branchingtree::Node &to) override;

    void report_unroutable(const branchingtree::Node &j) override
    {
        BOOST_LOG_TRIVIAL(error) << "Cannot route junction at " << j.pos.x()
                                 << " " << j.pos.y() << " " << j.pos.z();

        // Discard all the support points connecting to this branch.
        discard_subtree(j.id);
    }

    const std::vector<size_t>& unroutable_pinheads() const
    {
        return m_unroutable_pinheads;
    }

    bool is_valid() const override { return !m_builder.ctl().stopcondition(); }
};

bool BranchingTreeBuilder::add_bridge(const branchingtree::Node &from,
                                  const branchingtree::Node &to)
{
    Vec3d fromd = from.pos.cast<double>(), tod = to.pos.cast<double>();
    double fromR = get_radius(from), toR = get_radius(to);
    Beam beam{Ball{fromd, fromR}, Ball{tod, toR}};
    auto   hit = beam_mesh_hit(ex_tbb, m_sm.emesh, beam,
                               m_sm.cfg.safety_distance_mm);

    bool ret = hit.distance() > (tod - fromd).norm();

    return ret;
}

bool BranchingTreeBuilder::add_merger(const branchingtree::Node &node,
                                      const branchingtree::Node &closest,
                                      const branchingtree::Node &merge_node)
{
    Vec3d from1d = node.pos.cast<double>(),
          from2d = closest.pos.cast<double>(),
          tod    = merge_node.pos.cast<double>();

    double mergeR   = get_radius(merge_node);
    double nodeR    = get_radius(node);
    double closestR = get_radius(closest);
    Beam beam1{Ball{from1d, nodeR}, Ball{tod, mergeR}};
    Beam beam2{Ball{from2d, closestR}, Ball{tod, mergeR}};

    auto sd = m_sm.cfg.safety_distance_mm ;
    auto hit1 = beam_mesh_hit(ex_tbb, m_sm.emesh, beam1, sd);
    auto hit2 = beam_mesh_hit(ex_tbb, m_sm.emesh, beam2, sd);

    bool ret = hit1.distance() > (tod - from1d).norm() &&
               hit2.distance() > (tod - from2d).norm();

    return ret;
}

bool BranchingTreeBuilder::add_ground_bridge(const branchingtree::Node &from,
                                             const branchingtree::Node &to)
{
    bool ret = false;

    auto it = m_ground_mem.find(from.id);
    if (it == m_ground_mem.end()) {
        ret = search_ground_route(ex_tbb, m_builder, m_sm,
                                   sla::Junction{from.pos.cast<double>(),
                                                 get_radius(from)},
                                   get_radius(to)).first;

        // Remember that this node was tested if can go to ground, don't
        // test it with any other destination ground point because
        // it is unlikely that search_ground_route would find a better solution
        m_ground_mem.insert(from.id);
    }

    if (ret) {
        build_subtree(from.id);
    }

    return ret;
}

bool BranchingTreeBuilder::add_mesh_bridge(const branchingtree::Node &from,
                                           const branchingtree::Node &to)
{
    sla::Junction fromj = {from.pos.cast<double>(), get_radius(from)};

    auto anchor = m_sm.cfg.ground_facing_only ?
                      std::optional<Anchor>{} : // If no mesh connections are allowed
                      calculate_anchor_placement(ex_tbb, m_sm, fromj,
                                                 to.pos.cast<double>());

    if (anchor) {
        sla::Junction toj = {anchor->junction_point(), anchor->r_back_mm};

        auto hit = beam_mesh_hit(ex_tbb, m_sm.emesh,
                                 Beam{{fromj.pos, fromj.r}, {toj.pos, toj.r}}, 0.);

        if (hit.distance() > distance(fromj.pos, toj.pos)) {
            m_builder.add_diffbridge(fromj.pos, toj.pos, fromj.r, toj.r);
            m_builder.add_anchor(*anchor);

            build_subtree(from.id);
        } else {
            anchor.reset();
        }
    }

    return bool(anchor);
}

void create_branching_tree(SupportTreeBuilder &builder, const SupportableMesh &sm)
{
    auto coordfn = [&sm](size_t id, size_t dim) { return sm.pts[id].pos(dim); };
    KDTreeIndirect<3, float, decltype (coordfn)> tree{coordfn, sm.pts.size()};

    auto nondup_idx = non_duplicate_suppt_indices(tree, sm.pts, 0.1);
    std::vector<std::optional<Head>> heads(nondup_idx.size());
    auto leafs = reserve_vector<branchingtree::Node>(nondup_idx.size());

    execution::for_each(
        ex_tbb, size_t(0), nondup_idx.size(),
        [&sm, &heads, &nondup_idx, &builder](size_t i) {
            if (!builder.ctl().stopcondition())
                heads[i] = calculate_pinhead_placement(ex_seq, sm, nondup_idx[i]);
        },
        execution::max_concurrency(ex_tbb)
    );

    if (builder.ctl().stopcondition())
        return;

    for (auto &h : heads)
        if (h && h->is_valid()) {
            leafs.emplace_back(h->junction_point().cast<float>(), h->r_back_mm);
            h->id = leafs.size() - 1;
            builder.add_head(h->id, *h);
        }

    auto &its = *sm.emesh.get_triangle_mesh();
    ExPolygons bedpolys = {branchingtree::make_bed_poly(its)};

    auto props = branchingtree::Properties{}
                     .bed_shape(bedpolys)
                     .ground_level(sla::ground_level(sm))
                     .max_slope(sm.cfg.bridge_slope)
                     .max_branch_length(sm.cfg.max_bridge_length_mm);

    auto meshpts = sm.cfg.ground_facing_only ?
                       std::vector<branchingtree::Node>{} :
                       branchingtree::sample_mesh(its,
                                                  props.sampling_radius());

    auto bedpts  = branchingtree::sample_bed(props.bed_shape(),
                                             props.ground_level(),
                                             props.sampling_radius());

    branchingtree::PointCloud nodes{std::move(meshpts), std::move(bedpts),
                                    std::move(leafs), props};

    BranchingTreeBuilder vbuilder{builder, sm, nodes};
    branchingtree::build_tree(nodes, vbuilder);

    for (size_t id : vbuilder.unroutable_pinheads())
        builder.head(id).invalidate();

}

}} // namespace Slic3r::sla
