///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "ViewerImpl.hpp"
#include "../include/GCodeInputData.hpp"
#include "Shaders.hpp"
#include "OpenGLUtils.hpp"
#include "Utils.hpp"

#include <map>
#include <assert.h>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace libvgcode {

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

// Rounding up.
// 1.5 is rounded to 2
// 1.49 is rounded to 1
// 0.5 is rounded to 1,
// 0.49 is rounded to 0
// -0.5 is rounded to 0,
// -0.51 is rounded to -1,
// -1.5 is rounded to -1.
// -1.51 is rounded to -2.
// If input is not a valid float (it is infinity NaN or if it does not fit)
// the float to int conversion produces a max int on Intel and +-max int on ARM.
template<typename I>
inline IntegerOnly<I, I> fast_round_up(double a)
{
    // Why does Java Math.round(0.49999999999999994) return 1?
    // https://stackoverflow.com/questions/9902968/why-does-math-round0-49999999999999994-return-1
    return a == 0.49999999999999994 ? I(0) : I(floor(a + 0.5));
}

// Round to a bin with minimum two digits resolution.
// Equivalent to conversion to string with sprintf(buf, "%.2g", value) and conversion back to float, but faster.
static float round_to_bin(const float value)
{
//    assert(value >= 0);
    constexpr float const scale[5]     = { 100.f,  1000.f,  10000.f,  100000.f,  1000000.f };
    constexpr float const invscale[5]  = { 0.01f,  0.001f,  0.0001f,  0.00001f,  0.000001f };
    constexpr float const threshold[5] = { 0.095f, 0.0095f, 0.00095f, 0.000095f, 0.0000095f };
    // Scaling factor, pointer to the tables above.
    int                   i = 0;
    // While the scaling factor is not yet large enough to get two integer digits after scaling and rounding:
    for (; value < threshold[i] && i < 4; ++i);
    // At least on MSVC std::round() calls a complex function, which is pretty expensive.
    // our fast_round_up is much cheaper and it could be inlined.
//    return std::round(value * scale[i]) * invscale[i];
    double a = value * scale[i];
    assert(std::abs(a) < double(std::numeric_limits<int64_t>::max()));
    return fast_round_up<int64_t>(a) * invscale[i];
}

static Mat4x4 inverse(const Mat4x4& m)
{
    // ref: https://stackoverflow.com/questions/1148309/inverting-a-4x4-matrix

    Mat4x4 inv;

    inv[0] = m[5] * m[10] * m[15] -
             m[5] * m[11] * m[14] -
             m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];

    inv[4] = -m[4] * m[10] * m[15] +
             m[4] * m[11] * m[14] +
             m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];

    inv[8] = m[4] * m[9] * m[15] -
             m[4] * m[11] * m[13] -
             m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4] * m[9] * m[14] +
               m[4] * m[10] * m[13] +
               m[8] * m[5] * m[14] -
               m[8] * m[6] * m[13] -
               m[12] * m[5] * m[10] +
               m[12] * m[6] * m[9];

    inv[1] = -m[1] * m[10] * m[15] +
             m[1] * m[11] * m[14] +
             m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];

    inv[5] = m[0] * m[10] * m[15] -
             m[0] * m[11] * m[14] -
             m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0] * m[9] * m[15] +
             m[0] * m[11] * m[13] +
             m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];

    inv[13] = m[0] * m[9] * m[14] -
             m[0] * m[10] * m[13] -
             m[8] * m[1] * m[14] +
             m[8] * m[2] * m[13] +
             m[12] * m[1] * m[10] -
             m[12] * m[2] * m[9];

    inv[2] = m[1] * m[6] * m[15] -
             m[1] * m[7] * m[14] -
             m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0] * m[6] * m[15] +
             m[0] * m[7] * m[14] +
             m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];

    inv[10] = m[0] * m[5] * m[15] -
              m[0] * m[7] * m[13] -
              m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0] * m[5] * m[14] +
              m[0] * m[6] * m[13] +
              m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
             m[1] * m[7] * m[10] +
             m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
             m[0] * m[7] * m[9] +
             m[4] * m[1] * m[11] -
             m[4] * m[3] * m[9] -
             m[8] * m[1] * m[7] +
             m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    assert(det != 0.0f);

    det = 1.0f / det;

    std::array<float, 16> ret = {};
    for (int i = 0; i < 16; ++i) {
        ret[i] = inv[i] * det;
    }

    return ret;
}

std::string check_shader(GLuint handle)
{
    std::string ret;
    GLint params;
    glsafe(glGetShaderiv(handle, GL_COMPILE_STATUS, &params));
    if (params == GL_FALSE) {
        glsafe(glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &params));
        ret.resize(params);
        glsafe(glGetShaderInfoLog(handle, params, &params, ret.data()));
    }
    return ret;
}

std::string check_program(GLuint handle)
{
    std::string ret;
    GLint params;
    glsafe(glGetProgramiv(handle, GL_LINK_STATUS, &params));
    if (params == GL_FALSE) {
        glsafe(glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &params));
        ret.resize(params);
        glsafe(glGetProgramInfoLog(handle, params, &params, ret.data()));
    }
    return ret;
}

unsigned int init_shader(const std::string& shader_name, const char* vertex_shader, const char* fragment_shader)
{
    const GLuint vs_id = glCreateShader(GL_VERTEX_SHADER);
    glcheck();
    glsafe(glShaderSource(vs_id, 1, &vertex_shader, nullptr));
    glsafe(glCompileShader(vs_id));
    std::string res = check_shader(vs_id);
    if (!res.empty()) {
        glsafe(glDeleteShader(vs_id));
        throw std::runtime_error("LibVGCode: Unable to compile vertex shader:\n" + shader_name + "\n" + res + "\n");
    }

    const GLuint fs_id = glCreateShader(GL_FRAGMENT_SHADER);
    glcheck();
    glsafe(glShaderSource(fs_id, 1, &fragment_shader, nullptr));
    glsafe(glCompileShader(fs_id));
    res = check_shader(fs_id);
    if (!res.empty()) {
        glsafe(glDeleteShader(vs_id));
        glsafe(glDeleteShader(fs_id));
        throw std::runtime_error("LibVGCode: Unable to compile fragment shader:\n" + shader_name + "\n" + res + "\n");
    }

    const GLuint shader_id = glCreateProgram();
    glcheck();
    glsafe(glAttachShader(shader_id, vs_id));
    glsafe(glAttachShader(shader_id, fs_id));
    glsafe(glLinkProgram(shader_id));
    res = check_program(shader_id);
    if (!res.empty()) {
        glsafe(glDetachShader(shader_id, vs_id));
        glsafe(glDetachShader(shader_id, fs_id));
        glsafe(glDeleteShader(vs_id));
        glsafe(glDeleteShader(fs_id));
        glsafe(glDeleteProgram(shader_id));
        throw std::runtime_error("LibVGCode: Unable to link shader program:\n" + shader_name + "\n" + res + "\n");
    }

    glsafe(glDetachShader(shader_id, vs_id));
    glsafe(glDetachShader(shader_id, fs_id));
    glsafe(glDeleteShader(vs_id));
    glsafe(glDeleteShader(fs_id));
    return shader_id;
}

static void delete_textures(unsigned int& id)
{
    if (id != 0) {
        glsafe(glDeleteTextures(1, &id));
        id = 0;
    }
}

static void delete_buffers(unsigned int& id)
{
    if (id != 0) {
        glsafe(glDeleteBuffers(1, &id));
        id = 0;
    }
}

//
// Palette used to render extrusion moves by extrusion roles
// EViewType: FeatureType
//
const std::map<EGCodeExtrusionRole, Color> ViewerImpl::DEFAULT_EXTRUSION_ROLES_COLORS{ {
    { EGCodeExtrusionRole::None,                       { 230, 179, 179 } },
    { EGCodeExtrusionRole::Perimeter,                  { 255, 230,  77 } },
    { EGCodeExtrusionRole::ExternalPerimeter,          { 255, 125,  56 } },
    { EGCodeExtrusionRole::OverhangPerimeter,          {  31,  31, 255 } },
    { EGCodeExtrusionRole::InternalInfill,             { 176,  48,  41 } },
    { EGCodeExtrusionRole::SolidInfill,                { 150,  84, 204 } },
    { EGCodeExtrusionRole::TopSolidInfill,             { 240,  64,  64 } },
    { EGCodeExtrusionRole::Ironing,                    { 255, 140, 105 } },
    { EGCodeExtrusionRole::BridgeInfill,               {  77, 128, 186 } },
    { EGCodeExtrusionRole::GapFill,                    { 255, 255, 255 } },
    { EGCodeExtrusionRole::Skirt,                      {   0, 135, 110 } },
    { EGCodeExtrusionRole::SupportMaterial,            {   0, 255,   0 } },
    { EGCodeExtrusionRole::SupportMaterialInterface,   {   0, 128,   0 } },
    { EGCodeExtrusionRole::WipeTower,                  { 179, 227, 171 } },
    { EGCodeExtrusionRole::Custom,                     {  94, 209, 148 } }
} };

//
// Palette used to render options
// EViewType: FeatureType
//
const std::map<EOptionType, Color> ViewerImpl::DEFAULT_OPTIONS_COLORS{ {
    { EOptionType::Travels,       {  56,  72, 155 } },
    { EOptionType::Wipes,         { 255, 255,   0 } },
    { EOptionType::Retractions,   { 205,  34, 214 } },
    { EOptionType::Unretractions, {  73, 173, 207 } },
    { EOptionType::Seams,         { 230, 230, 230 } },
    { EOptionType::ToolChanges,   { 193, 190,  99 } },
    { EOptionType::ColorChanges,  { 218, 148, 139 } },
    { EOptionType::PausePrints,   {  82, 240, 131 } },
    { EOptionType::CustomGCodes,  { 226, 210,  67 } }
} };

void ViewerImpl::init()
{
    if (m_initialized)
        return;

    if (!load_opengl())
        throw std::runtime_error("LibVGCode was unable to initialize OpenGL\n");

    if (!check_opengl_version())
        throw std::runtime_error("LibVGCode requires an active OpenGL context based on OpenGL 3.2 or higher:\n");

    // segments shader
    m_segments_shader_id = init_shader("segments", Segments_Vertex_Shader, Segments_Fragment_Shader);

    m_uni_segments_view_matrix_id            = glGetUniformLocation(m_segments_shader_id, "view_matrix");
    m_uni_segments_projection_matrix_id      = glGetUniformLocation(m_segments_shader_id, "projection_matrix");
    m_uni_segments_camera_position_id        = glGetUniformLocation(m_segments_shader_id, "camera_position");
    m_uni_segments_positions_tex_id          = glGetUniformLocation(m_segments_shader_id, "positionsTex");
    m_uni_segments_height_width_angle_tex_id = glGetUniformLocation(m_segments_shader_id, "heightWidthAngleTex");
    m_uni_segments_colors_tex_id             = glGetUniformLocation(m_segments_shader_id, "colorsTex");
    m_uni_segments_segment_index_tex_id      = glGetUniformLocation(m_segments_shader_id, "segmentIndexTex");
    glcheck();
    assert(m_uni_segments_view_matrix_id != -1 &&
           m_uni_segments_projection_matrix_id != -1 &&
           m_uni_segments_camera_position_id != -1 &&
           m_uni_segments_positions_tex_id != -1 &&
           m_uni_segments_height_width_angle_tex_id != -1 &&
           m_uni_segments_colors_tex_id != -1 &&
           m_uni_segments_segment_index_tex_id != -1);

    m_segment_template.init();

    // options shader
    m_options_shader_id = init_shader("options", Options_Vertex_Shader, Options_Fragment_Shader);

    m_uni_options_view_matrix_id            = glGetUniformLocation(m_options_shader_id, "view_matrix");
    m_uni_options_projection_matrix_id      = glGetUniformLocation(m_options_shader_id, "projection_matrix");
    m_uni_options_positions_tex_id          = glGetUniformLocation(m_options_shader_id, "positionsTex");
    m_uni_options_height_width_angle_tex_id = glGetUniformLocation(m_options_shader_id, "heightWidthAngleTex");
    m_uni_options_colors_tex_id             = glGetUniformLocation(m_options_shader_id, "colorsTex");
    m_uni_options_segment_index_tex_id      = glGetUniformLocation(m_options_shader_id, "segmentIndexTex");
    glcheck();
    assert(m_uni_options_view_matrix_id != -1 &&
           m_uni_options_projection_matrix_id != -1 &&
           m_uni_options_positions_tex_id != -1 &&
           m_uni_options_height_width_angle_tex_id != -1 &&
           m_uni_options_colors_tex_id != -1 &&
           m_uni_options_segment_index_tex_id != -1);

    m_option_template.init(16);

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    // cog marker shader
    m_cog_marker_shader_id = init_shader("cog_marker", Cog_Marker_Vertex_Shader, Cog_Marker_Fragment_Shader);

    m_uni_cog_marker_world_center_position = glGetUniformLocation(m_cog_marker_shader_id, "world_center_position");
    m_uni_cog_marker_scale_factor          = glGetUniformLocation(m_cog_marker_shader_id, "scale_factor");
    m_uni_cog_marker_view_matrix           = glGetUniformLocation(m_cog_marker_shader_id, "view_matrix");
    m_uni_cog_marker_projection_matrix     = glGetUniformLocation(m_cog_marker_shader_id, "projection_matrix");
    glcheck();
    assert(m_uni_cog_marker_world_center_position != -1 &&
           m_uni_cog_marker_scale_factor != -1 &&
           m_uni_cog_marker_view_matrix != -1 &&
           m_uni_cog_marker_projection_matrix != -1);

    m_cog_marker.init(32, 1.0f);

    // tool marker shader
    m_tool_marker_shader_id = init_shader("tool_marker", Tool_Marker_Vertex_Shader, Tool_Marker_Fragment_Shader);

    m_uni_tool_marker_world_origin      = glGetUniformLocation(m_tool_marker_shader_id, "world_origin");
    m_uni_tool_marker_scale_factor      = glGetUniformLocation(m_tool_marker_shader_id, "scale_factor");
    m_uni_tool_marker_view_matrix       = glGetUniformLocation(m_tool_marker_shader_id, "view_matrix");
    m_uni_tool_marker_projection_matrix = glGetUniformLocation(m_tool_marker_shader_id, "projection_matrix");
    m_uni_tool_marker_color_base        = glGetUniformLocation(m_tool_marker_shader_id, "color_base");

    glcheck();
    assert(m_uni_tool_marker_world_origin != -1 &&
           m_uni_tool_marker_scale_factor != -1 &&
           m_uni_tool_marker_view_matrix != -1 &&
           m_uni_tool_marker_projection_matrix != -1 &&
           m_uni_tool_marker_color_base != -1);

    m_tool_marker.init(32, 2.0f, 4.0f, 1.0f, 8.0f);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    m_initialized = true;
}

void ViewerImpl::shutdown()
{
    reset();
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_tool_marker.shutdown();
    m_cog_marker.shutdown();
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_option_template.shutdown();
    m_segment_template.shutdown();
    if (m_options_shader_id != 0) {
        glsafe(glDeleteProgram(m_options_shader_id));
        m_options_shader_id = 0;
    }
    if (m_segments_shader_id != 0) {
        glsafe(glDeleteProgram(m_segments_shader_id));
        m_segments_shader_id = 0;
    }
    m_initialized = false;
}

void ViewerImpl::reset()
{
    m_layers.reset();
    m_view_range.reset();
    m_extrusion_roles.reset();
    m_options.clear();
    m_travels_time = { 0.0f, 0.0f };
    m_used_extruders_ids.clear();
    m_vertices.clear();
    m_valid_lines_bitset.clear();
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_cog_marker.reset();
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    m_enabled_segments_count = 0;
    m_enabled_options_count = 0;

    delete_textures(m_enabled_options_tex_id);
    delete_buffers(m_enabled_options_buf_id);

    delete_textures(m_enabled_segments_tex_id);
    delete_buffers(m_enabled_segments_buf_id);

    delete_textures(m_colors_tex_id);
    delete_buffers(m_colors_buf_id);

    delete_textures(m_heights_widths_angles_tex_id);
    delete_buffers(m_heights_widths_angles_buf_id);

    delete_textures(m_positions_tex_id);
    delete_buffers(m_positions_buf_id);
}

void ViewerImpl::load(GCodeInputData&& gcode_data)
{
    if (gcode_data.vertices.empty())
        return;

    reset();

    m_loading = true;

    m_vertices = std::move(gcode_data.vertices);
    m_settings.spiral_vase_mode = gcode_data.spiral_vase_mode;

    m_used_extruders_ids.reserve(m_vertices.size());

    for (size_t i = 0; i < m_vertices.size(); ++i) {
        const PathVertex& v = m_vertices[i];
        m_layers.update(v, static_cast<uint32_t>(i));
        if (v.type == EMoveType::Travel) {
            for (size_t j = 0; j < TIME_MODES_COUNT; ++j) {
                m_travels_time[j] += v.times[j];
            }
        }
        else
            m_extrusion_roles.add(v.role, v.times);

        const EOptionType option_type = move_type_to_option(v.type);
        if (option_type != EOptionType::COUNT)
            m_options.emplace_back(option_type);

        if (v.type == EMoveType::Extrude)
            m_used_extruders_ids.emplace_back(v.extruder_id);

        if (i > 0) {
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
            // updates calculation for center of gravity
            if (v.type == EMoveType::Extrude &&
                v.role != EGCodeExtrusionRole::Skirt &&
                v.role != EGCodeExtrusionRole::SupportMaterial &&
                v.role != EGCodeExtrusionRole::SupportMaterialInterface &&
                v.role != EGCodeExtrusionRole::WipeTower &&
                v.role != EGCodeExtrusionRole::Custom) {
                m_cog_marker.update(0.5f * (v.position + m_vertices[i - 1].position), v.weight);
            }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
        }
    }

    if (!m_layers.empty())
        m_layers.set_view_range(0, static_cast<uint32_t>(m_layers.count()) - 1);

    std::sort(m_options.begin(), m_options.end());
    m_options.erase(std::unique(m_options.begin(), m_options.end()), m_options.end());
    m_options.shrink_to_fit();

    std::sort(m_used_extruders_ids.begin(), m_used_extruders_ids.end());
    m_used_extruders_ids.erase(std::unique(m_used_extruders_ids.begin(), m_used_extruders_ids.end()), m_used_extruders_ids.end());
    m_used_extruders_ids.shrink_to_fit();

    // reset segments visibility bitset
    m_valid_lines_bitset = BitSet<>(m_vertices.size());
    m_valid_lines_bitset.setAll();

    static constexpr const Vec3 ZERO = { 0.0f, 0.0f, 0.0f };

    // buffers to send to gpu
    std::vector<Vec3> positions;
    std::vector<Vec3> heights_widths_angles;
    positions.reserve(m_vertices.size());
    heights_widths_angles.reserve(m_vertices.size());
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        const PathVertex& v = m_vertices[i];
        const EMoveType move_type = v.type;
        const bool prev_line_valid = i > 0 && m_valid_lines_bitset[i - 1];
        const Vec3 prev_line = prev_line_valid ? v.position - m_vertices[i - 1].position : ZERO;
        const bool this_line_valid = i + 1 < m_vertices.size() &&
                                     m_vertices[i + 1].position != v.position &&
                                     m_vertices[i + 1].type == move_type &&
                                     move_type != EMoveType::Seam;
        const Vec3 this_line = this_line_valid ? m_vertices[i + 1].position - v.position : ZERO;

        if (this_line_valid) {
            // there is a valid path between point i and i+1.
        }
        else {
            // the connection is invalid, there should be no line rendered, ever
            m_valid_lines_bitset.reset(i);
        }

        Vec3 position = v.position;
        if (move_type == EMoveType::Extrude)
            // push down extrusion vertices by half height to render them at the right z
            position[2] -= 0.5f * v.height;
        positions.emplace_back(position);

        const float angle = std::atan2(prev_line[0] * this_line[1] - prev_line[1] * this_line[0], dot(prev_line, this_line));
        heights_widths_angles.push_back({ v.height, v.width, angle });
    }

    if (!positions.empty()) {
        int old_bound_texture = 0;
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &old_bound_texture));

        // create and fill positions buffer
        glsafe(glGenBuffers(1, &m_positions_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_positions_buf_id));
        glsafe(glBufferData(GL_TEXTURE_BUFFER, positions.size() * sizeof(Vec3), positions.data(), GL_STATIC_DRAW));
        glsafe(glGenTextures(1, &m_positions_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_positions_tex_id));

        // create and fill height, width and angles buffer
        glsafe(glGenBuffers(1, &m_heights_widths_angles_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_heights_widths_angles_buf_id));
        glsafe(glBufferData(GL_TEXTURE_BUFFER, heights_widths_angles.size() * sizeof(Vec3), heights_widths_angles.data(), GL_STATIC_DRAW));
        glsafe(glGenTextures(1, &m_heights_widths_angles_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_heights_widths_angles_tex_id));

        // create (but do not fill) colors buffer (data is set in update_colors())
        glsafe(glGenBuffers(1, &m_colors_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_colors_buf_id));
        glsafe(glGenTextures(1, &m_colors_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_colors_tex_id));

        // create (but do not fill) enabled segments buffer (data is set in update_enabled_entities())
        glsafe(glGenBuffers(1, &m_enabled_segments_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_segments_buf_id));
        glsafe(glGenTextures(1, &m_enabled_segments_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_enabled_segments_tex_id));

        // create (but do not fill) enabled options buffer (data is set in update_enabled_entities())
        glsafe(glGenBuffers(1, &m_enabled_options_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_options_buf_id));
        glsafe(glGenTextures(1, &m_enabled_options_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_enabled_options_tex_id));

        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, old_bound_texture));
    }

    update_view_full_range();
    m_view_range.set_visible(m_view_range.get_enabled());
    update_enabled_entities();
    update_colors();

    m_loading = false;
}

void ViewerImpl::update_enabled_entities()
{
    if (m_vertices.empty())
        return;

    std::vector<uint32_t> enabled_segments;
    std::vector<uint32_t> enabled_options;
    Interval range = m_view_range.get_visible();

    // when top layer only visualization is enabled, we need to render
    // all the toolpaths in the other layers as grayed, so extend the range
    // to contain them
    if (m_settings.top_layer_only_view_range)
        range[0] = m_view_range.get_full()[0];

    // to show the options at the current tool marker position we need to extend the range by one extra step
    if (m_vertices[range[1]].is_option() && range[1] < static_cast<uint32_t>(m_vertices.size()) - 1)
        ++range[1];

    if (m_settings.spiral_vase_mode) {
        // when spiral vase mode is enabled and only one layer is shown, extend the range by one step
        const Interval& layers_range = m_layers.get_view_range();
        if (layers_range[0] > 0 && layers_range[0] == layers_range[1])
            --range[0];
    }

    for (size_t i = range[0]; i < range[1]; ++i) {
        const PathVertex& v = m_vertices[i];

        if (!m_valid_lines_bitset[i] && !v.is_option())
            continue;
        if (v.is_travel()) {
            if (!m_settings.options_visibility.at(EOptionType::Travels))
                continue;
        }
        else if (v.is_wipe()) {
            if (!m_settings.options_visibility.at(EOptionType::Wipes))
                continue;
        }
        else if (v.is_option()) {
            if (!m_settings.options_visibility.at(move_type_to_option(v.type)))
                continue;
        }
        else if (v.is_extrusion()) {
            if (!m_settings.extrusion_roles_visibility.at(v.role))
                continue;
        }
        else
            continue;

        if (v.is_option())
            enabled_options.push_back(static_cast<uint32_t>(i));
        else
            enabled_segments.push_back(static_cast<uint32_t>(i));
    }

    m_enabled_segments_count = enabled_segments.size();
    m_enabled_options_count = enabled_options.size();

    if (m_enabled_segments_count > 0)
        m_enabled_segments_range.set(enabled_segments.front(), enabled_segments.back());
    else
        m_enabled_segments_range.reset();
    if (m_enabled_options_count > 0)
        m_enabled_options_range.set(enabled_options.front(), enabled_options.back());
    else
        m_enabled_options_range.reset();

    // update gpu buffer for enabled segments
    assert(m_enabled_segments_buf_id > 0);
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_segments_buf_id));
    if (!enabled_segments.empty())
        glsafe(glBufferData(GL_TEXTURE_BUFFER, enabled_segments.size() * sizeof(uint32_t), enabled_segments.data(), GL_STATIC_DRAW));
    else
        glsafe(glBufferData(GL_TEXTURE_BUFFER, 0, nullptr, GL_STATIC_DRAW));

    // update gpu buffer for enabled options
    assert(m_enabled_options_buf_id > 0);
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_options_buf_id));
    if (!enabled_options.empty())
        glsafe(glBufferData(GL_TEXTURE_BUFFER, enabled_options.size() * sizeof(uint32_t), enabled_options.data(), GL_STATIC_DRAW));
    else
        glsafe(glBufferData(GL_TEXTURE_BUFFER, 0, nullptr, GL_STATIC_DRAW));

    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));

    m_settings.update_enabled_entities = false;
}

static float encode_color(const Color& color) {
    const int r = static_cast<int>(color[0]);
    const int g = static_cast<int>(color[1]);
    const int b = static_cast<int>(color[2]);
    const int i_color = r << 16 | g << 8 | b;
    return static_cast<float>(i_color);
}

void ViewerImpl::update_colors()
{
    if (m_colors_buf_id == 0)
      return;

    if (!m_used_extruders_ids.empty()) {
        // ensure that the number of defined tool colors matches the max id of the used extruders 
        const size_t max_used_extruder_id = 1 + static_cast<size_t>(m_used_extruders_ids.back());
        const size_t tool_colors_size = m_tool_colors.size();
        if (m_tool_colors.size() < max_used_extruder_id) {
            for (size_t i = 0; i < max_used_extruder_id - tool_colors_size; ++i) {
                m_tool_colors.emplace_back(DUMMY_COLOR);
            }
        }
    }

    update_color_ranges();

    const size_t top_layer_id = m_settings.top_layer_only_view_range ? m_layers.get_view_range()[1] : 0;
    const bool color_top_layer_only = m_view_range.get_full()[1] != m_view_range.get_visible()[1];
    std::vector<float> colors(m_vertices.size());
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        colors[i] = (color_top_layer_only && m_vertices[i].layer_id < top_layer_id &&
                     (!m_settings.spiral_vase_mode || i != m_view_range.get_enabled()[0])) ?
            encode_color(DUMMY_COLOR) : encode_color(get_vertex_color(m_vertices[i]));
    }

    // update gpu buffer for colors
    assert(m_colors_buf_id > 0);
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_colors_buf_id));
    glsafe(glBufferData(GL_TEXTURE_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW));
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));

    m_settings.update_colors = false;
}

void ViewerImpl::render(const Mat4x4& view_matrix, const Mat4x4& projection_matrix)
{
    // ensure that the render does take place while loading the data
    if (m_loading)
        return;

    if (m_settings.update_view_full_range)
        update_view_full_range();

    if (m_settings.update_enabled_entities)
        update_enabled_entities();

    if (m_settings.update_colors)
        update_colors();

    const Mat4x4 inv_view_matrix = inverse(view_matrix);
    const Vec3 camera_position = { inv_view_matrix[12], inv_view_matrix[13], inv_view_matrix[14] };
    render_segments(view_matrix, projection_matrix, camera_position);
    render_options(view_matrix, projection_matrix);

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    if (m_settings.options_visibility.at(EOptionType::ToolMarker))
        render_tool_marker(view_matrix, projection_matrix);
    if (m_settings.options_visibility.at(EOptionType::CenterOfGravity))
        render_cog_marker(view_matrix, projection_matrix);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
}

void ViewerImpl::set_view_type(EViewType type)
{
    m_settings.view_type = type;
    m_settings.update_colors = true;
}

void ViewerImpl::set_time_mode(ETimeMode mode)
{
    m_settings.time_mode = mode;
    m_settings.update_colors = true;
}

void ViewerImpl::set_layers_view_range(Interval::value_type min, Interval::value_type max)
{
    min = std::clamp<Interval::value_type>(min, 0, m_layers.count() - 1);
    max = std::clamp<Interval::value_type>(max, 0, m_layers.count() - 1);
    m_layers.set_view_range(min, max);
    // force immediate update of the full range
    update_view_full_range();
    m_view_range.set_visible(m_view_range.get_enabled());
    m_settings.update_enabled_entities = true;
    m_settings.update_colors = true;
}

void ViewerImpl::toggle_top_layer_only_view_range()
{
    m_settings.top_layer_only_view_range = !m_settings.top_layer_only_view_range;
    update_view_full_range();
    m_view_range.set_visible(m_view_range.get_enabled());
    m_settings.update_enabled_entities = true;
    m_settings.update_colors = true;
}

std::vector<ETimeMode> ViewerImpl::get_time_modes() const
{
    std::vector<ETimeMode> ret;
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i) {
        if (std::accumulate(m_vertices.begin(), m_vertices.end(), 0.0f,
            [i](float a, const PathVertex& v) { return a + v.times[i]; }) > 0.0f)
            ret.push_back(static_cast<ETimeMode>(i));
    }
    return ret;
}

AABox ViewerImpl::get_bounding_box(EBBoxType type) const
{
    assert(type < EBBoxType::COUNT);
    Vec3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
    Vec3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const PathVertex& v : m_vertices) {
        if (type != EBBoxType::Full && (v.type != EMoveType::Extrude || v.width == 0.0f || v.height == 0.0f))
            continue;
        else if (type == EBBoxType::ExtrusionNoCustom && v.role == EGCodeExtrusionRole::Custom)
            continue;

        for (int j = 0; j < 3; ++j) {
            min[j] = std::min(min[j], v.position[j]);
            max[j] = std::max(max[j], v.position[j]);
        }
    }
    return { min, max };
}

bool ViewerImpl::is_option_visible(EOptionType type) const
{
    const auto it = m_settings.options_visibility.find(type);
    return (it == m_settings.options_visibility.end()) ? false : it->second;
}

void ViewerImpl::toggle_option_visibility(EOptionType type)
{
    auto it = m_settings.options_visibility.find(type);
    if (it != m_settings.options_visibility.end()) {
        it->second = !it->second;
        const Interval old_enabled_range = m_view_range.get_enabled();
        update_view_full_range();
        const Interval& new_enabled_range = m_view_range.get_enabled();
        if (old_enabled_range != new_enabled_range) {
            const Interval& visible_range = m_view_range.get_visible();
            if (old_enabled_range == visible_range)
                m_view_range.set_visible(new_enabled_range);
            else if (m_settings.top_layer_only_view_range && new_enabled_range[0] < visible_range[0])
                m_view_range.set_visible(new_enabled_range[0], visible_range[1]);
        }
        m_settings.update_enabled_entities = true;
        m_settings.update_colors = true;
    }
}

bool ViewerImpl::is_extrusion_role_visible(EGCodeExtrusionRole role) const
{
    const auto it = m_settings.extrusion_roles_visibility.find(role);
    return (it == m_settings.extrusion_roles_visibility.end()) ? false : it->second;
}

void ViewerImpl::toggle_extrusion_role_visibility(EGCodeExtrusionRole role)
{
    auto it = m_settings.extrusion_roles_visibility.find(role);
    if (it != m_settings.extrusion_roles_visibility.end()) {
        it->second = !it->second;
        update_view_full_range();
        m_settings.update_enabled_entities = true;
        m_settings.update_colors = true;
    }
}

void ViewerImpl::set_view_visible_range(Interval::value_type min, Interval::value_type max)
{
    // force update of the full range, to avoid clamping the visible range with full old values
    // when calling m_view_range.set_visible()
    update_view_full_range();
    m_view_range.set_visible(min, max);
    update_enabled_entities();
    m_settings.update_colors = true;
}

float ViewerImpl::get_estimated_time_at(size_t id) const
{
    return std::accumulate(m_vertices.begin(), m_vertices.begin() + id + 1, 0.0f, 
        [this](float a, const PathVertex& v) { return a + v.times[static_cast<size_t>(m_settings.time_mode)]; });
}

Color ViewerImpl::get_vertex_color(const PathVertex& v) const
{
    if (v.type == EMoveType::Noop)
        return DUMMY_COLOR;

    if (v.is_wipe() || v.is_option())
        return get_option_color(move_type_to_option(v.type));

    switch (m_settings.view_type)
    {
    case EViewType::FeatureType:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) : get_extrusion_role_color(v.role);
    }
    case EViewType::Height:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) : m_height_range.get_color_at(v.height);
    }
    case EViewType::Width:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) : m_width_range.get_color_at(v.width);
    }
    case EViewType::Speed:
    {
        return m_speed_range.get_color_at(v.feedrate);
    }
    case EViewType::FanSpeed:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) : m_fan_speed_range.get_color_at(v.fan_speed);
    }
    case EViewType::Temperature:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) : m_temperature_range.get_color_at(v.temperature);
    }
    case EViewType::VolumetricFlowRate:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) : m_volumetric_rate_range.get_color_at(v.volumetric_rate);
    }
    case EViewType::LayerTimeLinear:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) :
            m_layer_time_range[0].get_color_at(m_layers.get_layer_time(m_settings.time_mode, static_cast<size_t>(v.layer_id)));
    }
    case EViewType::LayerTimeLogarithmic:
    {
        return v.is_travel() ? get_option_color(move_type_to_option(v.type)) :
            m_layer_time_range[1].get_color_at(m_layers.get_layer_time(m_settings.time_mode, static_cast<size_t>(v.layer_id)));
    }
    case EViewType::Tool:
    {
        assert(static_cast<size_t>(v.extruder_id) < m_tool_colors.size());
        return m_tool_colors[v.extruder_id];
    }
    case EViewType::ColorPrint:
    {
        return m_layers.layer_contains_colorprint_options(static_cast<size_t>(v.layer_id)) ? DUMMY_COLOR :
            m_tool_colors[static_cast<size_t>(v.color_id) % m_tool_colors.size()];
    }
    default: { break; }
    }

    return DUMMY_COLOR;
}

void ViewerImpl::set_tool_colors(const Palette& colors)
{
    m_tool_colors = colors;
    m_settings.update_colors = true;
}

const Color& ViewerImpl::get_extrusion_role_color(EGCodeExtrusionRole role) const
{
    const auto it = m_extrusion_roles_colors.find(role);
    return (it == m_extrusion_roles_colors.end()) ? DUMMY_COLOR : it->second;
}

void ViewerImpl::set_extrusion_role_color(EGCodeExtrusionRole role, const Color& color)
{
    auto it = m_extrusion_roles_colors.find(role);
    if (it != m_extrusion_roles_colors.end()) {
        it->second = color;
        m_settings.update_colors = true;
    }
}

const Color& ViewerImpl::get_option_color(EOptionType type) const
{
    const auto it = m_options_colors.find(type);
    return (it == m_options_colors.end()) ? DUMMY_COLOR : it->second;
}

void ViewerImpl::set_option_color(EOptionType type, const Color& color)
{
    auto it = m_options_colors.find(type);
    if (it != m_options_colors.end()) {
        it->second = color;
        m_settings.update_colors = true;
    }
}

const ColorRange& ViewerImpl::get_color_range(EViewType type) const
{
    switch (type)
    {
    case EViewType::Height:               { return m_height_range; }
    case EViewType::Width:                { return m_width_range; }
    case EViewType::Speed:                { return m_speed_range; }
    case EViewType::FanSpeed:             { return m_fan_speed_range; }
    case EViewType::Temperature:          { return m_temperature_range; }
    case EViewType::VolumetricFlowRate:   { return m_volumetric_rate_range; }
    case EViewType::LayerTimeLinear:      { return m_layer_time_range[0]; }
    case EViewType::LayerTimeLogarithmic: { return m_layer_time_range[1]; }
    default:                              { return ColorRange::DUMMY_COLOR_RANGE; }
    }
}

void ViewerImpl::set_color_range_palette(EViewType type, const Palette& palette)
{
    switch (type)
    {
    case EViewType::Height:               { m_height_range.set_palette(palette); }
    case EViewType::Width:                { m_width_range.set_palette(palette); }
    case EViewType::Speed:                { m_speed_range.set_palette(palette); }
    case EViewType::FanSpeed:             { m_fan_speed_range.set_palette(palette); }
    case EViewType::Temperature:          { m_temperature_range.set_palette(palette); }
    case EViewType::VolumetricFlowRate:   { m_volumetric_rate_range.set_palette(palette); }
    case EViewType::LayerTimeLinear:      { m_layer_time_range[0].set_palette(palette); }
    case EViewType::LayerTimeLogarithmic: { m_layer_time_range[1].set_palette(palette); }
    default:                              { break; }
    }
    m_settings.update_colors = true;
}

void ViewerImpl::set_travels_radius(float radius)
{
    m_travels_radius = std::clamp(radius, MIN_TRAVELS_RADIUS_MM, MAX_TRAVELS_RADIUS_MM);
    update_heights_widths();
}

void ViewerImpl::set_wipes_radius(float radius)
{
    m_wipes_radius = std::clamp(radius, MIN_WIPES_RADIUS_MM, MAX_WIPES_RADIUS_MM);
    update_heights_widths();
}

static bool is_visible(const PathVertex& v, const Settings& settings)
{
    const EOptionType option_type = move_type_to_option(v.type);
    try
    {
        return (option_type == EOptionType::COUNT) ?
            (v.type == EMoveType::Extrude) ? settings.extrusion_roles_visibility.at(v.role) : false :
            settings.options_visibility.at(option_type);
    }
    catch (...)
    {
        return false;
    }
}

void ViewerImpl::update_view_full_range()
{
    const Interval& layers_range = m_layers.get_view_range();
    const bool travels_visible = m_settings.options_visibility.at(EOptionType::Travels);
    const bool wipes_visible   = m_settings.options_visibility.at(EOptionType::Wipes);

    auto first_it = m_vertices.begin();
    while (first_it != m_vertices.end() &&
           (first_it->layer_id < layers_range[0] || !is_visible(*first_it, m_settings))) {
        ++first_it;
    }

    // If the first vertex is an extrusion, add an extra step to properly detect the first segment
    if (first_it != m_vertices.begin() && first_it->type == EMoveType::Extrude)
        --first_it;

    if (first_it == m_vertices.end())
        m_view_range.set_full(Range());
    else {
        if (travels_visible || wipes_visible) {
            // if the global range starts with a travel/wipe move, extend it to the travel/wipe start
            while (first_it != m_vertices.begin() &&
                   ((travels_visible && first_it->is_travel()) ||
                    (wipes_visible && first_it->is_wipe()))) {
                --first_it;
            }
        }

        auto last_it = first_it;
        while (last_it != m_vertices.end() && last_it->layer_id <= layers_range[1]) {
            ++last_it;
        }
        if (last_it != first_it)
            --last_it;

        // remove disabled trailing options, if any 
        auto rev_first_it = std::make_reverse_iterator(first_it);
        if (rev_first_it != m_vertices.rbegin())
            --rev_first_it;
        auto rev_last_it = std::make_reverse_iterator(last_it);
        if (rev_last_it != m_vertices.rbegin())
            --rev_last_it;

        bool reduced = false;
        while (rev_last_it != rev_first_it && !is_visible(*rev_last_it, m_settings)) {
            ++rev_last_it;
            reduced = true;
        }

        if (reduced && rev_last_it != m_vertices.rend())
            last_it = rev_last_it.base() - 1;

        if (travels_visible || wipes_visible) {
            // if the global range ends with a travel/wipe move, extend it to the travel/wipe end
            while (last_it != m_vertices.end() && last_it + 1 != m_vertices.end() &&
                   ((travels_visible && last_it->is_travel() && (last_it + 1)->is_travel()) ||
                    (wipes_visible && last_it->is_wipe() && (last_it + 1)->is_wipe()))) {
                  ++last_it;
            }
        }

        if (first_it != last_it)
            m_view_range.set_full(std::distance(m_vertices.begin(), first_it), std::distance(m_vertices.begin(), last_it));
        else
            m_view_range.set_full(Range());

        if (m_settings.top_layer_only_view_range) {
            const Interval& full_range = m_view_range.get_full();
            auto top_first_it = m_vertices.begin() + full_range[0];
            bool shortened = false;
            while (top_first_it != m_vertices.end() && (top_first_it->layer_id < layers_range[1] || !is_visible(*top_first_it, m_settings))) {
                ++top_first_it;
                shortened = true;
            }
            if (shortened)
                --top_first_it;

            // when spiral vase mode is enabled and only one layer is shown, extend the range by one step
            if (m_settings.spiral_vase_mode && layers_range[0] > 0 && layers_range[0] == layers_range[1])
                --top_first_it;
            m_view_range.set_enabled(std::distance(m_vertices.begin(), top_first_it), full_range[1]);
        }
        else
            m_view_range.set_enabled(m_view_range.get_full());
    }

    m_settings.update_view_full_range = false;
}

void ViewerImpl::update_color_ranges()
{
    m_width_range.reset();
    m_height_range.reset();
    m_speed_range.reset();
    m_fan_speed_range.reset();
    m_temperature_range.reset();
    m_volumetric_rate_range.reset();
    m_layer_time_range[0].reset(); // ColorRange::EType::Linear
    m_layer_time_range[1].reset(); // ColorRange::EType::Logarithmic

    for (size_t i = 0; i < m_vertices.size(); i++) {
        const PathVertex& v = m_vertices[i];
        if (v.is_extrusion()) {
            m_height_range.update(round_to_bin(v.height));
            if (!v.is_custom_gcode() || m_settings.extrusion_roles_visibility.at(EGCodeExtrusionRole::Custom)) {
                m_width_range.update(round_to_bin(v.width));
                m_volumetric_rate_range.update(round_to_bin(v.volumetric_rate));
            }
            m_fan_speed_range.update(v.fan_speed);
            m_temperature_range.update(v.temperature);
        }
        if ((v.is_travel() && m_settings.options_visibility.at(EOptionType::Travels)) || v.is_extrusion())
            m_speed_range.update(v.feedrate);
    }

    const std::vector<float> times = m_layers.get_times(m_settings.time_mode);
    for (size_t i = 0; i < m_layer_time_range.size(); ++i) {
        for (float t : times) {
            m_layer_time_range[i].update(t);
        }
    }
}

void ViewerImpl::update_heights_widths()
{
    if (m_heights_widths_angles_buf_id == 0)
        return;

    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_heights_widths_angles_buf_id));
    Vec3* buffer = static_cast<Vec3*>(glMapBuffer(GL_TEXTURE_BUFFER, GL_WRITE_ONLY));
    glcheck();

    for (size_t i = 0; i < m_vertices.size(); ++i) {
        const PathVertex& v = m_vertices[i];
        if (v.is_travel()) {
            buffer[i][0] = m_travels_radius;
            buffer[i][1] = m_travels_radius;
        }
        else if (v.is_wipe()) {
            buffer[i][0] = m_wipes_radius;
            buffer[i][1] = m_wipes_radius;
        }
    }

    glsafe(glUnmapBuffer(GL_TEXTURE_BUFFER));
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
}

void ViewerImpl::render_segments(const Mat4x4& view_matrix, const Mat4x4& projection_matrix, const Vec3& camera_position)
{
    if (m_segments_shader_id == 0)
        return;

    int curr_active_texture = 0;
    glsafe(glGetIntegerv(GL_ACTIVE_TEXTURE, &curr_active_texture));
    int curr_bound_texture = 0;
    glsafe(glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &curr_bound_texture));
    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    glcheck();

    glsafe(glActiveTexture(GL_TEXTURE0));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_positions_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, m_positions_buf_id));

    glsafe(glActiveTexture(GL_TEXTURE1));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_heights_widths_angles_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, m_heights_widths_angles_buf_id));

    glsafe(glActiveTexture(GL_TEXTURE2));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_colors_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, m_colors_buf_id));

    glsafe(glActiveTexture(GL_TEXTURE3));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_enabled_segments_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, m_enabled_segments_buf_id));

    glsafe(glUseProgram(m_segments_shader_id));

    glsafe(glUniform1i(m_uni_segments_positions_tex_id, 0));
    glsafe(glUniform1i(m_uni_segments_height_width_angle_tex_id, 1));
    glsafe(glUniform1i(m_uni_segments_colors_tex_id, 2));
    glsafe(glUniform1i(m_uni_segments_segment_index_tex_id, 3));
    glsafe(glUniformMatrix4fv(m_uni_segments_view_matrix_id, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(m_uni_segments_projection_matrix_id, 1, GL_FALSE, projection_matrix.data()));
    glsafe(glUniform3fv(m_uni_segments_camera_position_id, 1, camera_position.data()));

    glsafe(glDisable(GL_CULL_FACE));

    m_segment_template.render(m_enabled_segments_count);

    if (curr_cull_face)
        glsafe(glEnable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, curr_bound_texture));
    glsafe(glActiveTexture(curr_active_texture));
}

void ViewerImpl::render_options(const Mat4x4& view_matrix, const Mat4x4& projection_matrix)
{
    if (m_options_shader_id == 0)
        return;

    int curr_active_texture = 0;
    glsafe(glGetIntegerv(GL_ACTIVE_TEXTURE, &curr_active_texture));
    int curr_bound_texture = 0;
    glsafe(glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &curr_bound_texture));
    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    glcheck();

    glsafe(glActiveTexture(GL_TEXTURE0));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_positions_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, m_positions_buf_id));

    glsafe(glActiveTexture(GL_TEXTURE1));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_heights_widths_angles_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, m_heights_widths_angles_buf_id));

    glsafe(glActiveTexture(GL_TEXTURE2));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_colors_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, m_colors_buf_id));

    glsafe(glActiveTexture(GL_TEXTURE3));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_enabled_options_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, m_enabled_options_buf_id));

    glsafe(glEnable(GL_CULL_FACE));

    glsafe(glUseProgram(m_options_shader_id));

    glsafe(glUniform1i(m_uni_options_positions_tex_id, 0));
    glsafe(glUniform1i(m_uni_options_height_width_angle_tex_id, 1));
    glsafe(glUniform1i(m_uni_options_colors_tex_id, 2));
    glsafe(glUniform1i(m_uni_options_segment_index_tex_id, 3));
    glsafe(glUniformMatrix4fv(m_uni_options_view_matrix_id, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(m_uni_options_projection_matrix_id, 1, GL_FALSE, projection_matrix.data()));

    m_option_template.render(m_enabled_options_count);

    if (!curr_cull_face)
        glsafe(glDisable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, curr_bound_texture));
    glsafe(glActiveTexture(curr_active_texture));
}

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
void ViewerImpl::render_cog_marker(const Mat4x4& view_matrix, const Mat4x4& projection_matrix)
{
    if (m_cog_marker_shader_id == 0)
        return;

    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    const bool curr_depth_test = glIsEnabled(GL_DEPTH_TEST);
    glcheck();

    glsafe(glEnable(GL_CULL_FACE));
    glsafe(glDisable(GL_DEPTH_TEST));

    glsafe(glUseProgram(m_cog_marker_shader_id));

    glsafe(glUniform3fv(m_uni_cog_marker_world_center_position, 1, m_cog_marker.get_position().data()));
    glsafe(glUniform1f(m_uni_cog_marker_scale_factor, m_cog_marker_scale_factor));
    glsafe(glUniformMatrix4fv(m_uni_cog_marker_view_matrix, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(m_uni_cog_marker_projection_matrix, 1, GL_FALSE, projection_matrix.data()));

    m_cog_marker.render();

    if (curr_depth_test)
        glsafe(glEnable(GL_DEPTH_TEST));
    if (!curr_cull_face)
        glsafe(glDisable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
}

void ViewerImpl::render_tool_marker(const Mat4x4& view_matrix, const Mat4x4& projection_matrix)
{
    if (m_tool_marker_shader_id == 0)
        return;

    if (!m_tool_marker.is_enabled())
        return;

    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean curr_depth_mask;
    glsafe(glGetBooleanv(GL_DEPTH_WRITEMASK, &curr_depth_mask));
    const bool curr_blend = glIsEnabled(GL_BLEND);
    glcheck();
    int curr_blend_func;
    glsafe(glGetIntegerv(GL_BLEND_SRC_ALPHA, &curr_blend_func));

    glsafe(glDisable(GL_CULL_FACE));
    glsafe(glDepthMask(GL_FALSE));
    glsafe(glEnable(GL_BLEND));
    glsafe(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    glsafe(glUseProgram(m_tool_marker_shader_id));

    const Vec3& origin = m_tool_marker.get_position();
    const Vec3 offset = { 0.0f, 0.0f, m_tool_marker.get_offset_z() };
    const Vec3 position = origin + offset;
    glsafe(glUniform3fv(m_uni_tool_marker_world_origin, 1, position.data()));
    glsafe(glUniform1f(m_uni_tool_marker_scale_factor, m_tool_marker_scale_factor));
    glsafe(glUniformMatrix4fv(m_uni_tool_marker_view_matrix, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(m_uni_tool_marker_projection_matrix, 1, GL_FALSE, projection_matrix.data()));
    const Color& color = m_tool_marker.get_color();
    glsafe(glUniform4f(m_uni_tool_marker_color_base, color[0], color[1], color[2], m_tool_marker.get_alpha()));

    m_tool_marker.render();

    glsafe(glBlendFunc(GL_SRC_ALPHA, curr_blend_func));
    if (!curr_blend)
        glsafe(glDisable(GL_BLEND));
    if (curr_depth_mask == GL_TRUE)
        glsafe(glDepthMask(GL_TRUE));
    if (curr_cull_face)
        glsafe(glEnable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
}
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

} // namespace libvgcode
