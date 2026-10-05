#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "Package/CMChunk.h"
#include "Package/CMChunkTypes.h"
namespace pkzgui
{
    struct XmlNode
    {
        std::string name;
        std::map<std::string, std::string> attrs;
        std::vector<XmlNode> children;
        std::string Attr(const std::string& key) const
        {
            auto it = attrs.find(key);
            return it == attrs.end() ? std::string() : it->second;
        }
    };
    inline std::string XmlDecode(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] != '&')
            {
                out += s[i];
                continue;
            }
            const size_t semi = s.find(';', i);
            if (semi == std::string::npos)
            {
                out += s[i];
                continue;
            }
            const std::string ent = s.substr(i + 1, semi - i - 1);
            if (ent == "amp") out += '&';
            else if (ent == "lt") out += '<';
            else if (ent == "gt") out += '>';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else if (!ent.empty() && ent[0] == '#')
            {
                const long v = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                    ? std::strtol(ent.c_str() + 2, nullptr, 16)
                    : std::strtol(ent.c_str() + 1, nullptr, 10);
                out += static_cast<char>(v);
            }
            else
            {
                out += s.substr(i, semi - i + 1);
            }
            i = semi;
        }
        return out;
    }
    inline void XmlSkipSpace(const std::string& s, size_t& i)
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }
    inline bool XmlSkipMisc(const std::string& s, size_t& i)
    {
        for (;;)
        {
            XmlSkipSpace(s, i);
            if (i >= s.size())
                return true;
            if (s.compare(i, 4, "<!--") == 0)
            {
                const size_t e = s.find("-->", i + 4);
                if (e == std::string::npos)
                    return false;
                i = e + 3;
            }
            else if (s.compare(i, 2, "<?") == 0)
            {
                const size_t e = s.find("?>", i + 2);
                if (e == std::string::npos)
                    return false;
                i = e + 2;
            }
            else if (s.compare(i, 2, "<!") == 0)
            {
                const size_t e = s.find('>', i);
                if (e == std::string::npos)
                    return false;
                i = e + 1;
            }
            else
            {
                return true;
            }
        }
    }
    inline bool XmlParseElement(const std::string& s, size_t& i, XmlNode& node)
    {
        if (i >= s.size() || s[i] != '<')
            return false;
        ++i;
        size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/')
            ++i;
        node.name = s.substr(start, i - start);
        for (;;)
        {
            XmlSkipSpace(s, i);
            if (i >= s.size())
                return false;
            if (s[i] == '/')
            {
                if (i + 1 >= s.size() || s[i + 1] != '>')
                    return false;
                i += 2;
                return true;
            }
            if (s[i] == '>')
            {
                ++i;
                break;
            }
            const size_t keyStart = i;
            while (i < s.size() && s[i] != '=' && s[i] != '>' && s[i] != '/' &&
                !std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            const std::string key = s.substr(keyStart, i - keyStart);
            XmlSkipSpace(s, i);
            if (i >= s.size() || s[i] != '=')
                return false;
            ++i;
            XmlSkipSpace(s, i);
            if (i >= s.size() || (s[i] != '"' && s[i] != '\''))
                return false;
            const char quote = s[i++];
            const size_t end = s.find(quote, i);
            if (end == std::string::npos)
                return false;
            node.attrs[key] = XmlDecode(s.substr(i, end - i));
            i = end + 1;
        }
        for (;;)
        {
            const size_t lt = s.find('<', i);
            if (lt == std::string::npos)
                return false;
            i = lt;
            if (s.compare(i, 2, "</") == 0)
            {
                const size_t e = s.find('>', i);
                if (e == std::string::npos)
                    return false;
                i = e + 1;
                return true;
            }
            if (s.compare(i, 4, "<!--") == 0 || s.compare(i, 2, "<?") == 0 || s.compare(i, 2, "<!") == 0)
            {
                if (!XmlSkipMisc(s, i))
                    return false;
                continue;
            }
            XmlNode child;
            if (!XmlParseElement(s, i, child))
                return false;
            node.children.push_back(std::move(child));
        }
    }
    inline bool ParseXml(const std::string& text, XmlNode& root)
    {
        size_t i = 0;
        if (!XmlSkipMisc(text, i))
            return false;
        return XmlParseElement(text, i, root);
    }
    enum class BaseType
    {
        U8, U16, U32, U64,
        I8, I16, I32, I64,
        F16, F32, F64,
        Char, CString, Bytes, Struct, Invalid
    };
    struct FieldDef
    {
        std::string name;
        std::string offset = "next";
        std::string countExpr;
        std::string sizeExpr;
        std::string ifExpr;
        std::string structName;
        std::string enumName;
        std::string flagsName;
        std::string pointsTo;
        std::string typeText;
        BaseType base = BaseType::Invalid;
        int arrayLen = 0;
        int bitLo = -1;
        int bitHi = -1;
    };
    struct StructDef
    {
        std::string name;
        bool hasSize = false;
        size_t size = 0;
        std::vector<FieldDef> fields;
    };
    struct ChunkDef
    {
        uint32_t id = 0;
        std::string name;
        std::string version;
        std::vector<FieldDef> fields;
    };
    struct FlagBit
    {
        uint32_t mask = 0;
        std::string name;
    };
    struct FlagSet
    {
        std::vector<FlagBit> bits;
        std::vector<std::pair<uint32_t, std::string>> values;
    };
    struct Schema
    {
        std::string name;
        std::string platform;
        bool bigEndian = true;
        std::map<std::string, std::map<int64_t, std::string>> enums;
        std::map<std::string, FlagSet> flagSets;
        std::map<std::string, StructDef> structs;
        std::map<uint32_t, ChunkDef> chunks;
    };
    inline BaseType ParseBaseType(const std::string& t)
    {
        if (t == "u8") return BaseType::U8;
        if (t == "u16") return BaseType::U16;
        if (t == "u32") return BaseType::U32;
        if (t == "u64") return BaseType::U64;
        if (t == "s8" || t == "i8") return BaseType::I8;
        if (t == "s16" || t == "i16") return BaseType::I16;
        if (t == "s32" || t == "i32") return BaseType::I32;
        if (t == "s64" || t == "i64") return BaseType::I64;
        if (t == "f16") return BaseType::F16;
        if (t == "f32") return BaseType::F32;
        if (t == "f64") return BaseType::F64;
        if (t == "char") return BaseType::Char;
        if (t == "cstring") return BaseType::CString;
        if (t == "bytes") return BaseType::Bytes;
        return BaseType::Invalid;
    }
    inline FieldDef ParseFieldDef(const XmlNode& n)
    {
        FieldDef f;
        f.name = n.Attr("name");
        const std::string off = n.Attr("offset");
        f.offset = off.empty() ? "next" : off;
        f.countExpr = n.Attr("count");
        f.sizeExpr = n.Attr("size");
        f.ifExpr = n.Attr("if");
        f.enumName = n.Attr("enum");
        f.flagsName = n.Attr("flags");
        f.pointsTo = n.Attr("pointsTo");
        f.structName = n.Attr("struct");
        const std::string bits = n.Attr("bits");
        if (!bits.empty())
        {
            f.bitLo = std::atoi(bits.c_str());
            const size_t dash = bits.find('-');
            f.bitHi = dash == std::string::npos ? f.bitLo : std::atoi(bits.c_str() + dash + 1);
        }
        if (!f.structName.empty())
        {
            f.base = BaseType::Struct;
            f.typeText = f.structName;
            return f;
        }
        const std::string type = n.Attr("type");
        f.typeText = type;
        const size_t lb = type.find('[');
        if (lb != std::string::npos)
            f.arrayLen = std::atoi(type.c_str() + lb + 1);
        f.base = ParseBaseType(lb == std::string::npos ? type : type.substr(0, lb));
        return f;
    }
    inline void CollectSchema(const XmlNode& n, Schema& out)
    {
        if (n.name == "enum")
        {
            auto& map = out.enums[n.Attr("name")];
            for (const XmlNode& v : n.children)
            {
                if (v.name != "value")
                    continue;
                std::string key = v.Attr("value");
                if (key.empty())
                    key = v.Attr("key");
                map[static_cast<int64_t>(std::strtoll(key.c_str(), nullptr, 0))] = v.Attr("name");
            }
            return;
        }
        if (n.name == "flagSet")
        {
            FlagSet& fs = out.flagSets[n.Attr("name")];
            for (const XmlNode& v : n.children)
            {
                if (v.name == "bit")
                {
                    FlagBit b;
                    b.mask = static_cast<uint32_t>(std::strtoul(v.Attr("mask").c_str(), nullptr, 0));
                    b.name = v.Attr("name");
                    fs.bits.push_back(std::move(b));
                }
                else if (v.name == "value")
                {
                    fs.values.emplace_back(static_cast<uint32_t>(std::strtoul(v.Attr("value").c_str(), nullptr, 0)),
                        v.Attr("name"));
                }
            }
            return;
        }
        if (n.name == "struct")
        {
            StructDef sd;
            sd.name = n.Attr("name");
            const std::string size = n.Attr("size");
            if (!size.empty())
            {
                sd.hasSize = true;
                sd.size = static_cast<size_t>(std::strtoull(size.c_str(), nullptr, 0));
            }
            for (const XmlNode& c : n.children)
                if (c.name == "field")
                    sd.fields.push_back(ParseFieldDef(c));
            out.structs[sd.name] = std::move(sd);
            return;
        }
        if (n.name == "chunk")
        {
            ChunkDef cd;
            cd.id = static_cast<uint32_t>(std::strtoul(n.Attr("id").c_str(), nullptr, 0));
            cd.name = n.Attr("name");
            cd.version = n.Attr("version");
            for (const XmlNode& c : n.children)
                if (c.name == "field")
                    cd.fields.push_back(ParseFieldDef(c));
            if (cd.id != 0)
                out.chunks[cd.id] = std::move(cd);
            return;
        }
        if (n.name == "chunkHeader")
            return;
        for (const XmlNode& c : n.children)
            CollectSchema(c, out);
    }
    inline bool LoadSchemaText(const std::string& xml, Schema& out, std::string& error)
    {
        XmlNode root;
        if (!ParseXml(xml, root))
        {
            error = "schema is not well-formed XML";
            return false;
        }
        Schema schema;
        schema.name = root.Attr("game");
        if (schema.name.empty())
            schema.name = root.Attr("name");
        schema.platform = root.Attr("platform");
        const std::string endian = root.Attr("endian");
        schema.bigEndian = endian != "little";
        for (const XmlNode& c : root.children)
            CollectSchema(c, schema);
        if (schema.chunks.empty())
        {
            error = "schema defines no chunks";
            return false;
        }
        out = std::move(schema);
        return true;
    }
    inline bool LoadSchemaFile(const std::string& path, Schema& out, std::string& error)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
        {
            error = "cannot open schema: " + path;
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 0)
        {
            std::fclose(f);
            error = "cannot read schema size";
            return false;
        }
        std::string text(static_cast<size_t>(sz), '\0');
        if (sz > 0 && std::fread(&text[0], 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz))
        {
            std::fclose(f);
            error = "cannot read schema data";
            return false;
        }
        std::fclose(f);
        return LoadSchemaText(text, out, error);
    }
    struct Scope
    {
        std::map<std::string, int64_t> vars;
        std::map<std::string, std::vector<std::map<std::string, int64_t>>> arrays;
    };
    struct Row
    {
        int depth = 0;
        std::string name;
        std::string offset;
        std::string type;
        std::string value;
    };
    using ChunkFinder = std::function<const CMChunk* (uint32_t id, const CMChunk& from)>;
    inline std::string Hex(uint64_t v)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(v));
        return buf;
    }
    inline int PrimSize(BaseType t)
    {
        switch (t)
        {
        case BaseType::U8:
        case BaseType::I8:
        case BaseType::Char: return 1;
        case BaseType::U16:
        case BaseType::I16:
        case BaseType::F16: return 2;
        case BaseType::U32:
        case BaseType::I32:
        case BaseType::F32: return 4;
        case BaseType::U64:
        case BaseType::I64:
        case BaseType::F64: return 8;
        default: return 1;
        }
    }
    inline bool IsSignedType(BaseType t)
    {
        return t == BaseType::I8 || t == BaseType::I16 || t == BaseType::I32 || t == BaseType::I64;
    }
    inline bool IsIntegerType(BaseType t)
    {
        switch (t)
        {
        case BaseType::U8:
        case BaseType::U16:
        case BaseType::U32:
        case BaseType::U64:
        case BaseType::I8:
        case BaseType::I16:
        case BaseType::I32:
        case BaseType::I64: return true;
        default: return false;
        }
    }
    inline float HalfToFloat(uint16_t h)
    {
        uint32_t sign = (h >> 15) & 1u;
        uint32_t exp = (h >> 10) & 0x1Fu;
        uint32_t man = h & 0x3FFu;
        uint32_t out;
        if (exp == 0)
        {
            if (man == 0)
            {
                out = sign << 31;
            }
            else
            {
                int e = 1;
                while (!(man & 0x400u))
                {
                    man <<= 1;
                    --e;
                }
                man &= 0x3FFu;
                out = (sign << 31) | (static_cast<uint32_t>(e + 112) << 23) | (man << 13);
            }
        }
        else if (exp == 31)
        {
            out = (sign << 31) | 0x7F800000u | (man << 13);
        }
        else
        {
            out = (sign << 31) | ((exp + 112u) << 23) | (man << 13);
        }
        float f;
        std::memcpy(&f, &out, sizeof(f));
        return f;
    }
    inline std::string FormatF32(float f)
    {
        if (std::isnan(f))
            return "nan";
        if (std::isinf(f))
            return f < 0 ? "-inf" : "inf";
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(f));
        if (std::strtof(buf, nullptr) != f)
            std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(f));
        return buf;
    }
    inline std::string FormatF64(double d)
    {
        if (std::isnan(d))
            return "nan";
        if (std::isinf(d))
            return d < 0 ? "-inf" : "inf";
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.15g", d);
        if (std::strtod(buf, nullptr) != d)
            std::snprintf(buf, sizeof(buf), "%.17g", d);
        return buf;
    }
    inline void AppendEscaped(std::string& out, uint8_t c, char quote)
    {
        if (c == static_cast<uint8_t>(quote) || c == '\\')
        {
            out += '\\';
            out += static_cast<char>(c);
        }
        else if (c >= 0x20 && c < 0x7F)
        {
            out += static_cast<char>(c);
        }
        else if (c == '\n')
        {
            out += "\\n";
        }
        else if (c == '\r')
        {
            out += "\\r";
        }
        else if (c == '\t')
        {
            out += "\\t";
        }
        else
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            out += buf;
        }
    }
    inline std::string QuoteString(const uint8_t* p, size_t n)
    {
        std::string s = "\"";
        for (size_t i = 0; i < n && p[i] != 0; ++i)
            AppendEscaped(s, p[i], '"');
        s += '"';
        return s;
    }
    inline std::string HexLine(const uint8_t* p, size_t n)
    {
        std::string s;
        char buf[8];
        for (size_t i = 0; i < n; ++i)
        {
            std::snprintf(buf, sizeof(buf), i ? " %02X" : "%02X", p[i]);
            s += buf;
        }
        if (n > 4)
        {
            s += "  |";
            for (size_t i = 0; i < n; ++i)
                s += (p[i] >= 0x20 && p[i] < 0x7F) ? static_cast<char>(p[i]) : '.';
            s += '|';
        }
        return s;
    }
    inline std::string JoinStrings(const std::vector<std::string>& v, size_t from, size_t to, const char* sep)
    {
        std::string out;
        for (size_t i = from; i < to && i < v.size(); ++i)
        {
            if (i != from)
                out += sep;
            out += v[i];
        }
        return out;
    }
    inline int CountTrailingZeros(uint32_t v)
    {
        int n = 0;
        while (v && !(v & 1u))
        {
            v >>= 1;
            ++n;
        }
        return n;
    }
    class Decoder
    {
    public:
        static constexpr size_t kHexRowLimit = 8192;
        static constexpr int64_t kMaxRecords = 5000000;
        Decoder(const Schema* schema, ChunkFinder finder) : mSchema(schema), mFinder(std::move(finder)) {}
        std::string ChunkLabel(const CMChunk& c) const
        {
            auto it = mSchema->chunks.find(c.GetMaskedID());
            if (it != mSchema->chunks.end() && !it->second.name.empty())
                return it->second.name;
            return ToString(c.GetIDToEnum());
        }
        bool HasLayout(const CMChunk& c) const
        {
            auto it = mSchema->chunks.find(c.GetMaskedID());
            return it != mSchema->chunks.end() && !it->second.fields.empty();
        }
        void Describe(const CMChunk& c, std::vector<Row>& rows, size_t maxRows)
        {
            Run r;
            r.chunk = &c;
            r.data = c.data.data();
            r.size = c.data.size();
            r.le = c.isLittleEndian;
            r.rows = &rows;
            r.maxRows = maxRows;
            Add(r, 0, "Chunk", "", "", "");
            Add(r, 1, "Name", "", "string", ChunkLabel(c));
            Add(r, 1, "ID", "", "u24", Hex(c.GetMaskedID()));
            Add(r, 1, "Version", "", "u16", std::to_string(c.GetVersion()));
            Add(r, 1, "Has Children", "", "u16", c.GetHasChildren() ? "1 (HAS_CHILDREN)" : "0");
            Add(r, 1, "Length", "", c.HasWideLength() ? "u64" : "u32",
                std::to_string(c.GetLength()) + " (" + Hex(c.GetLength()) + ")");
            Add(r, 1, "Offset", "", "", Hex(c.offset));
            Add(r, 1, "Header Size", "", "", std::to_string(c.HeaderSize()));
            auto def = mSchema->chunks.find(c.GetMaskedID());
            if (def != mSchema->chunks.end() && !def->second.version.empty() &&
                def->second.version != std::to_string(c.GetVersion()))
                Add(r, 1, "Schema Version", "", "", def->second.version + " (chunk is v" + std::to_string(c.GetVersion()) + ")");
            if (c.GetHasChildren())
            {
                Add(r, 0, "Children (" + std::to_string(c.children.size()) + ")", "", "", "");
                for (const CMChunk& child : c.children)
                {
                    Add(r, 1, ChunkLabel(child), Hex(child.offset), child.GetHasChildren() ? "container" : "leaf",
                        "id " + Hex(child.GetMaskedID()) + "  v" + std::to_string(child.GetVersion()) + "  len " +
                        std::to_string(child.GetLength()));
                }
            }
            else if (def != mSchema->chunks.end() && !def->second.fields.empty())
            {
                Add(r, 0, "Fields", "", "", "");
                Scope scope;
                scope.vars["version"] = c.GetVersion();
                r.scopes.push_back(&scope);
                size_t cursor = 0;
                size_t maxEnd = 0;
                DecodeFields(r, def->second.fields, 0, cursor, maxEnd, 1, scope);
                if (r.failed)
                    Add(r, 1, "error", "", "", r.error);
                else if (maxEnd < r.size)
                    EmitBytes(r, 1, "unparsed", maxEnd, r.size - maxEnd);
            }
            else
            {
                Add(r, 0, "Data", "", "", "");
                if (r.size == 0)
                    Add(r, 1, "payload", "", "", "empty");
                else
                    EmitBytes(r, 1, "payload", 0, r.size);
            }
            if (r.truncated)
            {
                Row row;
                row.name = "output truncated at " + std::to_string(maxRows) + " rows";
                rows.push_back(std::move(row));
            }
        }
    private:
        struct Run
        {
            const CMChunk* chunk = nullptr;
            const uint8_t* data = nullptr;
            size_t size = 0;
            bool le = false;
            std::vector<Row>* rows = nullptr;
            size_t maxRows = 0;
            bool truncated = false;
            bool failed = false;
            std::string error;
            std::vector<Scope*> scopes;
            int nesting = 0;
        };
        struct CountSpec
        {
            enum Kind { Fixed, UntilEnd, UntilValue } kind = Fixed;
            int64_t n = 1;
            std::string field;
            int64_t value = 0;
        };
        const Schema* mSchema;
        ChunkFinder mFinder;
        std::map<const CMChunk*, std::map<std::string, int64_t>> mCache;
        std::set<const CMChunk*> mActive;
        static bool Room(const Run& r) { return r.rows && r.rows->size() < r.maxRows; }
        static void Add(Run& r, int depth, std::string name, std::string offset, std::string type, std::string value)
        {
            if (!r.rows)
                return;
            if (r.rows->size() >= r.maxRows)
            {
                r.truncated = true;
                return;
            }
            Row row;
            row.depth = depth;
            row.name = std::move(name);
            row.offset = std::move(offset);
            row.type = std::move(type);
            row.value = std::move(value);
            r.rows->push_back(std::move(row));
        }
        static void Fail(Run& r, const std::string& message)
        {
            r.failed = true;
            if (r.error.empty())
                r.error = message;
        }
        static bool Need(Run& r, size_t pos, size_t n)
        {
            if (n > r.size || pos > r.size - n)
            {
                Fail(r, "read of " + std::to_string(n) + " byte(s) at " + Hex(pos) + " runs past the end of the payload (" +
                    std::to_string(r.size) + " bytes)");
                return false;
            }
            return true;
        }
        static uint64_t ReadUInt(const Run& r, size_t pos, int bytes)
        {
            uint64_t v = 0;
            if (r.le)
            {
                for (int i = bytes - 1; i >= 0; --i)
                    v = (v << 8) | r.data[pos + static_cast<size_t>(i)];
            }
            else
            {
                for (int i = 0; i < bytes; ++i)
                    v = (v << 8) | r.data[pos + static_cast<size_t>(i)];
            }
            return v;
        }
        static int64_t SignExtend(uint64_t raw, int bytes)
        {
            switch (bytes)
            {
            case 1: return static_cast<int8_t>(raw);
            case 2: return static_cast<int16_t>(raw);
            case 4: return static_cast<int32_t>(raw);
            default: return static_cast<int64_t>(raw);
            }
        }
        const std::map<std::string, int64_t>& ChunkVars(const CMChunk& c)
        {
            static const std::map<std::string, int64_t> kEmpty;
            auto cached = mCache.find(&c);
            if (cached != mCache.end())
                return cached->second;
            if (mActive.count(&c))
                return kEmpty;
            mActive.insert(&c);
            std::map<std::string, int64_t> vars;
            auto def = mSchema->chunks.find(c.GetMaskedID());
            if (def != mSchema->chunks.end() && !c.GetHasChildren())
            {
                Run r;
                r.chunk = &c;
                r.data = c.data.data();
                r.size = c.data.size();
                r.le = c.isLittleEndian;
                Scope scope;
                scope.vars["version"] = c.GetVersion();
                r.scopes.push_back(&scope);
                size_t cursor = 0;
                size_t maxEnd = 0;
                DecodeFields(r, def->second.fields, 0, cursor, maxEnd, 0, scope);
                vars = scope.vars;
            }
            mActive.erase(&c);
            return mCache[&c] = std::move(vars);
        }
        int64_t Resolve(const std::string& tok, Run& r)
        {
            if (tok.empty())
                return 0;
            if (std::isdigit(static_cast<unsigned char>(tok[0])))
            {
                const size_t dot = tok.find('.');
                if (dot == std::string::npos)
                    return static_cast<int64_t>(std::strtoull(tok.c_str(), nullptr, 0));
                const uint32_t id = static_cast<uint32_t>(std::strtoul(tok.substr(0, dot).c_str(), nullptr, 0));
                const std::string field = tok.substr(dot + 1);
                const CMChunk* other = mFinder ? mFinder(id, *r.chunk) : nullptr;
                if (!other)
                    return 0;
                const auto& vars = ChunkVars(*other);
                auto it = vars.find(field);
                return it == vars.end() ? 0 : it->second;
            }
            for (size_t i = r.scopes.size(); i-- > 0;)
            {
                auto it = r.scopes[i]->vars.find(tok);
                if (it != r.scopes[i]->vars.end())
                    return it->second;
            }
            return 0;
        }
        struct Expr
        {
            const std::string& s;
            size_t p;
            Decoder& d;
            Run& r;
            Expr(const std::string& text, Decoder& dec, Run& run) : s(text), p(0), d(dec), r(run) {}
            void Skip()
            {
                while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
                    ++p;
            }
            static int Prec(const std::string& op)
            {
                if (op == "||") return 1;
                if (op == "&&") return 2;
                if (op == "|") return 3;
                if (op == "^") return 4;
                if (op == "&") return 5;
                if (op == "==" || op == "!=") return 6;
                if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
                if (op == "<<" || op == ">>") return 8;
                if (op == "+" || op == "-") return 9;
                return 10;
            }
            bool PeekOp(std::string& op, int& prec)
            {
                Skip();
                if (p >= s.size())
                    return false;
                static const char* const two[] = { "||", "&&", "==", "!=", "<=", ">=", "<<", ">>" };
                op.clear();
                for (const char* t : two)
                {
                    if (s.compare(p, 2, t) == 0)
                    {
                        op = t;
                        break;
                    }
                }
                if (op.empty())
                {
                    const char c = s[p];
                    if (c != 0 && std::strchr("|^&<>+-*/%", c))
                        op = std::string(1, c);
                    else
                        return false;
                }
                prec = Prec(op);
                return true;
            }
            static int64_t Apply(const std::string& op, int64_t a, int64_t b)
            {
                if (op == "||") return (a || b) ? 1 : 0;
                if (op == "&&") return (a && b) ? 1 : 0;
                if (op == "|") return a | b;
                if (op == "^") return a ^ b;
                if (op == "&") return a & b;
                if (op == "==") return a == b;
                if (op == "!=") return a != b;
                if (op == "<") return a < b;
                if (op == ">") return a > b;
                if (op == "<=") return a <= b;
                if (op == ">=") return a >= b;
                if (op == "<<") return (b >= 0 && b < 63) ? (a << b) : 0;
                if (op == ">>") return (b >= 0 && b < 63) ? (a >> b) : 0;
                if (op == "+") return a + b;
                if (op == "-") return a - b;
                if (op == "*") return a * b;
                if (op == "/") return b ? a / b : 0;
                if (op == "%") return b ? a % b : 0;
                return 0;
            }
            int64_t Binary(int minPrec)
            {
                int64_t lhs = Unary();
                for (;;)
                {
                    std::string op;
                    int prec = 0;
                    if (!PeekOp(op, prec) || prec < minPrec)
                        break;
                    p += op.size();
                    const int64_t rhs = Binary(prec + 1);
                    lhs = Apply(op, lhs, rhs);
                }
                return lhs;
            }
            int64_t Unary()
            {
                Skip();
                if (p < s.size() && s[p] == '-')
                {
                    ++p;
                    return -Unary();
                }
                if (p < s.size() && s[p] == '!')
                {
                    ++p;
                    return Unary() ? 0 : 1;
                }
                if (p < s.size() && s[p] == '(')
                {
                    ++p;
                    const int64_t v = Binary(1);
                    Skip();
                    if (p < s.size() && s[p] == ')')
                        ++p;
                    return v;
                }
                if (p < s.size() && s[p] == '@')
                    ++p;
                const size_t start = p;
                while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '_' || s[p] == '.'))
                    ++p;
                if (p == start)
                {
                    if (p < s.size())
                        ++p;
                    return 0;
                }
                return d.Resolve(s.substr(start, p - start), r);
            }
        };
        int64_t Eval(const std::string& expr, Run& r)
        {
            Expr e(expr, *this, r);
            return e.Binary(1);
        }
        CountSpec ParseCount(const std::string& expr, Run& r)
        {
            CountSpec c;
            if (expr == "rest")
            {
                c.kind = CountSpec::UntilEnd;
                return c;
            }
            if (expr.compare(0, 6, "until:") == 0)
            {
                const std::string body = expr.substr(6);
                const size_t eq = body.find('=');
                c.kind = CountSpec::UntilValue;
                c.field = body.substr(0, eq);
                if (eq != std::string::npos)
                    c.value = static_cast<int64_t>(std::strtoull(body.c_str() + eq + 1, nullptr, 0));
                return c;
            }
            if (expr.compare(0, 4, "sum:") == 0)
            {
                const std::string body = expr.substr(4);
                const size_t dot = body.find('.');
                const std::string arr = body.substr(0, dot);
                const std::string fld = dot == std::string::npos ? std::string() : body.substr(dot + 1);
                c.n = 0;
                for (size_t i = r.scopes.size(); i-- > 0;)
                {
                    auto it = r.scopes[i]->arrays.find(arr);
                    if (it == r.scopes[i]->arrays.end())
                        continue;
                    for (const auto& rec : it->second)
                    {
                        auto f = rec.find(fld);
                        if (f != rec.end())
                            c.n += f->second;
                    }
                    break;
                }
                return c;
            }
            c.n = Eval(expr, r);
            return c;
        }
        std::string FormatInt(const FieldDef& f, uint64_t raw, bool isSigned, int bytes, bool compact) const
        {
            if (f.bitLo >= 0)
            {
                const int width = f.bitHi - f.bitLo + 1;
                const uint64_t mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
                return std::to_string((raw >> f.bitLo) & mask);
            }
            const int64_t sv = SignExtend(raw, bytes);
            if (!f.enumName.empty())
            {
                auto it = mSchema->enums.find(f.enumName);
                if (it != mSchema->enums.end())
                {
                    auto jt = it->second.find(isSigned ? sv : static_cast<int64_t>(raw));
                    if (jt != it->second.end())
                        return std::to_string(isSigned ? sv : static_cast<int64_t>(raw)) + " (" + jt->second + ")";
                }
            }
            if (!f.flagsName.empty())
            {
                std::string out = Hex(raw);
                auto it = mSchema->flagSets.find(f.flagsName);
                if (it != mSchema->flagSets.end())
                {
                    const std::string label = FormatFlags(it->second, raw);
                    if (!label.empty())
                        out += " (" + label + ")";
                }
                return out;
            }
            if (isSigned)
                return std::to_string(sv);
            std::string s = std::to_string(raw);
            if (!compact && raw > 9)
                s += " (" + Hex(raw) + ")";
            return s;
        }
        static std::string FormatFlags(const FlagSet& fs, uint64_t v)
        {
            for (const auto& kv : fs.values)
                if (kv.first == v)
                    return kv.second;
            std::string out;
            uint64_t left = v;
            for (const FlagBit& b : fs.bits)
            {
                if (!b.mask || !(v & b.mask))
                    continue;
                if (!out.empty())
                    out += " | ";
                if ((b.mask & (b.mask - 1)) == 0)
                    out += b.name;
                else
                    out += b.name + "=" + std::to_string((v & b.mask) >> CountTrailingZeros(b.mask));
                left &= ~static_cast<uint64_t>(b.mask);
            }
            if (left)
            {
                if (!out.empty())
                    out += " | ";
                out += Hex(left);
            }
            return out;
        }
        std::string FormatScalar(const Run& r, const FieldDef& f, size_t pos, bool compact) const
        {
            switch (f.base)
            {
            case BaseType::F32:
            {
                const uint32_t u = static_cast<uint32_t>(ReadUInt(r, pos, 4));
                float v;
                std::memcpy(&v, &u, sizeof(v));
                return FormatF32(v);
            }
            case BaseType::F64:
            {
                const uint64_t u = ReadUInt(r, pos, 8);
                double v;
                std::memcpy(&v, &u, sizeof(v));
                return FormatF64(v);
            }
            case BaseType::F16:
                return FormatF32(HalfToFloat(static_cast<uint16_t>(ReadUInt(r, pos, 2))));
            default:
            {
                const int bytes = PrimSize(f.base);
                return FormatInt(f, ReadUInt(r, pos, bytes), IsSignedType(f.base), bytes, compact);
            }
            }
        }
        std::string FormatItem(const Run& r, const FieldDef& f, size_t pos) const
        {
            if (f.base == BaseType::Char)
            {
                if (f.arrayLen > 0)
                    return QuoteString(r.data + pos, static_cast<size_t>(f.arrayLen));
                const uint8_t c = r.data[pos];
                std::string s = "'";
                AppendEscaped(s, c, '\'');
                s += "' (" + Hex(c) + ")";
                return s;
            }
            const int elems = std::max(1, f.arrayLen);
            if (elems == 1)
                return FormatScalar(r, f, pos, false);
            std::string s = "[";
            const int sz = PrimSize(f.base);
            for (int e = 0; e < elems; ++e)
            {
                if (e)
                    s += ", ";
                s += FormatScalar(r, f, pos + static_cast<size_t>(e) * static_cast<size_t>(sz), true);
            }
            s += "]";
            return s;
        }
        int64_t ReadVar(const Run& r, const FieldDef& f, size_t pos) const
        {
            if (f.base == BaseType::F32)
            {
                const uint32_t u = static_cast<uint32_t>(ReadUInt(r, pos, 4));
                float v;
                std::memcpy(&v, &u, sizeof(v));
                return static_cast<int64_t>(v);
            }
            if (f.base == BaseType::F16)
                return static_cast<int64_t>(HalfToFloat(static_cast<uint16_t>(ReadUInt(r, pos, 2))));
            if (f.base == BaseType::F64)
            {
                const uint64_t u = ReadUInt(r, pos, 8);
                double v;
                std::memcpy(&v, &u, sizeof(v));
                return static_cast<int64_t>(v);
            }
            const int bytes = PrimSize(f.base);
            const uint64_t raw = ReadUInt(r, pos, bytes);
            if (f.bitLo >= 0)
            {
                const int width = f.bitHi - f.bitLo + 1;
                const uint64_t mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
                return static_cast<int64_t>((raw >> f.bitLo) & mask);
            }
            return IsSignedType(f.base) ? SignExtend(raw, bytes) : static_cast<int64_t>(raw);
        }
        static void EmitBytes(Run& r, int depth, const std::string& name, size_t pos, size_t len)
        {
            const std::string type = "bytes[" + std::to_string(len) + "]";
            if (len <= 16)
            {
                Add(r, depth, name, Hex(pos), type, len ? HexLine(r.data + pos, len) : "(empty)");
                return;
            }
            Add(r, depth, name, Hex(pos), type, "");
            size_t done = 0;
            size_t rowsEmitted = 0;
            while (done < len && rowsEmitted < kHexRowLimit && Room(r))
            {
                const size_t n = std::min<size_t>(16, len - done);
                char label[24];
                std::snprintf(label, sizeof(label), "+0x%04zX", done);
                Add(r, depth + 1, label, Hex(pos + done), "", HexLine(r.data + pos + done, n));
                done += n;
                ++rowsEmitted;
            }
            if (done < len)
                Add(r, depth + 1, "...", "", "", std::to_string(len - done) + " more byte(s) not shown");
        }
        bool DecodeFields(Run& r, const std::vector<FieldDef>& fields, size_t base, size_t& cursor, size_t& maxEnd,
            int depth, Scope& scope)
        {
            for (const FieldDef& f : fields)
            {
                if (r.failed)
                    return false;
                if (!f.ifExpr.empty() && Eval(f.ifExpr, r) == 0)
                    continue;
                size_t pos = cursor;
                if (f.offset != "next")
                {
                    const int64_t o = Eval(f.offset, r);
                    if (o < 0)
                    {
                        Fail(r, "field " + f.name + " has a negative offset");
                        return false;
                    }
                    pos = base + static_cast<size_t>(o);
                }
                const bool ok = f.base == BaseType::Struct ? DecodeStructField(r, f, pos, depth, scope, cursor)
                    : DecodeValueField(r, f, pos, depth, scope, cursor);
                if (!ok)
                    return false;
                maxEnd = std::max(maxEnd, cursor);
            }
            return !r.failed;
        }
        bool DecodeStruct(Run& r, const StructDef& sd, size_t start, int depth, size_t& endOut,
            std::map<std::string, int64_t>& varsOut)
        {
            if (r.nesting >= 24)
            {
                Fail(r, "structures are nested too deeply");
                return false;
            }
            Scope scope;
            r.scopes.push_back(&scope);
            ++r.nesting;
            size_t cursor = start;
            size_t maxEnd = start;
            const bool ok = DecodeFields(r, sd.fields, start, cursor, maxEnd, depth, scope);
            --r.nesting;
            r.scopes.pop_back();
            varsOut = std::move(scope.vars);
            endOut = sd.hasSize ? std::max(start + sd.size, maxEnd) : maxEnd;
            return ok;
        }
        bool DecodeStructField(Run& r, const FieldDef& f, size_t pos, int depth, Scope& scope, size_t& cursor)
        {
            auto it = mSchema->structs.find(f.structName);
            if (it == mSchema->structs.end())
            {
                Add(r, depth, f.name, Hex(pos), f.structName, "<undefined struct>");
                cursor = pos;
                return true;
            }
            const StructDef& sd = it->second;
            const bool arrayed = !f.countExpr.empty() || !f.sizeExpr.empty();
            if (!arrayed)
            {
                Add(r, depth, f.name, Hex(pos), sd.name, "");
                size_t end = pos;
                std::map<std::string, int64_t> vars;
                if (!DecodeStruct(r, sd, pos, depth + 1, end, vars))
                    return false;
                cursor = end;
                return true;
            }
            const bool headerAdded = Room(r);
            const size_t headerIdx = r.rows ? r.rows->size() : 0;
            Add(r, depth, f.name, Hex(pos), sd.name + "[]", "");
            CountSpec cs;
            size_t regionEnd = r.size;
            if (!f.sizeExpr.empty())
            {
                int64_t sz = Eval(f.sizeExpr, r);
                if (sz < 0)
                    sz = 0;
                if (!Need(r, pos, static_cast<size_t>(sz)))
                    return false;
                regionEnd = pos + static_cast<size_t>(sz);
                cs.kind = CountSpec::UntilEnd;
            }
            else
            {
                cs = ParseCount(f.countExpr, r);
                if (cs.kind == CountSpec::Fixed && cs.n > static_cast<int64_t>(r.size))
                {
                    Fail(r, "field " + f.name + " has " + std::to_string(cs.n) + " elements, more than the payload can hold");
                    return false;
                }
            }
            auto& store = scope.arrays[f.name];
            store.clear();
            size_t p = pos;
            int64_t i = 0;
            for (;;)
            {
                if (cs.kind == CountSpec::Fixed && i >= cs.n)
                    break;
                if (cs.kind != CountSpec::Fixed && p >= regionEnd)
                    break;
                if (i >= kMaxRecords)
                    break;
                Add(r, depth + 1, "[" + std::to_string(i) + "]", Hex(p), sd.name, "");
                size_t end = p;
                std::map<std::string, int64_t> vars;
                if (!DecodeStruct(r, sd, p, depth + 2, end, vars))
                    return false;
                ++i;
                bool stop = false;
                if (cs.kind == CountSpec::UntilValue)
                {
                    auto vt = vars.find(cs.field);
                    stop = vt != vars.end() && vt->second == cs.value;
                }
                store.push_back(std::move(vars));
                const bool progressed = end > p;
                p = end;
                if (stop || !progressed)
                    break;
            }
            cursor = p;
            if (headerAdded)
                (*r.rows)[headerIdx].type = sd.name + "[" + std::to_string(i) + "]";
            return true;
        }
        bool DecodeValueField(Run& r, const FieldDef& f, size_t pos, int depth, Scope& scope, size_t& cursor)
        {
            if (f.base == BaseType::Invalid)
            {
                Add(r, depth, f.name, Hex(pos), f.typeText, "<unsupported type>");
                cursor = pos;
                return true;
            }
            if (f.base == BaseType::Bytes)
            {
                size_t len = pos < r.size ? r.size - pos : 0;
                if (!f.sizeExpr.empty())
                {
                    const int64_t v = Eval(f.sizeExpr, r);
                    len = v < 0 ? 0 : static_cast<size_t>(v);
                }
                if (!Need(r, pos, len))
                    return false;
                EmitBytes(r, depth, f.name, pos, len);
                cursor = pos + len;
                return true;
            }
            const bool arrayed = !f.countExpr.empty();
            const int elems = f.base == BaseType::Char ? 1 : std::max(1, f.arrayLen);
            size_t itemBytes;
            if (f.base == BaseType::Char)
                itemBytes = f.arrayLen > 0 ? static_cast<size_t>(f.arrayLen) : 1;
            else if (f.base == BaseType::CString)
                itemBytes = 1;
            else
                itemBytes = static_cast<size_t>(PrimSize(f.base)) * static_cast<size_t>(elems);
            int64_t n = 1;
            if (arrayed)
            {
                const CountSpec cs = ParseCount(f.countExpr, r);
                if (cs.kind == CountSpec::UntilEnd)
                    n = static_cast<int64_t>((pos < r.size ? r.size - pos : 0) / itemBytes);
                else if (cs.kind == CountSpec::Fixed)
                    n = cs.n;
                else
                    n = 0;
                if (n < 0)
                    n = 0;
                if (n > static_cast<int64_t>(r.size))
                {
                    Fail(r, "field " + f.name + " has " + std::to_string(n) + " elements, more than the payload can hold");
                    return false;
                }
            }
            const bool scalarItems = elems == 1 && f.base != BaseType::Char && f.base != BaseType::CString;
            const size_t budget = Room(r) ? (r.maxRows - r.rows->size()) * (scalarItems ? 16 : 1) : 0;
            std::vector<std::string> vals;
            std::vector<size_t> positions;
            size_t p = pos;
            for (int64_t i = 0; i < n; ++i)
            {
                size_t len;
                std::string text;
                const bool emit = static_cast<size_t>(i) < budget;
                if (f.base == BaseType::CString)
                {
                    if (p > r.size)
                    {
                        Fail(r, "string at " + Hex(p) + " is past the end of the payload");
                        return false;
                    }
                    size_t e = p;
                    while (e < r.size && r.data[e])
                        ++e;
                    len = e < r.size ? e - p + 1 : e - p;
                    if (emit)
                        text = QuoteString(r.data + p, e - p);
                }
                else
                {
                    len = itemBytes;
                    if (!Need(r, p, len))
                        return false;
                    if (emit)
                        text = FormatItem(r, f, p);
                }
                if (i == 0 && !f.name.empty() &&
                    ((IsIntegerType(f.base) || f.base == BaseType::F32 || f.base == BaseType::F16 || f.base == BaseType::F64) &&
                        elems == 1 ||
                        (f.base == BaseType::Char && f.arrayLen == 0)))
                {
                    if (f.base == BaseType::Char)
                        scope.vars[f.name] = r.data[p];
                    else
                        scope.vars[f.name] = ReadVar(r, f, p);
                }
                if (emit)
                {
                    vals.push_back(std::move(text));
                    positions.push_back(p);
                }
                p += len;
            }
            if (!arrayed && f.base == BaseType::CString)
            {
                if (pos > r.size)
                {
                    Fail(r, "string at " + Hex(pos) + " is past the end of the payload");
                    return false;
                }
            }
            cursor = p;
            std::string type = f.typeText;
            if (arrayed)
                type += "[" + std::to_string(n) + "]";
            if (!f.pointsTo.empty())
                type += " -> " + f.pointsTo;
            const std::string off = Hex(pos);
            if (!arrayed)
            {
                Add(r, depth, f.name, off, type, vals.empty() ? std::string() : vals[0]);
                return true;
            }
            if (n == 0)
            {
                Add(r, depth, f.name, off, type, "[]");
                return true;
            }
            if (scalarItems && n <= 16)
            {
                Add(r, depth, f.name, off, type, "[" + JoinStrings(vals, 0, vals.size(), ", ") + "]");
                return true;
            }
            if (!scalarItems && n == 1)
            {
                Add(r, depth, f.name, off, type, vals.empty() ? std::string() : vals[0]);
                return true;
            }
            Add(r, depth, f.name, off, type, "");
            if (scalarItems)
            {
                for (size_t g = 0; g < vals.size(); g += 16)
                {
                    const size_t e = std::min(vals.size(), g + 16);
                    Add(r, depth + 1, "[" + std::to_string(g) + ".." + std::to_string(e - 1) + "]", Hex(positions[g]), "",
                        JoinStrings(vals, g, e, ", "));
                }
            }
            else
            {
                for (size_t k = 0; k < vals.size(); ++k)
                    Add(r, depth + 1, "[" + std::to_string(k) + "]", Hex(positions[k]), "", vals[k]);
            }
            if (static_cast<size_t>(n) > vals.size())
                Add(r, depth + 1, "...", "", "", std::to_string(static_cast<size_t>(n) - vals.size()) + " more item(s) not shown");
            return true;
        }
    };
}  // namespace pkzgui