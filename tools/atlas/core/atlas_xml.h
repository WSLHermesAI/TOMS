// atlas_xml.h -- a minimal XML reader for .pi and .plist: elements, attributes, text. No DTDs,
// no namespaces. Handles a UTF-8 or UTF-16 (BOM) file; other 8-bit encodings pass through as bytes.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atlas {

struct XmlNode {
    std::string m_strName;
    std::vector<std::pair<std::string, std::string>> m_vAttrs;
    std::vector<std::unique_ptr<XmlNode>> m_vChildren;
    std::string m_strText;

    const std::string* f_Attr(const std::string& l_strKey) const {
        for (const auto& l_pairA : m_vAttrs) if (l_pairA.first == l_strKey) return &l_pairA.second;
        return nullptr;
    }
    std::string f_AttrOr(const std::string& l_strKey, const std::string& l_strDef = std::string()) const {
        const std::string* l_pV = f_Attr(l_strKey);
        return l_pV ? *l_pV : l_strDef;
    }
};

// Returns the root element, or null with *err set.
std::unique_ptr<XmlNode> f_ParseXml(const std::string& l_strBytes, std::string* l_pErr = nullptr);
std::string f_XmlEscape(const std::string& l_strS);

}  // namespace atlas
