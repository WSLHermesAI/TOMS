// gltf_model.cpp -- see gltf_model.h.
#define CGLTF_IMPLEMENTATION
#include "gltf_model.h"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>   // the implementation is in stb_image_impl.cpp

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace toms::gltf {
namespace {

// ---- small helpers ------------------------------------------------------------------------------

template <class T> int indexOf(const T* item, const T* first) { return item ? int(item - first) : -1; }

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// The extensions we draw. Anything else the file uses is "ignored" (still loads), unless required.
const char* const kSupported[] = {"KHR_materials_unlit", "KHR_texture_transform", "KHR_materials_emissive_strength",
                                  "KHR_mesh_quantization", "EXT_mesh_gpu_instancing", "KHR_materials_pbrSpecularGlossiness",
                                  "KHR_materials_transmission", "KHR_materials_volume", "KHR_lights_punctual"};
bool supported(const std::string& e) {
    for (const char* s : kSupported)
        if (e == s) return true;
    return false;
}

std::vector<float> floats(const cgltf_accessor* a) {
    std::vector<float> out;
    if (!a) return out;
    const size_t n = a->count * cgltf_num_components(a->type);
    out.resize(n);
    cgltf_accessor_unpack_floats(a, out.data(), n);   // normalized / quantized ints, sparse accessors
    return out;
}

template <class V> std::vector<V> vectors(const cgltf_accessor* a, int comps, float fill = 0.0f) {
    std::vector<V> out;
    if (!a) return out;
    const int have = (int)cgltf_num_components(a->type);
    const std::vector<float> f = floats(a);
    out.resize(a->count);
    for (size_t i = 0; i < a->count; i++) {
        V v(fill);
        for (int c = 0; c < comps && c < have; c++) v[c] = f[i * (size_t)have + (size_t)c];
        out[i] = v;
    }
    return out;
}

std::vector<uint8_t> decodeBase64(const char* s, size_t len) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(len * 3 / 4);
    int buf = 0, bits = 0;
    for (size_t i = 0; i < len; i++) {
        const int v = val(s[i]);
        if (v < 0) continue;   // '=' padding, whitespace
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((buf >> bits) & 0xff));
        }
    }
    return out;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::string dirOf(const std::string& path) {
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : path.substr(0, s);
}

void decodeImage(const cgltf_image& src, const std::string& baseDir, Image& out, std::vector<std::string>& warnings) {
    out.name = src.name ? src.name : "";
    std::vector<uint8_t> bytes;
    if (src.buffer_view && src.buffer_view->buffer && src.buffer_view->buffer->data) {
        const uint8_t* p = (const uint8_t*)src.buffer_view->buffer->data + src.buffer_view->offset;
        bytes.assign(p, p + src.buffer_view->size);
    } else if (src.uri && std::strncmp(src.uri, "data:", 5) == 0) {
        const char* comma = std::strchr(src.uri, ',');
        if (comma) bytes = decodeBase64(comma + 1, std::strlen(comma + 1));
    } else if (src.uri) {
        std::string uri = src.uri;
        cgltf_decode_uri(&uri[0]);
        uri.resize(std::strlen(uri.c_str()));
        if (!readFile(baseDir + "/" + uri, bytes)) warnings.push_back("image not found: " + uri);
    }
    if (bytes.empty()) return;
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (!px) {
        warnings.push_back("image could not be decoded (" + std::string(stbi_failure_reason() ? stbi_failure_reason() : "?") +
                           "): " + (src.uri && std::strncmp(src.uri, "data:", 5) != 0 ? src.uri : out.name));
        return;
    }
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + (size_t)w * (size_t)h * 4);
    stbi_image_free(px);
}

TextureRef textureRef(const cgltf_data* d, const cgltf_texture_view& v) {
    TextureRef r;
    r.texture = indexOf(v.texture, d->textures);
    r.uvSet = (int)v.texcoord;
    if (v.has_transform) {
        r.offset = {v.transform.offset[0], v.transform.offset[1]};
        r.scale = {v.transform.scale[0], v.transform.scale[1]};
        r.rotation = v.transform.rotation;
        if (v.transform.has_texcoord) r.uvSet = (int)v.transform.texcoord;
    }
    return r;
}

// Strips and fans to lists.
std::vector<uint32_t> toList(cgltf_primitive_type type, const std::vector<uint32_t>& in, Topology& topo) {
    std::vector<uint32_t> out;
    switch (type) {
    case cgltf_primitive_type_triangle_strip:
        topo = Topology::Triangles;
        for (size_t i = 2; i < in.size(); i++) {
            if (i % 2 == 0) out.insert(out.end(), {in[i - 2], in[i - 1], in[i]});
            else out.insert(out.end(), {in[i - 1], in[i - 2], in[i]});
        }
        return out;
    case cgltf_primitive_type_triangle_fan:
        topo = Topology::Triangles;
        for (size_t i = 2; i < in.size(); i++) out.insert(out.end(), {in[0], in[i - 1], in[i]});
        return out;
    case cgltf_primitive_type_line_strip:
    case cgltf_primitive_type_line_loop:
        topo = Topology::Lines;
        for (size_t i = 1; i < in.size(); i++) out.insert(out.end(), {in[i - 1], in[i]});
        if (type == cgltf_primitive_type_line_loop && in.size() > 2) out.insert(out.end(), {in.back(), in.front()});
        return out;
    case cgltf_primitive_type_lines: topo = Topology::Lines; return in;
    case cgltf_primitive_type_points: topo = Topology::Points; return in;
    default: topo = Topology::Triangles; return in;
    }
}

// No normals in the file: flat normals (the spec), so every triangle gets its own vertices.
template <class T> void expand(std::vector<T>& v, const std::vector<uint32_t>& idx) {
    if (v.empty()) return;
    std::vector<T> out(idx.size());
    for (size_t i = 0; i < idx.size(); i++) out[i] = v[idx[i]];
    v.swap(out);
}
void flatNormals(Primitive& p) {
    expand(p.position, p.indices);
    expand(p.tangent, p.indices);
    expand(p.uv0, p.indices);
    expand(p.uv1, p.indices);
    expand(p.color, p.indices);
    expand(p.joints, p.indices);
    expand(p.weights, p.indices);
    for (MorphTarget& t : p.targets) {
        expand(t.position, p.indices);
        expand(t.normal, p.indices);
        expand(t.tangent, p.indices);
    }
    p.normal.assign(p.position.size(), glm::vec3(0, 0, 1));
    for (size_t i = 0; i + 2 < p.position.size(); i += 3) {
        const glm::vec3 n = glm::cross(p.position[i + 1] - p.position[i], p.position[i + 2] - p.position[i]);
        const float l = glm::length(n);
        const glm::vec3 nn = l > 0 ? n / l : glm::vec3(0, 0, 1);
        p.normal[i] = p.normal[i + 1] = p.normal[i + 2] = nn;
    }
    for (uint32_t i = 0; i < (uint32_t)p.indices.size(); i++) p.indices[i] = i;
}

void readAttribute(const cgltf_attribute& a, Primitive& p) {
    switch (a.type) {
    case cgltf_attribute_type_position: p.position = vectors<glm::vec3>(a.data, 3); break;
    case cgltf_attribute_type_normal: p.normal = vectors<glm::vec3>(a.data, 3); break;
    case cgltf_attribute_type_tangent: p.tangent = vectors<glm::vec4>(a.data, 4, 1.0f); break;
    case cgltf_attribute_type_texcoord:
        if (a.index == 0) p.uv0 = vectors<glm::vec2>(a.data, 2);
        else if (a.index == 1) p.uv1 = vectors<glm::vec2>(a.data, 2);
        break;
    case cgltf_attribute_type_color:
        if (a.index == 0) p.color = vectors<glm::vec4>(a.data, 4, 1.0f);   // rgb: alpha stays 1
        break;
    case cgltf_attribute_type_joints:
        if (a.index == 0) p.joints = vectors<glm::vec4>(a.data, 4);
        break;
    case cgltf_attribute_type_weights:
        if (a.index == 0) p.weights = vectors<glm::vec4>(a.data, 4);
        break;
    default: break;
    }
}

bool convert(const cgltf_data* d, const std::string& baseDir, Model& m, std::string* error) {
    m.generator = d->asset.generator ? d->asset.generator : "";
    for (size_t i = 0; i < d->extensions_used_count; i++) {
        const std::string e = d->extensions_used[i];
        m.extensionsUsed.push_back(e);
        if (!supported(e)) m.ignoredExtensions.push_back(e);
    }
    for (size_t i = 0; i < d->extensions_required_count; i++) {
        const std::string e = d->extensions_required[i];
        if (!supported(e)) {
            if (error) *error = "the file requires the extension " + e + ", which this engine cannot read yet "
                               "(re-export without it, e.g. gltf-transform: no Draco / meshopt / KTX2)";
            return false;
        }
    }

    // Images and textures.
    m.images.resize(d->images_count);
    for (size_t i = 0; i < d->images_count; i++) decodeImage(d->images[i], baseDir, m.images[i], m.warnings);
    m.textures.resize(d->textures_count);
    for (size_t i = 0; i < d->textures_count; i++) {
        const cgltf_texture& t = d->textures[i];
        Texture& o = m.textures[i];
        o.image = indexOf(t.image, d->images);
        if (const cgltf_sampler* s = t.sampler) {
            o.repeatS = (int)s->wrap_s == 10497;
            o.repeatT = (int)s->wrap_t == 10497;
            o.mirrorS = (int)s->wrap_s == 33648;
            o.mirrorT = (int)s->wrap_t == 33648;
            o.nearest = (int)s->mag_filter == 9728;
        }
    }

    // Materials.
    m.materials.resize(d->materials_count);
    for (size_t i = 0; i < d->materials_count; i++) {
        const cgltf_material& s = d->materials[i];
        Material& o = m.materials[i];
        o.name = s.name ? s.name : "";
        if (s.has_pbr_metallic_roughness) {
            const auto& pbr = s.pbr_metallic_roughness;
            o.baseColor = glm::make_vec4(pbr.base_color_factor);
            o.metallic = pbr.metallic_factor;
            o.roughness = pbr.roughness_factor;
            o.baseColorTex = textureRef(d, pbr.base_color_texture);
            o.metalRoughTex = textureRef(d, pbr.metallic_roughness_texture);
        } else if (s.has_pbr_specular_glossiness) {   // older files: diffuse as base colour, no metal
            const auto& sg = s.pbr_specular_glossiness;
            o.baseColor = glm::make_vec4(sg.diffuse_factor);
            o.metallic = 0.0f;
            o.roughness = 1.0f - sg.glossiness_factor;
            o.baseColorTex = textureRef(d, sg.diffuse_texture);
        }
        o.normalTex = textureRef(d, s.normal_texture);
        o.normalScale = s.normal_texture.texture ? s.normal_texture.scale : 1.0f;
        o.occlusionTex = textureRef(d, s.occlusion_texture);
        o.occlusionStrength = s.occlusion_texture.texture ? s.occlusion_texture.scale : 1.0f;
        o.emissiveTex = textureRef(d, s.emissive_texture);
        o.emissive = glm::make_vec3(s.emissive_factor) * (s.has_emissive_strength ? s.emissive_strength.emissive_strength : 1.0f);
        o.alphaMode = s.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                    : s.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
        o.alphaCutoff = s.alpha_cutoff;
        o.doubleSided = s.double_sided;
        o.unlit = s.unlit;
        if (s.has_transmission) o.transmission = s.transmission.transmission_factor;
        if (s.has_volume) o.attenuationColor = glm::make_vec3(s.volume.attenuation_color);
        for (const TextureRef* r : {&o.baseColorTex, &o.emissiveTex})   // colour data: sRGB
            if (r->valid()) m.textures[(size_t)r->texture].srgb = true;
    }

    // Meshes.
    m.meshes.resize(d->meshes_count);
    for (size_t i = 0; i < d->meshes_count; i++) {
        const cgltf_mesh& s = d->meshes[i];
        Mesh& o = m.meshes[i];
        o.name = s.name ? s.name : "";
        o.weights.assign(s.weights, s.weights + s.weights_count);
        for (size_t t = 0; t < s.target_names_count; t++) o.targetNames.push_back(s.target_names[t]);
        for (size_t pi = 0; pi < s.primitives_count; pi++) {
            const cgltf_primitive& sp = s.primitives[pi];
            if (sp.has_draco_mesh_compression) {
                m.warnings.push_back("mesh '" + o.name + "': a Draco-compressed primitive is skipped");
                continue;
            }
            Primitive p;
            p.material = indexOf(sp.material, d->materials);
            for (size_t a = 0; a < sp.attributes_count; a++) readAttribute(sp.attributes[a], p);
            if (p.position.empty()) continue;
            std::vector<uint32_t> idx;
            if (sp.indices) {
                idx.resize(sp.indices->count);
                for (size_t k = 0; k < idx.size(); k++) idx[k] = (uint32_t)cgltf_accessor_read_index(sp.indices, k);
            } else {
                idx.resize(p.position.size());
                for (uint32_t k = 0; k < (uint32_t)idx.size(); k++) idx[k] = k;
            }
            p.indices = toList(sp.type, idx, p.topology);
            for (size_t t = 0; t < sp.targets_count; t++) {
                MorphTarget mt;
                for (size_t a = 0; a < sp.targets[t].attributes_count; a++) {
                    const cgltf_attribute& ta = sp.targets[t].attributes[a];
                    if (ta.type == cgltf_attribute_type_position) mt.position = vectors<glm::vec3>(ta.data, 3);
                    else if (ta.type == cgltf_attribute_type_normal) mt.normal = vectors<glm::vec3>(ta.data, 3);
                    else if (ta.type == cgltf_attribute_type_tangent) mt.tangent = vectors<glm::vec3>(ta.data, 3);
                }
                p.targets.push_back(std::move(mt));
            }
            if (p.normal.empty() && p.topology == Topology::Triangles) flatNormals(p);
            p.boundsMin = p.boundsMax = p.position[0];
            for (const glm::vec3& v : p.position) {
                p.boundsMin = glm::min(p.boundsMin, v);
                p.boundsMax = glm::max(p.boundsMax, v);
            }
            o.primitives.push_back(std::move(p));
        }
        size_t targets = 0;
        for (const Primitive& p : o.primitives) targets = std::max(targets, p.targets.size());
        o.weights.resize(targets, 0.0f);
    }

    // Nodes.
    m.nodes.resize(d->nodes_count);
    for (size_t i = 0; i < d->nodes_count; i++) {
        const cgltf_node& s = d->nodes[i];
        Node& o = m.nodes[i];
        o.name = s.name ? s.name : "";
        o.parent = indexOf(s.parent, d->nodes);
        for (size_t c = 0; c < s.children_count; c++) o.children.push_back(indexOf(s.children[c], d->nodes));
        if (s.has_matrix) {   // decompose: animations and the editor work on T, R, S
            glm::mat4 mt = glm::make_mat4(s.matrix);
            o.translation = glm::vec3(mt[3]);
            glm::vec3 sc(glm::length(glm::vec3(mt[0])), glm::length(glm::vec3(mt[1])), glm::length(glm::vec3(mt[2])));
            if (glm::determinant(glm::mat3(mt)) < 0) sc.x = -sc.x;   // mirrored
            o.scale = sc;
            glm::mat3 rm(glm::vec3(mt[0]) / (sc.x != 0 ? sc.x : 1.0f), glm::vec3(mt[1]) / (sc.y != 0 ? sc.y : 1.0f),
                         glm::vec3(mt[2]) / (sc.z != 0 ? sc.z : 1.0f));
            o.rotation = glm::normalize(glm::quat_cast(rm));
        } else {
            if (s.has_translation) o.translation = glm::make_vec3(s.translation);
            if (s.has_rotation) o.rotation = glm::normalize(glm::quat(s.rotation[3], s.rotation[0], s.rotation[1], s.rotation[2]));
            if (s.has_scale) o.scale = glm::make_vec3(s.scale);
        }
        o.mesh = indexOf(s.mesh, d->meshes);
        o.skin = indexOf(s.skin, d->skins);
        o.weights.assign(s.weights, s.weights + s.weights_count);
        if (s.has_mesh_gpu_instancing) {
            std::vector<glm::vec3> t, sc;
            std::vector<glm::vec4> r;
            size_t count = 0;
            for (size_t a = 0; a < s.mesh_gpu_instancing.attributes_count; a++) {
                const cgltf_attribute& at = s.mesh_gpu_instancing.attributes[a];
                const std::string name = at.name ? at.name : "";
                if (name == "TRANSLATION") t = vectors<glm::vec3>(at.data, 3);
                else if (name == "ROTATION") r = vectors<glm::vec4>(at.data, 4);
                else if (name == "SCALE") sc = vectors<glm::vec3>(at.data, 3, 1.0f);
                if (at.data) count = std::max(count, (size_t)at.data->count);
            }
            for (size_t k = 0; k < count; k++) {
                const glm::vec3 tt = k < t.size() ? t[k] : glm::vec3(0.0f);
                const glm::quat rr = k < r.size() ? glm::normalize(glm::quat(r[k].w, r[k].x, r[k].y, r[k].z)) : glm::quat(1, 0, 0, 0);
                const glm::vec3 ss = k < sc.size() ? sc[k] : glm::vec3(1.0f);
                o.instances.push_back(composeTRS(tt, rr, ss));
            }
        }
    }

    // Cameras. Unnamed ones take their node's name, else the nearest named parent's.
    m.cameras.resize(d->cameras_count);
    for (size_t i = 0; i < d->cameras_count; i++) {
        const cgltf_camera& s = d->cameras[i];
        Camera& o = m.cameras[i];
        o.name = s.name ? s.name : "";
        if (s.type == cgltf_camera_type_orthographic) {
            o.perspective = false;
            o.xmag = s.data.orthographic.xmag;
            o.ymag = s.data.orthographic.ymag;
            o.znear = s.data.orthographic.znear;
            o.zfar = s.data.orthographic.zfar;
        } else {
            o.yfov = s.data.perspective.yfov;
            o.aspectRatio = s.data.perspective.has_aspect_ratio ? s.data.perspective.aspect_ratio : 0.0f;
            o.znear = s.data.perspective.znear;
            o.zfar = s.data.perspective.has_zfar ? s.data.perspective.zfar : 0.0f;
        }
    }
    for (size_t i = 0; i < d->nodes_count; i++) {
        const int cam = indexOf(d->nodes[i].camera, d->cameras);
        m.nodes[i].camera = cam;
        if (cam < 0 || !m.cameras[(size_t)cam].name.empty()) continue;
        for (const cgltf_node* n = &d->nodes[i]; n; n = n->parent)
            if (n->name && *n->name) {
                m.cameras[(size_t)cam].name = n->name;
                break;
            }
    }

    // Lights (KHR_lights_punctual).
    m.lights.resize(d->lights_count);
    for (size_t i = 0; i < d->lights_count; i++) {
        const cgltf_light& s = d->lights[i];
        Light& o = m.lights[i];
        o.name = s.name ? s.name : "";
        o.type = s.type == cgltf_light_type_directional ? Light::Type::Directional
               : s.type == cgltf_light_type_spot ? Light::Type::Spot : Light::Type::Point;
        o.color = glm::make_vec3(s.color);
        o.intensity = s.intensity;
        o.range = s.range;
        o.innerCone = s.spot_inner_cone_angle;
        o.outerCone = s.spot_outer_cone_angle > 0 ? s.spot_outer_cone_angle : o.outerCone;
    }
    for (size_t i = 0; i < d->nodes_count; i++) m.nodes[i].light = indexOf(d->nodes[i].light, d->lights);

    // Skins.
    m.skins.resize(d->skins_count);
    for (size_t i = 0; i < d->skins_count; i++) {
        const cgltf_skin& s = d->skins[i];
        Skin& o = m.skins[i];
        o.name = s.name ? s.name : "";
        for (size_t j = 0; j < s.joints_count; j++) o.joints.push_back(indexOf(s.joints[j], d->nodes));
        o.inverseBind.assign(o.joints.size(), glm::mat4(1.0f));
        if (s.inverse_bind_matrices) {
            const std::vector<float> f = floats(s.inverse_bind_matrices);
            for (size_t j = 0; j < o.joints.size() && (j + 1) * 16 <= f.size(); j++) o.inverseBind[j] = glm::make_mat4(&f[j * 16]);
        }
        if ((int)o.joints.size() > kMaxJoints)
            m.warnings.push_back("skin '" + o.name + "' has " + std::to_string(o.joints.size()) + " joints; only " +
                                 std::to_string(kMaxJoints) + " are animated");
    }

    // Animations.
    m.animations.resize(d->animations_count);
    for (size_t i = 0; i < d->animations_count; i++) {
        const cgltf_animation& s = d->animations[i];
        Animation& o = m.animations[i];
        o.name = s.name ? s.name : "animation " + std::to_string(i);
        o.samplers.resize(s.samplers_count);
        for (size_t k = 0; k < s.samplers_count; k++) {
            const cgltf_animation_sampler& ss = s.samplers[k];
            Sampler& so = o.samplers[k];
            so.input = floats(ss.input);
            so.output = floats(ss.output);
            so.interpolation = ss.interpolation == cgltf_interpolation_type_step ? Interpolation::Step
                             : ss.interpolation == cgltf_interpolation_type_cubic_spline ? Interpolation::CubicSpline
                                                                                         : Interpolation::Linear;
            const size_t keys = so.input.size();
            const size_t per = so.interpolation == Interpolation::CubicSpline ? 3 : 1;
            so.components = keys ? int(so.output.size() / (keys * per)) : 0;
            if (!so.input.empty()) o.duration = std::max(o.duration, so.input.back());
        }
        for (size_t k = 0; k < s.channels_count; k++) {
            const cgltf_animation_channel& c = s.channels[k];
            if (!c.target_node || !c.sampler) continue;
            Channel co;
            co.sampler = indexOf(c.sampler, s.samplers);
            co.node = indexOf(c.target_node, d->nodes);
            switch (c.target_path) {
            case cgltf_animation_path_type_translation: co.path = Path::Translation; break;
            case cgltf_animation_path_type_rotation: co.path = Path::Rotation; break;
            case cgltf_animation_path_type_scale: co.path = Path::Scale; break;
            case cgltf_animation_path_type_weights: co.path = Path::Weights; break;
            default: continue;
            }
            o.channels.push_back(co);
        }
    }

    // Scenes (a file without one: every root node).
    for (size_t i = 0; i < d->scenes_count; i++) {
        Scene sc;
        sc.name = d->scenes[i].name ? d->scenes[i].name : "";
        for (size_t k = 0; k < d->scenes[i].nodes_count; k++) sc.roots.push_back(indexOf(d->scenes[i].nodes[k], d->nodes));
        m.scenes.push_back(sc);
    }
    if (m.scenes.empty()) {
        Scene sc;
        for (size_t i = 0; i < m.nodes.size(); i++)
            if (m.nodes[i].parent < 0) sc.roots.push_back((int)i);
        m.scenes.push_back(sc);
    }
    m.scene = d->scene ? indexOf(d->scene, d->scenes) : 0;
    return true;
}

bool finish(cgltf_result r, cgltf_data* d, const cgltf_options& opt, const std::string& gltfPath, const std::string& baseDir,
            Model& out, std::string* error) {
    if (r != cgltf_result_success) {
        if (error) *error = "not a valid glTF file (cgltf error " + std::to_string((int)r) + ")";
        return false;
    }
    r = cgltf_load_buffers(&opt, d, gltfPath.c_str());
    if (r != cgltf_result_success) {
        cgltf_free(d);
        if (error) *error = "a buffer (.bin) could not be loaded (cgltf error " + std::to_string((int)r) + ")";
        return false;
    }
    if (cgltf_validate(d) != cgltf_result_success) {
        cgltf_free(d);
        if (error) *error = "the file does not validate (an accessor or buffer is out of range)";
        return false;
    }
    out = Model();
    out.path = gltfPath;
    const bool ok = convert(d, baseDir, out, error);
    cgltf_free(d);
    return ok;
}

// ---- animation sampling ---------------------------------------------------------------------------

// Key segment for t: i (and i + 1), with the 0..1 position s inside it.
void locate(const std::vector<float>& in, float t, size_t& i, float& s) {
    if (t <= in.front()) {
        i = 0;
        s = 0;
        return;
    }
    if (t >= in.back()) {
        i = in.size() - 1;
        s = 0;
        return;
    }
    i = size_t(std::upper_bound(in.begin(), in.end(), t) - in.begin()) - 1;
    const float dt = in[i + 1] - in[i];
    s = dt > 0 ? (t - in[i]) / dt : 0.0f;
}

void sampleValue(const Sampler& sm, float t, bool isRotation, std::vector<float>& out) {
    const size_t n = (size_t)sm.components;
    out.assign(n, 0.0f);
    if (sm.input.empty() || n == 0) return;
    size_t i;
    float s;
    locate(sm.input, t, i, s);
    const bool last = i + 1 >= sm.input.size();
    if (sm.interpolation == Interpolation::CubicSpline) {
        auto at = [&](size_t key, int part) { return &sm.output[(key * 3 + (size_t)part) * n]; };   // 0 in, 1 value, 2 out
        if (last || s == 0) {
            for (size_t c = 0; c < n; c++) out[c] = at(i, 1)[c];
        } else {
            const float dt = sm.input[i + 1] - sm.input[i];
            const float s2 = s * s, s3 = s2 * s;
            const float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
            for (size_t c = 0; c < n; c++)
                out[c] = h00 * at(i, 1)[c] + h10 * dt * at(i, 2)[c] + h01 * at(i + 1, 1)[c] + h11 * dt * at(i + 1, 0)[c];
        }
    } else if (sm.interpolation == Interpolation::Step || last || s == 0) {
        for (size_t c = 0; c < n; c++) out[c] = sm.output[i * n + c];
    } else if (isRotation) {
        const float* a = &sm.output[i * n];
        const float* b = &sm.output[(i + 1) * n];
        const glm::quat q = glm::slerp(glm::quat(a[3], a[0], a[1], a[2]), glm::quat(b[3], b[0], b[1], b[2]), s);
        out = {q.x, q.y, q.z, q.w};
    } else {
        for (size_t c = 0; c < n; c++) out[c] = sm.output[i * n + c] * (1 - s) + sm.output[(i + 1) * n + c] * s;
    }
}

}  // namespace

// ---- Model ------------------------------------------------------------------------------------------

size_t Model::vertexCount() const {
    size_t n = 0;
    for (const Mesh& m : meshes)
        for (const Primitive& p : m.primitives) n += p.position.size();
    return n;
}

size_t Model::triangleCount() const {
    size_t n = 0;
    for (const Mesh& m : meshes)
        for (const Primitive& p : m.primitives)
            if (p.topology == Topology::Triangles) n += p.indices.size() / 3;
    return n;
}

size_t Model::morphTargetCount() const {
    size_t n = 0;
    for (const Mesh& m : meshes) n += m.weights.size();
    return n;
}

size_t Model::instanceCount() const {
    size_t n = 0;
    for (const Node& node : nodes) n += node.instances.size();
    return n;
}

bool loadModel(const std::string& path, Model& out, std::string* error) {
    cgltf_options opt{};
    cgltf_data* d = nullptr;
    const cgltf_result r = cgltf_parse_file(&opt, path.c_str(), &d);
    if (r == cgltf_result_file_not_found || r == cgltf_result_io_error) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    return finish(r, d, opt, path, dirOf(path), out, error);
}

bool loadModelFromMemory(const void* data, size_t size, const std::string& baseDir, Model& out, std::string* error) {
    cgltf_options opt{};
    cgltf_data* d = nullptr;
    const cgltf_result r = cgltf_parse(&opt, data, size, &d);
    return finish(r, d, opt, baseDir + "/model.gltf", baseDir, out, error);
}

glm::mat4 composeTRS(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    glm::mat4 m = glm::mat4_cast(r);
    m[0] *= s.x;
    m[1] *= s.y;
    m[2] *= s.z;
    m[3] = glm::vec4(t, 1.0f);
    return m;
}

void morph(const Primitive& p, const std::vector<float>& weights, std::vector<glm::vec3>& position,
           std::vector<glm::vec3>& normal, std::vector<glm::vec4>& tangent) {
    position = p.position;
    normal = p.normal;
    tangent = p.tangent;
    for (size_t t = 0; t < p.targets.size() && t < weights.size(); t++) {
        const float w = weights[t];
        if (w == 0.0f) continue;
        const MorphTarget& mt = p.targets[t];
        for (size_t i = 0; i < mt.position.size() && i < position.size(); i++) position[i] += w * mt.position[i];
        for (size_t i = 0; i < mt.normal.size() && i < normal.size(); i++) normal[i] += w * mt.normal[i];
        for (size_t i = 0; i < mt.tangent.size() && i < tangent.size(); i++) tangent[i] += glm::vec4(w * mt.tangent[i], 0.0f);
    }
    for (glm::vec3& n : normal) {
        const float l = glm::length(n);
        if (l > 0) n /= l;
    }
}

// ---- Pose -------------------------------------------------------------------------------------------

void Pose::bind(const Model* model) {
    model_ = model;
    reset();
}

void Pose::reset() {
    if (!model_) return;
    const size_t n = model_->nodes.size();
    t_.resize(n);
    r_.resize(n);
    s_.resize(n);
    weights_.assign(n, {});
    for (size_t i = 0; i < n; i++) {
        const Node& node = model_->nodes[i];
        t_[i] = node.translation;
        r_[i] = node.rotation;
        s_[i] = node.scale;
        if (node.mesh >= 0) {
            const Mesh& mesh = model_->meshes[(size_t)node.mesh];
            weights_[i] = node.weights.empty() ? mesh.weights : node.weights;
            weights_[i].resize(mesh.weights.size(), 0.0f);
        }
    }
    updateWorld();
}

void Pose::apply(int index, float t) { sample(index, t, 1.0f); }

void Pose::blend(int index, float t, float w) { sample(index, t, std::clamp(w, 0.0f, 1.0f)); }

void Pose::sample(int index, float t, float w) {
    if (!model_ || index < 0 || index >= (int)model_->animations.size() || w <= 0.0f) return;
    const Animation& a = model_->animations[(size_t)index];
    t = std::clamp(t, 0.0f, a.duration);
    std::vector<float> v;
    for (const Channel& c : a.channels) {
        if (c.node < 0 || c.node >= (int)t_.size() || c.sampler < 0 || c.sampler >= (int)a.samplers.size()) continue;
        const size_t n = (size_t)c.node;
        sampleValue(a.samplers[(size_t)c.sampler], t, c.path == Path::Rotation, v);
        switch (c.path) {
        case Path::Translation:
            if (v.size() >= 3) t_[n] = glm::mix(t_[n], glm::vec3(v[0], v[1], v[2]), w);
            break;
        case Path::Scale:
            if (v.size() >= 3) s_[n] = glm::mix(s_[n], glm::vec3(v[0], v[1], v[2]), w);
            break;
        case Path::Rotation:
            if (v.size() >= 4) {
                const glm::quat q = glm::normalize(glm::quat(v[3], v[0], v[1], v[2]));
                r_[n] = w >= 1.0f ? q : glm::normalize(glm::slerp(r_[n], q, w));
            }
            break;
        case Path::Weights:
            for (size_t k = 0; k < v.size() && k < weights_[n].size(); k++) weights_[n][k] = weights_[n][k] * (1 - w) + v[k] * w;
            break;
        }
    }
}

void Pose::updateWorld() {
    if (!model_) return;
    const size_t n = model_->nodes.size();
    world_.assign(n, glm::mat4(1.0f));
    // Parents first: walk down from every root (files do not promise parents come first).
    std::vector<int> stack;
    for (size_t i = 0; i < n; i++)
        if (model_->nodes[i].parent < 0) stack.push_back((int)i);
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const Node& node = model_->nodes[(size_t)i];
        const glm::mat4 local = composeTRS(t_[(size_t)i], r_[(size_t)i], s_[(size_t)i]);
        world_[(size_t)i] = node.parent >= 0 ? world_[(size_t)node.parent] * local : local;
        for (int c : node.children) stack.push_back(c);
    }
}

void Pose::jointMatrices(int skin, std::vector<glm::mat4>& out) const {
    out.clear();
    if (!model_ || skin < 0 || skin >= (int)model_->skins.size()) return;
    const Skin& s = model_->skins[(size_t)skin];
    out.resize(std::min(s.joints.size(), (size_t)kMaxJoints));
    for (size_t j = 0; j < out.size(); j++) out[j] = world_[(size_t)s.joints[j]] * s.inverseBind[j];
}

std::vector<PlacedCamera> Pose::cameras() const {
    std::vector<PlacedCamera> out;
    if (!model_) return out;
    for (size_t i = 0; i < model_->nodes.size() && i < world_.size(); i++) {
        const int cam = model_->nodes[i].camera;
        if (cam < 0 || cam >= (int)model_->cameras.size()) continue;
        PlacedCamera p;
        p.camera = cam;
        p.node = (int)i;
        const glm::mat4& w = world_[i];
        // Rotation only: a scaled (or mirrored) node must not stretch the view.
        glm::vec3 z = glm::vec3(w[2]), y = glm::vec3(w[1]);
        z = glm::length(z) > 0 ? glm::normalize(z) : glm::vec3(0, 0, 1);
        glm::vec3 x = glm::cross(y, z);
        x = glm::length(x) > 0 ? glm::normalize(x) : glm::vec3(1, 0, 0);
        y = glm::cross(z, x);
        p.world = glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), w[3]);
        out.push_back(p);
    }
    return out;
}

std::vector<PlacedLight> Pose::lights() const {
    std::vector<PlacedLight> out;
    if (!model_) return out;
    for (size_t i = 0; i < model_->nodes.size() && i < world_.size(); i++) {
        const int l = model_->nodes[i].light;
        if (l < 0 || l >= (int)model_->lights.size()) continue;
        PlacedLight p;
        p.light = l;
        p.position = glm::vec3(world_[i][3]);
        const glm::vec3 d = -glm::vec3(world_[i][2]);
        p.direction = glm::length(d) > 0 ? glm::normalize(d) : glm::vec3(0, 0, -1);
        out.push_back(p);
    }
    return out;
}

bool Pose::bounds(glm::vec3& mn, glm::vec3& mx) const {
    if (!model_ || model_->scenes.empty()) return false;
    bool any = false;
    auto add = [&](const glm::vec3& p) {
        if (!any) mn = mx = p;
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
        any = true;
    };
    const Scene& sc = model_->scenes[(size_t)std::clamp(model_->scene, 0, (int)model_->scenes.size() - 1)];
    std::vector<int> stack(sc.roots.begin(), sc.roots.end());
    std::vector<glm::mat4> joints;
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const Node& node = model_->nodes[(size_t)i];
        for (int c : node.children) stack.push_back(c);
        if (node.mesh < 0) continue;
        const Mesh& mesh = model_->meshes[(size_t)node.mesh];
        if (node.skin >= 0) {   // skinned on the CPU: the bind pose box can be far off
            jointMatrices(node.skin, joints);
            for (const Primitive& p : mesh.primitives) {
                if (!p.skinned()) continue;
                for (size_t v = 0; v < p.position.size(); v++) {
                    glm::mat4 skinM(0.0f);
                    for (int k = 0; k < 4; k++) {
                        const int j = (int)p.joints[v][k];
                        if (p.weights[v][k] > 0 && j >= 0 && j < (int)joints.size()) skinM += p.weights[v][k] * joints[(size_t)j];
                    }
                    add(glm::vec3(skinM * glm::vec4(p.position[v], 1.0f)));
                }
            }
            continue;
        }
        const std::vector<glm::mat4> one{glm::mat4(1.0f)};
        const std::vector<glm::mat4>& inst = node.instances.empty() ? one : node.instances;
        for (const Primitive& p : mesh.primitives) {
            glm::vec3 bmin = p.boundsMin, bmax = p.boundsMax;
            if (!p.targets.empty()) {   // morphed: the box of the morphed positions
                std::vector<glm::vec3> mp, mn2;
                std::vector<glm::vec4> mt;
                morph(p, weights_[(size_t)i], mp, mn2, mt);
                if (!mp.empty()) bmin = bmax = mp[0];
                for (const glm::vec3& v : mp) {
                    bmin = glm::min(bmin, v);
                    bmax = glm::max(bmax, v);
                }
            }
            for (const glm::mat4& im : inst)
                for (int c = 0; c < 8; c++) {
                    const glm::vec3 corner((c & 1) ? bmax.x : bmin.x, (c & 2) ? bmax.y : bmin.y, (c & 4) ? bmax.z : bmin.z);
                    add(glm::vec3(world_[(size_t)i] * im * glm::vec4(corner, 1.0f)));
                }
        }
    }
    return any;
}

}  // namespace toms::gltf
