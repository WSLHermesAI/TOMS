// atlas_xml.cpp -- see atlas_xml.h.
#include "atlas_xml.h"

#include <cstring>

namespace atlas {

static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

static std::string toUtf8(const std::string& in) {
    if (in.size() >= 3 && (uint8_t)in[0] == 0xEF && (uint8_t)in[1] == 0xBB && (uint8_t)in[2] == 0xBF) return in.substr(3);
    const bool le = in.size() >= 2 && (uint8_t)in[0] == 0xFF && (uint8_t)in[1] == 0xFE;
    const bool be = in.size() >= 2 && (uint8_t)in[0] == 0xFE && (uint8_t)in[1] == 0xFF;
    if (!le && !be) return in;
    std::string out;
    for (size_t i = 2; i + 1 < in.size(); i += 2) {
        uint32_t u = le ? ((uint8_t)in[i] | ((uint8_t)in[i + 1] << 8)) : (((uint8_t)in[i] << 8) | (uint8_t)in[i + 1]);
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < in.size()) {
            const uint32_t lo = le ? ((uint8_t)in[i + 2] | ((uint8_t)in[i + 3] << 8)) : (((uint8_t)in[i + 2] << 8) | (uint8_t)in[i + 3]);
            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        appendUtf8(out, u);
    }
    return out;
}

static std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { out += s[i]; continue; }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos) { out += s[i]; continue; }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "amp") out += '&';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') appendUtf8(out, (uint32_t)strtoul(ent.c_str() + (ent.size() > 1 && ent[1] == 'x' ? 2 : 1), nullptr, ent.size() > 1 && ent[1] == 'x' ? 16 : 10));
        else { out += s.substr(i, semi - i + 1); }
        i = semi;
    }
    return out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

namespace {
struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;

    bool startsWith(const char* t) const { return s.compare(i, strlen(t), t) == 0; }
    void ws() { while (i < s.size() && (unsigned char)s[i] <= ' ') i++; }
    bool skipMisc() {   // comments, <? ?>, <!DOCTYPE>
        for (;;) {
            ws();
            if (startsWith("<!--")) { const size_t e = s.find("-->", i); if (e == std::string::npos) return false; i = e + 3; }
            else if (startsWith("<?")) { const size_t e = s.find("?>", i); if (e == std::string::npos) return false; i = e + 2; }
            else if (startsWith("<!")) { const size_t e = s.find('>', i); if (e == std::string::npos) return false; i = e + 1; }
            else return true;
        }
    }
    std::string name() {
        const size_t b = i;
        while (i < s.size() && !strchr(" \t\r\n/>=", s[i])) i++;
        return s.substr(b, i - b);
    }
    std::unique_ptr<XmlNode> element(int depth) {
        if (depth > 256) { err = "nesting too deep"; return nullptr; }
        if (i >= s.size() || s[i] != '<') { err = "expected '<' at byte " + std::to_string(i); return nullptr; }
        i++;
        auto n = std::make_unique<XmlNode>();
        n->name = name();
        for (;;) {
            ws();
            if (i >= s.size()) { err = "unexpected end in <" + n->name + ">"; return nullptr; }
            if (startsWith("/>")) { i += 2; return n; }
            if (s[i] == '>') { i++; break; }
            std::string key = name();
            ws();
            if (i >= s.size() || s[i] != '=') { err = "attribute '" + key + "' has no value"; return nullptr; }
            i++;
            ws();
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) { err = "attribute '" + key + "' is not quoted"; return nullptr; }
            const char q = s[i++];
            const size_t e = s.find(q, i);
            if (e == std::string::npos) { err = "unterminated attribute '" + key + "'"; return nullptr; }
            n->attrs.emplace_back(key, unescape(s.substr(i, e - i)));
            i = e + 1;
        }
        for (;;) {
            const size_t b = i;
            while (i < s.size() && s[i] != '<') i++;
            n->text += unescape(s.substr(b, i - b));
            if (i >= s.size()) { err = "unexpected end in <" + n->name + ">"; return nullptr; }
            if (startsWith("</")) {
                i += 2;
                const std::string close = name();
                ws();
                if (close != n->name || i >= s.size() || s[i] != '>') { err = "mismatched </" + close + "> for <" + n->name + ">"; return nullptr; }
                i++;
                return n;
            }
            if (startsWith("<!--") || startsWith("<?") || startsWith("<!")) {
                if (startsWith("<![CDATA[")) {
                    const size_t e = s.find("]]>", i);
                    if (e == std::string::npos) { err = "unterminated CDATA"; return nullptr; }
                    n->text += s.substr(i + 9, e - i - 9);
                    i = e + 3;
                    continue;
                }
                if (!skipMisc()) { err = "unterminated comment"; return nullptr; }
                continue;
            }
            auto c = element(depth + 1);
            if (!c) return nullptr;
            n->children.push_back(std::move(c));
        }
    }
};
}  // namespace

std::unique_ptr<XmlNode> parseXml(const std::string& bytes, std::string* err) {
    const std::string text = toUtf8(bytes);
    Parser p{text};
    if (!p.skipMisc()) { if (err) *err = "unterminated comment or declaration"; return nullptr; }
    auto root = p.element(0);
    if (!root && err) *err = p.err;
    return root;
}

}  // namespace atlas
