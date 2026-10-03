// atlas_xml.h -- a minimal XML reader for .pi and .plist: elements, attributes, text. No DTDs,
// no namespaces. Handles a UTF-8 or UTF-16 (BOM) file; other 8-bit encodings pass through as bytes.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atlas {

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<XmlNode>> children;
    std::string text;

    const std::string* attr(const std::string& key) const {
        for (const auto& a : attrs) if (a.first == key) return &a.second;
        return nullptr;
    }
    std::string attrOr(const std::string& key, const std::string& def = std::string()) const {
        const std::string* v = attr(key);
        return v ? *v : def;
    }
};

// Returns the root element, or null with *err set.
std::unique_ptr<XmlNode> parseXml(const std::string& bytes, std::string* err = nullptr);
std::string xmlEscape(const std::string& s);

}  // namespace atlas
