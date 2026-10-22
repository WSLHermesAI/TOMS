// sprite_mesh.cpp -- see sprite_mesh.h.
#include "sprite_mesh.h"

#include <json.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace toms {

using json = nlohmann::json;

std::string spriteMeshPath(const std::string& atlasPath) {
    const size_t slash = atlasPath.find_last_of("/\\");
    const size_t dot = atlasPath.find_last_of('.');
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    return (hasExt ? atlasPath.substr(0, dot) : atlasPath) + ".mesh.json";
}

namespace {

void checkMesh(const SpriteMesh& m, const std::string& name) {
    if (m.vertices.size() % 2) throw std::runtime_error(name + ": vertices must be x, y pairs");
    if (m.vertexCount() > 65536u) throw std::runtime_error(name + ": more than 65536 vertices");
    if (m.triangles.size() % 3) throw std::runtime_error(name + ": triangles must be three indices each");
    for (uint16_t i : m.triangles)
        if (i >= m.vertexCount()) throw std::runtime_error(name + ": index " + std::to_string(i) + " past the last vertex");
}

// count values of T at `view` ({"buffer", "byteOffset"}) in the loaded buffers.
template <class T>
std::vector<T> fromBuffer(const json& view, size_t count, const std::vector<std::string>& buffers, const std::string& where) {
    const size_t b = view.at("buffer").get<size_t>(), off = view.value("byteOffset", (size_t)0);
    if (b >= buffers.size()) throw std::runtime_error(where + ": no buffer " + std::to_string(b));
    if (off + count * sizeof(T) > buffers[b].size()) throw std::runtime_error(where + ": past the end of buffer " + std::to_string(b));
    std::vector<T> out(count);
    if (count) std::memcpy(out.data(), buffers[b].data() + off, count * sizeof(T));   // little-endian, as every target
    return out;
}

}  // namespace

bool parseSpriteMeshes(const std::string& text, const std::function<bool(const std::string&, std::string&)>& readBuffer,
                       SpriteMeshFile& out, std::string* err) {
    try {
        const json j = json::parse(text);
        if (j.value("version", 1) > 1) throw std::runtime_error("version " + std::to_string(j.value("version", 1)) + " is newer than this build reads (1)");
        std::vector<std::string> buffers;
        for (const json& b : j.value("buffers", json::array())) {
            const std::string uri = b.at("uri").get<std::string>();
            std::string bytes;
            if (!readBuffer || !readBuffer(uri, bytes)) throw std::runtime_error("cannot read the buffer " + uri);
            if (b.contains("byteLength") && bytes.size() < b["byteLength"].get<size_t>()) throw std::runtime_error(uri + " is shorter than its byteLength");
            buffers.push_back(std::move(bytes));
        }
        SpriteMeshFile f;
        const json meshes = j.value("meshes", json::object());
        for (auto it = meshes.begin(); it != meshes.end(); ++it) {
            const std::string& name = it.key();
            const json& m = it.value();
            SpriteMesh mesh;
            mesh.source = m.value("source", std::string("alpha"));
            if (m.contains("cells")) {
                const json& c = m["cells"];
                if (!c.is_array() || c.size() != 2 || c[0].get<int>() < 1 || c[1].get<int>() < 1)
                    throw std::runtime_error(name + ": cells must be [across, down], at least 1 each");
                mesh.cells[0] = c[0].get<int>();
                mesh.cells[1] = c[1].get<int>();
            }
            const json& v = m.at("vertices");
            const json& t = m.at("triangles");
            if (v.is_array()) mesh.vertices = v.get<std::vector<float>>();
            else mesh.vertices = fromBuffer<float>(v, m.at("vertexCount").get<size_t>() * 2, buffers, name);
            if (t.is_array()) mesh.triangles = t.get<std::vector<uint16_t>>();
            else mesh.triangles = fromBuffer<uint16_t>(t, m.at("indexCount").get<size_t>(), buffers, name);
            checkMesh(mesh, name);
            f.meshes[name] = std::move(mesh);
        }
        out = std::move(f);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

std::string spriteMeshesToJson(const SpriteMeshFile& f, const std::string& binaryUri, std::string* binary) {
    const bool bin = !binaryUri.empty() && binary;
    if (bin) binary->clear();
    json j;
    j["version"] = 1;
    json meshes = json::object();
    for (const auto& [name, m] : f.meshes) {
        json mj;
        mj["source"] = m.source;
        if (m.cells[0] > 0 && m.cells[1] > 0) mj["cells"] = {m.cells[0], m.cells[1]};
        if (bin) {
            // Each array starts on a 4-byte boundary (glTF's rule), so the floats are aligned.
            auto put = [&](const void* data, size_t bytes) {
                while (binary->size() % 4) binary->push_back('\0');
                const size_t at = binary->size();
                binary->append((const char*)data, bytes);
                return json{{"buffer", 0}, {"byteOffset", at}};
            };
            mj["vertexCount"] = m.vertexCount();
            mj["vertices"] = put(m.vertices.data(), m.vertices.size() * sizeof(float));
            mj["indexCount"] = m.triangles.size();
            mj["triangles"] = put(m.triangles.data(), m.triangles.size() * sizeof(uint16_t));
        } else {
            json v = json::array();   // whole pixels (meshes cut from alpha) as integers: "8", not "8.0"
            for (float x : m.vertices)
                if (x == (float)(long long)x) v.push_back((long long)x);
                else v.push_back(x);
            mj["vertices"] = v;
            mj["triangles"] = m.triangles;
        }
        meshes[name] = mj;
    }
    if (bin) {
        while (binary->size() % 4) binary->push_back('\0');
        j["buffers"] = json::array({{{"uri", binaryUri}, {"byteLength", binary->size()}}});
    }
    j["meshes"] = meshes;
    return j.dump() + "\n";   // compact: it is data (the atlas tool and the anim editor show it)
}

namespace {
// The mesh on the grid whose column edges are xs (0 .. w) and row edges ys (0 .. h), in packed pixels.
SpriteMesh cutOnGrid(const uint8_t* rgba, int stride, const std::vector<int>& xs, const std::vector<int>& ys, int offX, int offY,
                     int alphaMin) {
    SpriteMesh m;
    const int cols = (int)xs.size() - 1, rows = (int)ys.size() - 1;
    auto filled = [&](int cx, int cy) {
        for (int y = ys[(size_t)cy]; y < ys[(size_t)cy + 1]; y++)
            for (int x = xs[(size_t)cx]; x < xs[(size_t)cx + 1]; x++)
                if (rgba[((size_t)y * stride + x) * 4 + 3] >= alphaMin) return true;
        return false;
    };
    // Grid corners are shared: corner (gx, gy) -> its vertex, made the first time a cell uses it.
    std::unordered_map<int, uint16_t> corner;
    auto vertex = [&](int gx, int gy) -> uint16_t {
        const int key = gy * (cols + 1) + gx;
        auto it = corner.find(key);
        if (it != corner.end()) return it->second;
        const uint16_t i = (uint16_t)m.vertexCount();
        m.vertices.push_back((float)(offX + xs[(size_t)gx]));
        m.vertices.push_back((float)(offY + ys[(size_t)gy]));
        corner.emplace(key, i);
        return i;
    };
    for (int cy = 0; cy < rows; cy++)
        for (int cx = 0; cx < cols; cx++) {
            if (!filled(cx, cy)) continue;
            if (m.vertexCount() > 65532u) return m;   // 16-bit indices
            const uint16_t a = vertex(cx, cy), b = vertex(cx + 1, cy), c = vertex(cx + 1, cy + 1), d = vertex(cx, cy + 1);
            m.triangles.insert(m.triangles.end(), {a, b, c, a, c, d});
        }
    return m;
}
}  // namespace

SpriteMesh meshFromAlpha(const uint8_t* rgba, int stride, int w, int h, int offX, int offY, int cell, int alphaMin) {
    if (!rgba || w <= 0 || h <= 0) return SpriteMesh();
    if (cell < 1) cell = 1;
    std::vector<int> xs, ys;
    for (int x = 0; x < w; x += cell) xs.push_back(x);
    xs.push_back(w);
    for (int y = 0; y < h; y += cell) ys.push_back(y);
    ys.push_back(h);
    return cutOnGrid(rgba, stride, xs, ys, offX, offY, alphaMin);
}

SpriteBox visibleBox(const uint8_t* rgba, int stride, int w, int h, int offX, int offY, int origW, int origH, int alphaMin) {
    SpriteBox b;
    b.origW = (float)origW;
    b.origH = (float)origH;
    if (!rgba || w <= 0 || h <= 0) return b;
    int l = w, t = h, r = -1, bt = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (rgba[((size_t)y * stride + x) * 4 + 3] >= alphaMin) {
                l = std::min(l, x); r = std::max(r, x);
                t = std::min(t, y); bt = std::max(bt, y);
            }
    if (r < 0) return b;
    b.left = (float)(offX + l);
    b.top = (float)(offY + t);
    b.right = (float)(offX + r + 1);
    b.bottom = (float)(offY + bt + 1);
    return b;
}

SpriteMesh meshFromAlphaGrid(const uint8_t* rgba, int stride, int w, int h, int offX, int offY, int cols, int rows, int alphaMin) {
    if (!rgba || w <= 0 || h <= 0) return SpriteMesh();
    cols = std::clamp(cols, 1, w);
    rows = std::clamp(rows, 1, h);
    std::vector<int> xs, ys;
    for (int i = 0; i <= cols; i++) xs.push_back((int)((long long)i * w / cols));
    for (int i = 0; i <= rows; i++) ys.push_back((int)((long long)i * h / rows));
    SpriteMesh m = cutOnGrid(rgba, stride, xs, ys, offX, offY, alphaMin);
    m.cells[0] = cols;
    m.cells[1] = rows;
    return m;
}

}  // namespace toms
