// atlas_xml.cpp -- see atlas_xml.h.
#include "atlas_xml.h"

#include <cstring>

namespace atlas {

static void f_AppendUtf8(std::string& l_strOut, uint32_t l_uCp) {
    if (l_uCp < 0x80) l_strOut += (char)l_uCp;
    else if (l_uCp < 0x800) { l_strOut += (char)(0xC0 | (l_uCp >> 6)); l_strOut += (char)(0x80 | (l_uCp & 0x3F)); }
    else if (l_uCp < 0x10000) { l_strOut += (char)(0xE0 | (l_uCp >> 12)); l_strOut += (char)(0x80 | ((l_uCp >> 6) & 0x3F)); l_strOut += (char)(0x80 | (l_uCp & 0x3F)); }
    else { l_strOut += (char)(0xF0 | (l_uCp >> 18)); l_strOut += (char)(0x80 | ((l_uCp >> 12) & 0x3F)); l_strOut += (char)(0x80 | ((l_uCp >> 6) & 0x3F)); l_strOut += (char)(0x80 | (l_uCp & 0x3F)); }
}

static std::string f_ToUtf8(const std::string& l_strIn) {
    if (l_strIn.size() >= 3 && (uint8_t)l_strIn[0] == 0xEF && (uint8_t)l_strIn[1] == 0xBB && (uint8_t)l_strIn[2] == 0xBF) return l_strIn.substr(3);
    const bool l_bLe = l_strIn.size() >= 2 && (uint8_t)l_strIn[0] == 0xFF && (uint8_t)l_strIn[1] == 0xFE;
    const bool l_bBe = l_strIn.size() >= 2 && (uint8_t)l_strIn[0] == 0xFE && (uint8_t)l_strIn[1] == 0xFF;
    if (!l_bLe && !l_bBe) return l_strIn;
    std::string l_strOut;
    for (size_t l_nI = 2; l_nI + 1 < l_strIn.size(); l_nI += 2) {
        uint32_t l_uU = l_bLe ? ((uint8_t)l_strIn[l_nI] | ((uint8_t)l_strIn[l_nI + 1] << 8)) : (((uint8_t)l_strIn[l_nI] << 8) | (uint8_t)l_strIn[l_nI + 1]);
        if (l_uU >= 0xD800 && l_uU < 0xDC00 && l_nI + 3 < l_strIn.size()) {
            const uint32_t l_uLo = l_bLe ? ((uint8_t)l_strIn[l_nI + 2] | ((uint8_t)l_strIn[l_nI + 3] << 8)) : (((uint8_t)l_strIn[l_nI + 2] << 8) | (uint8_t)l_strIn[l_nI + 3]);
            l_uU = 0x10000 + ((l_uU - 0xD800) << 10) + (l_uLo - 0xDC00);
            l_nI += 2;
        }
        f_AppendUtf8(l_strOut, l_uU);
    }
    return l_strOut;
}

static std::string f_Unescape(const std::string& l_strS) {
    std::string l_strOut;
    for (size_t l_nI = 0; l_nI < l_strS.size(); l_nI++) {
        if (l_strS[l_nI] != '&') { l_strOut += l_strS[l_nI]; continue; }
        const size_t l_nSemi = l_strS.find(';', l_nI);
        if (l_nSemi == std::string::npos) { l_strOut += l_strS[l_nI]; continue; }
        const std::string l_strEnt = l_strS.substr(l_nI + 1, l_nSemi - l_nI - 1);
        if (l_strEnt == "lt") l_strOut += '<';
        else if (l_strEnt == "gt") l_strOut += '>';
        else if (l_strEnt == "amp") l_strOut += '&';
        else if (l_strEnt == "quot") l_strOut += '"';
        else if (l_strEnt == "apos") l_strOut += '\'';
        else if (!l_strEnt.empty() && l_strEnt[0] == '#') f_AppendUtf8(l_strOut, (uint32_t)strtoul(l_strEnt.c_str() + (l_strEnt.size() > 1 && l_strEnt[1] == 'x' ? 2 : 1), nullptr, l_strEnt.size() > 1 && l_strEnt[1] == 'x' ? 16 : 10));
        else { l_strOut += l_strS.substr(l_nI, l_nSemi - l_nI + 1); }
        l_nI = l_nSemi;
    }
    return l_strOut;
}

std::string f_XmlEscape(const std::string& l_strS) {
    std::string l_strOut;
    for (char l_cC : l_strS) {
        switch (l_cC) {
            case '<': l_strOut += "&lt;"; break;
            case '>': l_strOut += "&gt;"; break;
            case '&': l_strOut += "&amp;"; break;
            case '"': l_strOut += "&quot;"; break;
            default: l_strOut += l_cC;
        }
    }
    return l_strOut;
}

namespace {
struct Parser {
    const std::string& m_strS;
    size_t m_nI = 0;
    std::string m_strErr;

    bool f_StartsWith(const char* l_pszT) const { return m_strS.compare(m_nI, strlen(l_pszT), l_pszT) == 0; }
    void f_Ws() { while (m_nI < m_strS.size() && (unsigned char)m_strS[m_nI] <= ' ') m_nI++; }
    bool f_SkipMisc() {   // comments, <? ?>, <!DOCTYPE>
        for (;;) {
            f_Ws();
            if (f_StartsWith("<!--")) { const size_t l_nE = m_strS.find("-->", m_nI); if (l_nE == std::string::npos) return false; m_nI = l_nE + 3; }
            else if (f_StartsWith("<?")) { const size_t l_nE = m_strS.find("?>", m_nI); if (l_nE == std::string::npos) return false; m_nI = l_nE + 2; }
            else if (f_StartsWith("<!")) { const size_t l_nE = m_strS.find('>', m_nI); if (l_nE == std::string::npos) return false; m_nI = l_nE + 1; }
            else return true;
        }
    }
    std::string f_Name() {
        const size_t l_nB = m_nI;
        while (m_nI < m_strS.size() && !strchr(" \t\r\n/>=", m_strS[m_nI])) m_nI++;
        return m_strS.substr(l_nB, m_nI - l_nB);
    }
    std::unique_ptr<XmlNode> f_Element(int l_iDepth) {
        if (l_iDepth > 256) { m_strErr = "nesting too deep"; return nullptr; }
        if (m_nI >= m_strS.size() || m_strS[m_nI] != '<') { m_strErr = "expected '<' at byte " + std::to_string(m_nI); return nullptr; }
        m_nI++;
        auto l_upN = std::make_unique<XmlNode>();
        l_upN->m_strName = f_Name();
        for (;;) {
            f_Ws();
            if (m_nI >= m_strS.size()) { m_strErr = "unexpected end in <" + l_upN->m_strName + ">"; return nullptr; }
            if (f_StartsWith("/>")) { m_nI += 2; return l_upN; }
            if (m_strS[m_nI] == '>') { m_nI++; break; }
            std::string l_strKey = f_Name();
            f_Ws();
            if (m_nI >= m_strS.size() || m_strS[m_nI] != '=') { m_strErr = "attribute '" + l_strKey + "' has no value"; return nullptr; }
            m_nI++;
            f_Ws();
            if (m_nI >= m_strS.size() || (m_strS[m_nI] != '"' && m_strS[m_nI] != '\'')) { m_strErr = "attribute '" + l_strKey + "' is not quoted"; return nullptr; }
            const char l_cQ = m_strS[m_nI++];
            const size_t l_nE = m_strS.find(l_cQ, m_nI);
            if (l_nE == std::string::npos) { m_strErr = "unterminated attribute '" + l_strKey + "'"; return nullptr; }
            l_upN->m_vAttrs.emplace_back(l_strKey, f_Unescape(m_strS.substr(m_nI, l_nE - m_nI)));
            m_nI = l_nE + 1;
        }
        for (;;) {
            const size_t l_nB = m_nI;
            while (m_nI < m_strS.size() && m_strS[m_nI] != '<') m_nI++;
            l_upN->m_strText += f_Unescape(m_strS.substr(l_nB, m_nI - l_nB));
            if (m_nI >= m_strS.size()) { m_strErr = "unexpected end in <" + l_upN->m_strName + ">"; return nullptr; }
            if (f_StartsWith("</")) {
                m_nI += 2;
                const std::string l_strClose = f_Name();
                f_Ws();
                if (l_strClose != l_upN->m_strName || m_nI >= m_strS.size() || m_strS[m_nI] != '>') { m_strErr = "mismatched </" + l_strClose + "> for <" + l_upN->m_strName + ">"; return nullptr; }
                m_nI++;
                return l_upN;
            }
            if (f_StartsWith("<!--") || f_StartsWith("<?") || f_StartsWith("<!")) {
                if (f_StartsWith("<![CDATA[")) {
                    const size_t l_nE = m_strS.find("]]>", m_nI);
                    if (l_nE == std::string::npos) { m_strErr = "unterminated CDATA"; return nullptr; }
                    l_upN->m_strText += m_strS.substr(m_nI + 9, l_nE - m_nI - 9);
                    m_nI = l_nE + 3;
                    continue;
                }
                if (!f_SkipMisc()) { m_strErr = "unterminated comment"; return nullptr; }
                continue;
            }
            auto l_upC = f_Element(l_iDepth + 1);
            if (!l_upC) return nullptr;
            l_upN->m_vChildren.push_back(std::move(l_upC));
        }
    }
};
}  // namespace

std::unique_ptr<XmlNode> f_ParseXml(const std::string& l_strBytes, std::string* l_pErr) {
    const std::string l_strText = f_ToUtf8(l_strBytes);
    Parser l_oP{l_strText};
    if (!l_oP.f_SkipMisc()) { if (l_pErr) *l_pErr = "unterminated comment or declaration"; return nullptr; }
    auto l_upRoot = l_oP.f_Element(0);
    if (!l_upRoot && l_pErr) *l_pErr = l_oP.m_strErr;
    return l_upRoot;
}

}  // namespace atlas
