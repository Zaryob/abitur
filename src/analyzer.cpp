// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/analyzer.hpp"
#include <algorithm>
#include <functional>
#include <llvm/ADT/StringExtras.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/BinaryFormat/MachO.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFDie.h>
#include <llvm/DebugInfo/DWARF/DWARFUnit.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/Object/Archive.h>
#include <llvm/Object/ELF.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/MachO.h>
#include <llvm/Object/MachOUniversal.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace abitur {
namespace {
using namespace llvm;
using namespace llvm::object;
using namespace llvm::dwarf;

template <class T> T checked(Expected<T> value) {
    if (!value)
        throw std::runtime_error(toString(value.takeError()));
    return std::move(*value);
}
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

struct Node {
    std::string kind;
    std::map<std::string, std::string> properties;
    std::vector<std::pair<std::string, size_t>> edges;
    std::set<std::string> unknown;
};
struct Graph {
    std::vector<Node> nodes{{"void", {}, {}, {}}};
    std::map<std::string, size_t> interfaces;
};
struct Corpus {
    bool archive = false;
    std::string target;
    std::set<std::string> exports;
    std::map<std::string, std::string> symbols;
    std::set<std::string> producers;
    std::vector<Graph> graphs;
    std::set<std::string> warnings;
    std::vector<ElfTypeLayout> type_layouts;
};

std::optional<uint64_t> number(DWARFDie die, Attribute attr) {
    if (auto value = die.findRecursively({attr}))
        return value->getAsUnsignedConstant();
    return {};
}
std::string name(DWARFDie die) {
    auto n = die.getName(DINameKind::ShortName);
    return n ? n : "";
}
std::string qualified(DWARFDie die) {
    auto result = name(die);
    for (auto parent = die.getParent(); parent; parent = parent.getParent()) {
        auto tag = parent.getTag();
        if (tag == DW_TAG_namespace || tag == DW_TAG_structure_type || tag == DW_TAG_class_type ||
            tag == DW_TAG_union_type) {
            auto scope = name(parent);
            if (!scope.empty())
                result = scope + "::" + result;
        }
    }
    return result;
}
DWARFDie reference(DWARFDie die, Attribute attr, unsigned depth = 0) {
    if (depth > 32)
        fail("cyclic DWARF specification/abstract origin");
    if (auto ref = die.getAttributeValueAsReferencedDie(attr))
        return ref;
    for (auto fallback : {DW_AT_specification, DW_AT_abstract_origin})
        if (auto ref = die.getAttributeValueAsReferencedDie(fallback))
            if (auto target = reference(ref, attr, depth + 1))
                return target;
    return {};
}
bool record(Tag tag) {
    return tag == DW_TAG_structure_type || tag == DW_TAG_class_type || tag == DW_TAG_union_type;
}

class GraphBuilder {
    Graph graph;
    using Key = std::pair<DWARFUnit*, uint64_t>;
    std::map<Key, size_t> ids;
    std::map<std::string, DWARFDie> definitions;
    std::set<std::string>& warnings;

    void property(Node& node, DWARFDie die, Attribute attr, const std::string& key,
                  bool required = false) {
        if (auto v = number(die, attr))
            node.properties[key] = std::to_string(*v);
        else if (die.find(attr) || required)
            node.unknown.insert("unresolved " + key);
    }
    size_t type(DWARFDie die, unsigned depth = 0) {
        if (!die)
            return 0;
        if (depth > 256)
            fail("DWARF type depth limit exceeded");
        auto tag = die.getTag();
        if (tag == DW_TAG_typedef || tag == DW_TAG_const_type || tag == DW_TAG_volatile_type ||
            tag == DW_TAG_restrict_type)
            return type(reference(die, DW_AT_type), depth + 1);
        if (record(tag) && number(die, DW_AT_declaration).value_or(0)) {
            auto it = definitions.find(qualified(die));
            if (it != definitions.end())
                die = it->second;
        }
        Key key{die.getDwarfUnit(), die.getOffset()};
        if (auto it = ids.find(key); it != ids.end())
            return it->second;
        auto id = graph.nodes.size();
        ids[key] = id;
        graph.nodes.emplace_back();
        Node node;
        tag = die.getTag();
        node.kind = TagString(tag).str();
        auto edge = [&](const std::string& label, DWARFDie target) {
            node.edges.emplace_back(label, type(target, depth + 1));
        };
        auto target = [&] { return reference(die, DW_AT_type); };
        if (record(tag)) {
            property(node, die, DW_AT_byte_size, "size", true);
            property(node, die, DW_AT_alignment, "alignment");
            property(node, die, DW_AT_calling_convention, "calling-convention");
            if (number(die, DW_AT_declaration).value_or(0))
                node.unknown.insert("incomplete record " + qualified(die));
            size_t member = 0;
            for (auto child : die.children()) {
                if (child.getTag() == DW_TAG_inheritance)
                    node.unknown.insert("C++ inheritance layout is not analyzed by this version");
                if (child.getTag() == DW_TAG_subprogram &&
                    number(child, DW_AT_virtuality).value_or(0))
                    node.unknown.insert(
                        "C++ virtual methods/vtable layout is not analyzed by this version");
                if (child.getTag() == DW_TAG_subprogram &&
                    (name(child) == name(die) || name(child).starts_with('~')))
                    node.unknown.insert(
                        "C++ special member functions may affect value passing and require review");
                if (child.getTag() != DW_TAG_member)
                    continue;
                if (number(child, DW_AT_declaration).value_or(0) ||
                    number(child, DW_AT_external).value_or(0))
                    continue;
                auto member_name = name(child);
                auto prefix = member_name.empty()
                                  ? "anonymous-member[" + std::to_string(member) + "]"
                                  : "member " + member_name;
                ++member;
                property(node, child, DW_AT_data_member_location, prefix + " offset",
                         tag != DW_TAG_union_type && !child.find(DW_AT_data_bit_offset));
                property(node, child, DW_AT_bit_size, prefix + " bit-size");
                property(node, child, DW_AT_bit_offset, prefix + " bit-offset");
                property(node, child, DW_AT_data_bit_offset, prefix + " data-bit-offset");
                if (!reference(child, DW_AT_type))
                    node.unknown.insert(prefix + " has no type");
                edge(prefix + " type", reference(child, DW_AT_type));
            }
            node.properties["member-count"] = std::to_string(member);
        } else if (tag == DW_TAG_subprogram || tag == DW_TAG_subroutine_type) {
            node.kind = "function";
            property(node, die, DW_AT_calling_convention, "calling-convention");
            edge("return", target());
            auto parameters = die;
            bool has_parameters = false;
            for (auto child : die.children())
                if (child.getTag() == DW_TAG_formal_parameter ||
                    child.getTag() == DW_TAG_unspecified_parameters)
                    has_parameters = true;
            if (!has_parameters) {
                if (auto spec = die.getAttributeValueAsReferencedDie(DW_AT_specification))
                    parameters = spec;
                else if (auto origin = die.getAttributeValueAsReferencedDie(DW_AT_abstract_origin))
                    parameters = origin;
            }
            size_t count = 0;
            for (auto child : parameters.children()) {
                if (child.getTag() == DW_TAG_unspecified_parameters)
                    node.properties["variadic"] = "true";
                if (child.getTag() == DW_TAG_formal_parameter) {
                    if (!reference(child, DW_AT_type))
                        node.unknown.insert("parameter has no type");
                    edge("parameter[" + std::to_string(count++) + "]",
                         reference(child, DW_AT_type));
                }
            }
            node.properties["parameter-count"] = std::to_string(count);
        } else if (tag == DW_TAG_pointer_type || tag == DW_TAG_reference_type ||
                   tag == DW_TAG_rvalue_reference_type) {
            node.properties["size"] = std::to_string(
                number(die, DW_AT_byte_size).value_or(die.getDwarfUnit()->getAddressByteSize()));
            edge("pointee", target());
        } else if (tag == DW_TAG_base_type) {
            property(node, die, DW_AT_byte_size, "size", true);
            property(node, die, DW_AT_encoding, "encoding", true);
        } else if (tag == DW_TAG_array_type) {
            if (!target())
                node.unknown.insert("array has no element type");
            edge("element", target());
            size_t dimension = 0;
            for (auto child : die.children())
                if (child.getTag() == DW_TAG_subrange_type) {
                    auto prefix = "dimension[" + std::to_string(dimension++) + "]";
                    if (auto count = number(child, DW_AT_count))
                        node.properties[prefix] = std::to_string(*count);
                    else if (auto upper = number(child, DW_AT_upper_bound)) {
                        auto lower = number(child, DW_AT_lower_bound).value_or(0);
                        if (*upper < lower)
                            node.unknown.insert("invalid array bounds");
                        else
                            node.properties[prefix] = std::to_string(*upper - lower + 1);
                    } else
                        node.unknown.insert("unknown array bound");
                }
            if (!dimension)
                node.unknown.insert("missing array dimensions");
        } else if (tag == DW_TAG_enumeration_type) {
            property(node, die, DW_AT_byte_size, "size", true);
            if (target())
                edge("underlying", target());
        } else if (tag == DW_TAG_variable) {
            node.kind = "variable";
            edge("type", target());
            if (!target())
                node.unknown.insert("variable has no type");
        } else if (tag == DW_TAG_unspecified_type && name(die) == "void") {
            node.kind = "void";
        } else {
            node.unknown.insert("unsupported type: " + node.kind + " " + qualified(die));
        }
        if (die.find(DW_AT_signature))
            node.unknown.insert("type signature indirection requires review");
        if (die.find(DW_AT_GNU_vector))
            node.unknown.insert("compiler vector extension requires review");
        graph.nodes[id] = std::move(node);
        return id;
    }
    void walk(DWARFDie die, const std::function<void(DWARFDie)>& visitor, unsigned depth = 0) {
        if (depth > 256)
            fail("DWARF scope depth limit exceeded");
        visitor(die);
        for (auto child : die.children())
            walk(child, visitor, depth + 1);
    }

  public:
    explicit GraphBuilder(std::set<std::string>& w) : warnings(w) {}
    Graph build(DWARFContext& context, const std::set<std::string>& exports,
                std::vector<ElfTypeLayout>& out_layouts) {
        std::vector<DWARFDie> roots;
        for (auto& unit : context.info_section_units())
            roots.push_back(unit->getUnitDIE(false));
        for (auto& unit : context.types_section_units())
            roots.push_back(unit->getUnitDIE(false));
        if (roots.empty())
            fail("embedded DWARF required; compile with -g");
        for (auto root : roots) {
            if (!root)
                fail("invalid DWARF unit");
            if (root.find(DW_AT_dwo_name) || root.find(DW_AT_GNU_dwo_name))
                fail("split DWARF is unsupported; use embedded debug info");
            walk(root, [&](DWARFDie die) {
                if (record(die.getTag()) && !number(die, DW_AT_declaration).value_or(0) &&
                    !name(die).empty())
                    definitions.emplace(qualified(die), die);
            });
        }

        for (const auto& [qname, die] : definitions) {
            ElfTypeLayout tl;
            tl.name = qname;
            auto tag = die.getTag();
            tl.kind = tag == DW_TAG_structure_type ? "struct"
                      : tag == DW_TAG_class_type   ? "class"
                                                   : "union";
            tl.byte_size = number(die, DW_AT_byte_size).value_or(0);
            tl.alignment = number(die, DW_AT_alignment).value_or(0);
            uint64_t current_offset = 0;
            for (auto child : die.children()) {
                if (child.getTag() != DW_TAG_member)
                    continue;
                if (number(child, DW_AT_declaration).value_or(0) ||
                    number(child, DW_AT_external).value_or(0))
                    continue;
                auto mname = name(child);
                if (mname.empty())
                    mname = "<anonymous>";
                auto off = number(child, DW_AT_data_member_location).value_or(0);
                auto type_die = reference(child, DW_AT_type);
                std::string tname = type_die ? name(type_die) : "void";
                if (tname.empty() && type_die)
                    tname = TagString(type_die.getTag()).str();
                auto sz = type_die ? number(type_die, DW_AT_byte_size).value_or(0) : 0;

                if (off > current_offset && tl.kind != "union") {
                    ElfMemberLayout pad;
                    pad.name = "[padding]";
                    pad.type = "";
                    pad.offset = current_offset;
                    pad.size = off - current_offset;
                    pad.is_padding = true;
                    tl.members.push_back(pad);
                }
                ElfMemberLayout mem;
                mem.name = mname;
                mem.type = tname;
                mem.offset = off;
                mem.size = sz;
                mem.is_padding = false;
                tl.members.push_back(mem);
                current_offset = off + (sz ? sz : 1);
            }
            if (tl.byte_size > current_offset && tl.kind != "union") {
                ElfMemberLayout pad;
                pad.name = "[tail padding]";
                pad.type = "";
                pad.offset = current_offset;
                pad.size = tl.byte_size - current_offset;
                pad.is_padding = true;
                tl.members.push_back(pad);
            }
            out_layouts.push_back(std::move(tl));
        }

        for (auto root : roots)
            walk(root, [&](DWARFDie die) {
                if (die.getTag() != DW_TAG_subprogram && die.getTag() != DW_TAG_variable)
                    return;
                const char* linkage = die.getLinkageName();
                auto symbol = linkage ? std::string(linkage) : name(die);
                if (!exports.contains(symbol)) {
                    if (exports.contains("_" + symbol))
                        symbol = "_" + symbol;
                    else
                        return;
                }
                if (graph.interfaces.contains(symbol) && number(die, DW_AT_declaration).value_or(0))
                    return;
                graph.interfaces[symbol] = type(die);
            });
        for (const auto& symbol : exports)
            if (!graph.interfaces.contains(symbol))
                warnings.insert("No DWARF interface for exported symbol: " + symbol);
        return std::move(graph);
    }
};

std::string target(const ELFObjectFileBase& object) {
    auto osabi = static_cast<unsigned char>(object.getMemoryBufferRef().getBuffer()[ELF::EI_OSABI]);
    return std::to_string(object.getEMachine()) + "/" +
           std::to_string(object.getBytesInAddress() * 8) +
           (object.isLittleEndian() ? "/little" : "/big") + "/osabi=" + std::to_string(osabi);
}

std::string target(const MachOObjectFile& object) {
    return "macho/" + std::to_string(object.getHeader().cputype) + "/" +
           std::to_string(object.getHeader().cpusubtype) + "/" +
           (object.is64Bit() ? "64" : "32") +
           (object.isLittleEndian() ? "/little" : "/big");
}

std::string machine_name(uint16_t machine) {
    switch (machine) {
    case ELF::EM_X86_64: return "x86_64 (AMD/Intel 64-bit)";
    case ELF::EM_AARCH64: return "AArch64 (ARM 64-bit)";
    case ELF::EM_ARM: return "ARM (32-bit)";
    case ELF::EM_386: return "i386 (Intel 32-bit)";
    case ELF::EM_RISCV: return "RISC-V";
    case ELF::EM_PPC64: return "PowerPC 64";
    case ELF::EM_PPC: return "PowerPC 32";
    case ELF::EM_MIPS: return "MIPS";
    case ELF::EM_LOONGARCH: return "LoongArch";
    case ELF::EM_S390: return "IBM System/390";
    default: return "Machine " + std::to_string(machine);
    }
}

std::string file_type_name(uint16_t type) {
    switch (type) {
    case ELF::ET_DYN: return "Shared library / PIE (ET_DYN)";
    case ELF::ET_EXEC: return "Executable (ET_EXEC)";
    case ELF::ET_REL: return "Relocatable object (ET_REL)";
    case ELF::ET_CORE: return "Core dump (ET_CORE)";
    default: return "ELF Type " + std::to_string(type);
    }
}

std::string osabi_name(uint8_t osabi) {
    switch (osabi) {
    case ELF::ELFOSABI_NONE: return "UNIX - System V";
    case ELF::ELFOSABI_HPUX: return "HP-UX";
    case ELF::ELFOSABI_NETBSD: return "NetBSD";
    case ELF::ELFOSABI_GNU: return "GNU/Linux";
    case ELF::ELFOSABI_SOLARIS: return "Solaris";
    case ELF::ELFOSABI_AIX: return "AIX";
    case ELF::ELFOSABI_FREEBSD: return "FreeBSD";
    case ELF::ELFOSABI_OPENBSD: return "OpenBSD";
    case ELF::ELFOSABI_ARM: return "ARM Architecture";
    default: return "OSABI=" + std::to_string(osabi);
    }
}

std::string segment_type_name(uint32_t type) {
    switch (type) {
    case ELF::PT_LOAD: return "LOAD";
    case ELF::PT_DYNAMIC: return "DYNAMIC";
    case ELF::PT_INTERP: return "INTERP";
    case ELF::PT_NOTE: return "NOTE";
    case ELF::PT_SHLIB: return "SHLIB";
    case ELF::PT_PHDR: return "PHDR";
    case ELF::PT_TLS: return "TLS";
    case ELF::PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
    case ELF::PT_GNU_STACK: return "GNU_STACK";
    case ELF::PT_GNU_RELRO: return "GNU_RELRO";
    case ELF::PT_GNU_PROPERTY: return "GNU_PROPERTY";
    default: return "0x" + utohexstr(type);
    }
}

std::string section_type_name(uint32_t type) {
    switch (type) {
    case ELF::SHT_PROGBITS: return "PROGBITS";
    case ELF::SHT_SYMTAB: return "SYMTAB";
    case ELF::SHT_STRTAB: return "STRTAB";
    case ELF::SHT_RELA: return "RELA";
    case ELF::SHT_HASH: return "HASH";
    case ELF::SHT_DYNAMIC: return "DYNAMIC";
    case ELF::SHT_NOTE: return "NOTE";
    case ELF::SHT_NOBITS: return "NOBITS";
    case ELF::SHT_REL: return "REL";
    case ELF::SHT_SHLIB: return "SHLIB";
    case ELF::SHT_DYNSYM: return "DYNSYM";
    case ELF::SHT_INIT_ARRAY: return "INIT_ARRAY";
    case ELF::SHT_FINI_ARRAY: return "FINI_ARRAY";
    case ELF::SHT_GNU_HASH: return "GNU_HASH";
    case ELF::SHT_GNU_verdef: return "VERDEF";
    case ELF::SHT_GNU_verneed: return "VERNEED";
    case ELF::SHT_GNU_versym: return "VERSYM";
    default: return "0x" + utohexstr(type);
    }
}

std::string section_flags_str(uint64_t flags) {
    std::string s;
    if (flags & ELF::SHF_WRITE) s += (s.empty() ? "" : " ") + std::string("WRITE");
    if (flags & ELF::SHF_ALLOC) s += (s.empty() ? "" : " ") + std::string("ALLOC");
    if (flags & ELF::SHF_EXECINSTR) s += (s.empty() ? "" : " ") + std::string("EXEC");
    if (flags & ELF::SHF_MERGE) s += (s.empty() ? "" : " ") + std::string("MERGE");
    if (flags & ELF::SHF_STRINGS) s += (s.empty() ? "" : " ") + std::string("STRINGS");
    if (flags & ELF::SHF_TLS) s += (s.empty() ? "" : " ") + std::string("TLS");
    return s.empty() ? "-" : s;
}

std::string symbol_type_name(uint8_t type) {
    switch (type) {
    case ELF::STT_NOTYPE: return "NOTYPE";
    case ELF::STT_OBJECT: return "OBJECT";
    case ELF::STT_FUNC: return "FUNC";
    case ELF::STT_SECTION: return "SECTION";
    case ELF::STT_FILE: return "FILE";
    case ELF::STT_COMMON: return "COMMON";
    case ELF::STT_TLS: return "TLS";
    case ELF::STT_GNU_IFUNC: return "GNU_IFUNC";
    default: return std::to_string(type);
    }
}

std::string visibility_name(uint8_t other) {
    switch (other & 3) {
    case ELF::STV_DEFAULT: return "DEFAULT";
    case ELF::STV_INTERNAL: return "INTERNAL";
    case ELF::STV_HIDDEN: return "HIDDEN";
    case ELF::STV_PROTECTED: return "PROTECTED";
    default: return std::to_string(other);
    }
}

std::string describe_node(const Graph& g, size_t id, unsigned depth = 0) {
    if (depth > 16 || id >= g.nodes.size())
        return "...";
    const auto& node = g.nodes[id];
    if (node.kind == "function") {
        std::string ret = "void";
        std::vector<std::string> params;
        for (const auto& [edge_name, child_id] : node.edges) {
            if (edge_name == "return") {
                ret = describe_node(g, child_id, depth + 1);
            } else if (edge_name.starts_with("parameter")) {
                params.push_back(describe_node(g, child_id, depth + 1));
            }
        }
        std::string res = ret + "(";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0)
                res += ", ";
            res += params[i];
        }
        if (node.properties.count("variadic")) {
            if (!params.empty())
                res += ", ";
            res += "...";
        }
        res += ")";
        return res;
    }
    if (node.kind == "void")
        return "void";
    if (node.kind == "DW_TAG_pointer_type") {
        for (const auto& [edge_name, child_id] : node.edges)
            if (edge_name == "pointee")
                return describe_node(g, child_id, depth + 1) + "*";
        return "void*";
    }
    if (node.kind == "DW_TAG_reference_type") {
        for (const auto& [edge_name, child_id] : node.edges)
            if (edge_name == "pointee")
                return describe_node(g, child_id, depth + 1) + "&";
        return "&";
    }
    if (node.kind == "DW_TAG_rvalue_reference_type") {
        for (const auto& [edge_name, child_id] : node.edges)
            if (edge_name == "pointee")
                return describe_node(g, child_id, depth + 1) + "&&";
        return "&&";
    }
    if (node.kind == "DW_TAG_array_type") {
        std::string elem = "void";
        for (const auto& [edge_name, child_id] : node.edges)
            if (edge_name == "element")
                elem = describe_node(g, child_id, depth + 1);
        std::string dims = "[]";
        auto it = node.properties.find("dimension[0]");
        if (it != node.properties.end())
            dims = "[" + it->second + "]";
        return elem + dims;
    }
    if (node.kind == "DW_TAG_base_type") {
        auto it = node.properties.find("size");
        return it != node.properties.end() ? "base(" + it->second + "B)" : "base";
    }
    if (node.kind == "DW_TAG_structure_type" || node.kind == "DW_TAG_class_type" ||
        node.kind == "DW_TAG_union_type") {
        auto it = node.properties.find("size");
        std::string kind = node.kind == "DW_TAG_structure_type" ? "struct"
                           : node.kind == "DW_TAG_class_type"   ? "class"
                                                                : "union";
        return kind + (it != node.properties.end() ? " (" + it->second + "B)" : "");
    }
    if (node.kind == "DW_TAG_enumeration_type")
        return "enum";
    return node.kind;
}

std::filesystem::path find_dsym_binary(const std::filesystem::path& binary_path) {
    if (binary_path.empty())
        return {};
    auto candidate1 = std::filesystem::path(binary_path.string() + ".dSYM") / "Contents" / "Resources" / "DWARF" / binary_path.filename();
    if (std::filesystem::exists(candidate1))
        return candidate1;

    auto candidate2 = binary_path.parent_path() / (binary_path.stem().string() + ".dSYM") / "Contents" / "Resources" / "DWARF" / binary_path.stem();
    if (std::filesystem::exists(candidate2))
        return candidate2;

    auto dwarf_dir = std::filesystem::path(binary_path.string() + ".dSYM") / "Contents" / "Resources" / "DWARF";
    std::error_code ec;
    if (std::filesystem::is_directory(dwarf_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dwarf_dir, ec)) {
            if (entry.is_regular_file())
                return entry.path();
        }
    }

    if (std::filesystem::exists(binary_path)) {
        std::string cmd = "dsymutil \"" + binary_path.string() + "\" 2>/dev/null";
        int res = std::system(cmd.c_str());
        if (res == 0) {
            if (std::filesystem::exists(candidate1))
                return candidate1;
            if (std::filesystem::exists(candidate2))
                return candidate2;
        }
    }
    return {};
}

void read_macho_object(const MachOObjectFile& macho, Corpus& corpus, bool allow_exec = false,
                       const std::filesystem::path& origin_path = {}) {
    auto filetype = macho.getHeader().filetype;
    if (!allow_exec && filetype != (corpus.archive ? MachO::MH_OBJECT : MachO::MH_DYLIB))
        fail(corpus.archive ? "Archive member must be relocatable Mach-O object (.o)"
                            : "Library must be Mach-O dynamic library (.dylib)");

    auto architecture = target(macho);
    if (corpus.target.empty())
        corpus.target = architecture;
    else if (corpus.target != architecture)
        fail("Mixed target architectures in archive");

    bool has_embedded_dwarf = false;
    for (const auto& sec : macho.sections()) {
        if (auto n = sec.getName()) {
            if (n->contains("__debug_info") || n->contains(".debug_info") ||
                n->contains("__debug_abbrev") || n->contains(".debug_abbrev")) {
                has_embedded_dwarf = true;
                break;
            }
        }
    }

    const ObjectFile* debug_obj = &macho;
    std::optional<OwningBinary<Binary>> dsym_holder;
    if (!has_embedded_dwarf) {
        auto dsym_file = find_dsym_binary(origin_path);
        if (!dsym_file.empty()) {
            dsym_holder.emplace(checked(createBinary(dsym_file.string())));
            if (auto* dsym_obj = dyn_cast<ObjectFile>(dsym_holder->getBinary())) {
                debug_obj = dsym_obj;
            }
        }
    }

    if (!has_embedded_dwarf && debug_obj == &macho)
        fail("Embedded DWARF or companion .dSYM required; compile with -g (or run dsymutil <binary>)");

    std::set<std::string> local_exports;
    for (const auto& symbol : macho.symbols()) {
        auto flags = checked(symbol.getFlags());
        if ((flags & SymbolRef::SF_Undefined) || !(flags & SymbolRef::SF_Global) ||
            (flags & SymbolRef::SF_Hidden))
            continue;
        auto n = checked(symbol.getName()).str();
        if (n.empty())
            continue;
        local_exports.insert(n);
        corpus.symbols[n] = "Mach-O global symbol (flags=0x" + utohexstr(flags) + ")";
    }

    Error trie_err = Error::success();
    for (const auto& exp : macho.exports(trie_err)) {
        auto n = exp.name().str();
        if (!n.empty()) {
            local_exports.insert(n);
            if (!corpus.symbols.contains(n))
                corpus.symbols[n] = "Mach-O export-trie entry";
        }
    }
    if (trie_err)
        consumeError(std::move(trie_err));

    std::vector<std::string> diagnostics;
    auto on_error = [&](Error error) { diagnostics.push_back(toString(std::move(error))); };
    auto context = DWARFContext::create(*debug_obj, DWARFContext::ProcessDebugRelocations::Process,
                                        nullptr, "", on_error, on_error);
    for (auto& unit : context->compile_units()) {
        if (auto value = unit->getUnitDIE().find(DW_AT_producer))
            corpus.producers.insert(checked(value->getAsCString()));
    }
    auto graph = GraphBuilder(corpus.warnings).build(*context, local_exports, corpus.type_layouts);
    if (!diagnostics.empty())
        fail("DWARF parsing failed: " + diagnostics.front());
    corpus.exports.insert(local_exports.begin(), local_exports.end());
    corpus.graphs.push_back(std::move(graph));
}

void read_object(const ObjectFile& object, Corpus& corpus, bool allow_exec = false,
                 const std::filesystem::path& origin_path = {}) {
    if (auto* elf = dyn_cast<ELFObjectFileBase>(&object)) {
        if (!allow_exec && elf->getEType() != (corpus.archive ? ELF::ET_REL : ELF::ET_DYN))
            fail(corpus.archive ? "Archive member must be relocatable ELF"
                                : "Library must be ELF ET_DYN (.so)");
        auto architecture = target(*elf);
        if (corpus.target.empty())
            corpus.target = architecture;
        else if (corpus.target != architecture)
            fail("Mixed target architectures in archive");
        bool debug = false;
        for (auto section : object.sections()) {
            auto n = checked(section.getName());
            if (n == ".debug_info" || n == ".zdebug_info")
                debug = section.getSize() > 0;
            if (n == ".gnu.version_d")
                corpus.warnings.insert("Symbol versions require libabigail/manual review; native "
                                       "matching uses symbol names");
        }
        if (!debug)
            fail("Embedded DWARF required; compile with -g (stripped/split debug is unsupported)");
        std::set<std::string> local_exports;
        auto symbols = corpus.archive ? elf->symbols() : elf->getDynamicSymbolIterators();
        for (auto symbol : symbols) {
            auto flags = checked(symbol.getFlags());
            auto visibility = symbol.getOther() & 3;
            if ((flags & SymbolRef::SF_Undefined) || !(flags & SymbolRef::SF_Global) ||
                visibility == ELF::STV_HIDDEN || visibility == ELF::STV_INTERNAL)
                continue;
            auto kind = symbol.getELFType();
            if (kind == ELF::STT_FILE || kind == ELF::STT_SECTION)
                continue;
            auto n = checked(symbol.getName()).str();
            if (n.empty())
                continue;
            local_exports.insert(n);
            corpus.symbols[n] =
                "ELF type=" + std::to_string(kind) + ", binding=" + std::to_string(symbol.getBinding());
            if (kind == ELF::STT_GNU_IFUNC)
                corpus.warnings.insert("GNU IFUNC resolver requires review: " + n);
            if (n.starts_with("_ZTV") || n.starts_with("_ZTT") || n.starts_with("_ZTh") ||
                n.starts_with("_ZTv"))
                corpus.warnings.insert("C++ vtables/thunks are outside the analyzed scope");
        }
        std::vector<std::string> diagnostics;
        auto on_error = [&](Error error) { diagnostics.push_back(toString(std::move(error))); };
        auto context = DWARFContext::create(object, DWARFContext::ProcessDebugRelocations::Process,
                                            nullptr, "", on_error, on_error);
        for (auto& unit : context->compile_units()) {
            if (auto value = unit->getUnitDIE().find(DW_AT_producer))
                corpus.producers.insert(checked(value->getAsCString()));
        }
        auto graph = GraphBuilder(corpus.warnings).build(*context, local_exports, corpus.type_layouts);
        if (!diagnostics.empty())
            fail("DWARF parsing failed: " + diagnostics.front());
        corpus.exports.insert(local_exports.begin(), local_exports.end());
        corpus.graphs.push_back(std::move(graph));
    } else if (auto* macho = dyn_cast<MachOObjectFile>(&object)) {
        read_macho_object(*macho, corpus, allow_exec, origin_path);
    } else {
        fail("Only ELF and Mach-O inputs are supported");
    }
}

Corpus load(const std::filesystem::path& path, bool allow_exec = false) {
    auto binary = checked(createBinary(path.string()));
    Corpus corpus;
    if (auto* archive = dyn_cast<Archive>(binary.getBinary())) {
        corpus.archive = true;
        if (archive->isThin())
            fail("Thin archives are unsupported; use a regular archive");
        Error error = Error::success();
        for (const auto& member : archive->children(error)) {
            auto child = checked(member.getAsBinary());
            auto* object = dyn_cast<ObjectFile>(child.get());
            if (!object)
                fail("Archive contains a non-object member");
            read_object(*object, corpus, allow_exec, path);
        }
        if (error)
            fail(toString(std::move(error)));
        if (corpus.graphs.empty())
            fail("Archive has no ELF or Mach-O members");
    } else if (auto* universal = dyn_cast<MachOUniversalBinary>(binary.getBinary())) {
        std::string preferred_arch;
#if defined(__arm64__) || defined(__aarch64__)
        preferred_arch = "arm64";
#elif defined(__x86_64__)
        preferred_arch = "x86_64";
#endif
        std::unique_ptr<MachOObjectFile> selected_macho;
        std::unique_ptr<Archive> selected_archive;
        if (!preferred_arch.empty()) {
            if (auto obj_for_arch = universal->getObjectForArch(preferred_arch)) {
                if (auto macho_exp = obj_for_arch->getAsObjectFile())
                    selected_macho = std::move(*macho_exp);
                else if (auto arch_exp = obj_for_arch->getAsArchive())
                    selected_archive = std::move(*arch_exp);
            } else {
                consumeError(obj_for_arch.takeError());
            }
        }
        if (!selected_macho && !selected_archive) {
            for (const auto& slice : universal->objects()) {
                if (auto macho_exp = slice.getAsObjectFile()) {
                    selected_macho = std::move(*macho_exp);
                    break;
                } else if (auto arch_exp = slice.getAsArchive()) {
                    selected_archive = std::move(*arch_exp);
                    break;
                }
            }
        }
        if (selected_macho) {
            read_object(*selected_macho, corpus, allow_exec, path);
        } else if (selected_archive) {
            corpus.archive = true;
            Error error = Error::success();
            for (const auto& member : selected_archive->children(error)) {
                auto child = checked(member.getAsBinary());
                auto* object = dyn_cast<ObjectFile>(child.get());
                if (!object)
                    fail("Universal archive contains a non-object member");
                read_object(*object, corpus, allow_exec, path);
            }
            if (error)
                fail(toString(std::move(error)));
            if (corpus.graphs.empty())
                fail("Universal archive has no Mach-O members");
        } else {
            fail("Universal binary contains no valid Mach-O slices");
        }
    } else {
        auto* object = dyn_cast<ObjectFile>(binary.getBinary());
        if (!object)
            fail("Expected ELF or Mach-O shared library, or regular archive");
        read_object(*object, corpus, allow_exec, path);
    }
    return corpus;
}

struct Interface {
    const Graph* graph;
    size_t id;
};
std::map<std::string, Interface> interfaces(const Corpus& corpus) {
    std::map<std::string, Interface> result;
    for (const auto& graph : corpus.graphs)
        for (const auto& [symbol, id] : graph.interfaces)
            result.emplace(symbol, Interface{&graph, id});
    return result;
}

void unknowns(const Graph& graph, size_t id, std::set<size_t>& seen, std::set<std::string>& out) {
    if (!seen.insert(id).second)
        return;
    const auto& node = graph.nodes.at(id);
    out.insert(node.unknown.begin(), node.unknown.end());
    for (const auto& [_, child] : node.edges)
        unknowns(graph, child, seen, out);
}

void difference(const Graph& old, size_t a, const Graph& now, size_t b, const std::string& path,
                std::set<std::pair<size_t, size_t>>& seen, std::vector<std::string>& changes) {
    if (!seen.emplace(a, b).second)
        return;
    const auto& left = old.nodes.at(a);
    const auto& right = now.nodes.at(b);
    if (left.kind != right.kind) {
        changes.push_back(path + ": " + left.kind + " -> " + right.kind);
        return;
    }
    std::set<std::string> keys;
    for (const auto& [key, _] : left.properties)
        keys.insert(key);
    for (const auto& [key, _] : right.properties)
        keys.insert(key);
    for (const auto& key : keys) {
        auto l = left.properties.find(key), r = right.properties.find(key);
        auto lv = l == left.properties.end() ? "absent" : l->second;
        auto rv = r == right.properties.end() ? "absent" : r->second;
        if (lv != rv)
            changes.push_back(path + "." + key + ": " + lv + " -> " + rv);
    }
    std::map<std::string, size_t> old_edges(left.edges.begin(), left.edges.end());
    std::map<std::string, size_t> new_edges(right.edges.begin(), right.edges.end());
    for (const auto& [label, child] : old_edges) {
        auto it = new_edges.find(label);
        if (it == new_edges.end())
            changes.push_back(path + "." + label + ": removed");
        else
            difference(old, child, now, it->second, path + "." + label, seen, changes);
    }
    for (const auto& [label, _] : new_edges)
        if (!old_edges.contains(label))
            changes.push_back(path + "." + label + ": added");
}

Consumer consumer(const std::filesystem::path& path, const Corpus& library) {
    auto binary = checked(createBinary(path.string()));
    Consumer result{path.string(), {}, {}};
    std::set<std::string> imports;
    if (auto* elf = dyn_cast<ELFObjectFileBase>(binary.getBinary())) {
        if (elf->getEType() != ELF::ET_EXEC && elf->getEType() != ELF::ET_DYN)
            fail("Consumer must be an ELF executable or shared library");
        if (target(*elf) != library.target)
            fail("Consumer target differs from library");
        for (auto symbol : elf->getDynamicSymbolIterators())
            if (checked(symbol.getFlags()) & SymbolRef::SF_Undefined) {
                auto n = checked(symbol.getName()).str();
                if (library.exports.contains(n))
                    imports.insert(n);
            }
    } else if (auto* macho = dyn_cast<MachOObjectFile>(binary.getBinary())) {
        if (target(*macho) != library.target)
            fail("Consumer target differs from library");
        for (const auto& symbol : macho->symbols()) {
            if (checked(symbol.getFlags()) & SymbolRef::SF_Undefined) {
                auto n = checked(symbol.getName()).str();
                if (library.exports.contains(n))
                    imports.insert(n);
                else if (n.starts_with('_') && library.exports.contains(n.substr(1)))
                    imports.insert(n);
            }
        }
    } else {
        fail("Consumer must be an ELF or Mach-O executable or shared library");
    }
    result.imports.assign(imports.begin(), imports.end());
    return result;
}

template <class ELFT>
void inspect_elf_template(const ELFObjectFile<ELFT>& elf_obj, ElfAnalysis& analysis) {
    const auto& ef = elf_obj.getELFFile();
    bool has_nx_stack = false;
    bool has_relro = false;
    auto phdrs = ef.program_headers();
    if (phdrs) {
        for (const auto& ph : *phdrs) {
            ElfSegment seg;
            seg.type = segment_type_name(ph.p_type);
            seg.vaddr = ph.p_vaddr;
            seg.paddr = ph.p_paddr;
            seg.file_size = ph.p_filesz;
            seg.mem_size = ph.p_memsz;
            seg.align = ph.p_align;
            std::string flags;
            if (ph.p_flags & ELF::PF_R) flags += 'R';
            if (ph.p_flags & ELF::PF_W) flags += 'W';
            if (ph.p_flags & ELF::PF_X) flags += 'X';
            seg.flags = flags.empty() ? "-" : flags;
            analysis.segments.push_back(seg);

            if (ph.p_type == ELF::PT_GNU_STACK) {
                if (!(ph.p_flags & ELF::PF_X))
                    has_nx_stack = true;
            }
            if (ph.p_type == ELF::PT_GNU_RELRO)
                has_relro = true;
        }
    }
    if (has_nx_stack)
        analysis.security_features.push_back("NX stack (non-executable)");
    else
        analysis.security_features.push_back("Executable stack (NX disabled - security alert)");
    if (has_relro)
        analysis.security_features.push_back("RELRO enabled");
    if (ef.getHeader().e_type == ELF::ET_DYN)
        analysis.security_features.push_back("PIE / Position Independent");

    StringRef dynstr;
    for (const auto& sec : elf_obj.sections()) {
        if (auto n = sec.getName()) {
            if (*n == ".dynstr") {
                if (auto c = sec.getContents())
                    dynstr = *c;
            }
        }
    }
    auto get_dyn_string = [&](uint64_t offset) -> std::string {
        if (offset < dynstr.size())
            return std::string(dynstr.data() + offset);
        return "";
    };
    auto dyn_entries = ef.dynamicEntries();
    if (dyn_entries) {
        for (const auto& dyn : *dyn_entries) {
            if (dyn.d_tag == ELF::DT_SONAME) {
                analysis.soname = get_dyn_string(dyn.d_un.d_val);
            } else if (dyn.d_tag == ELF::DT_NEEDED) {
                auto name = get_dyn_string(dyn.d_un.d_val);
                if (!name.empty())
                    analysis.needed_libraries.push_back(name);
            } else if (dyn.d_tag == ELF::DT_RPATH) {
                analysis.rpath = get_dyn_string(dyn.d_un.d_val);
            } else if (dyn.d_tag == ELF::DT_RUNPATH) {
                analysis.runpath = get_dyn_string(dyn.d_un.d_val);
            } else if (dyn.d_tag == ELF::DT_FLAGS_1) {
                if (dyn.d_un.d_val & ELF::DF_1_NOW)
                    analysis.security_features.push_back("BIND_NOW (Full RELRO)");
                if (dyn.d_un.d_val & ELF::DF_1_PIE)
                    analysis.security_features.push_back("DF_1_PIE");
            }
        }
    }
}

void inspect_segments_and_dynamic(const ELFObjectFileBase& elf, ElfAnalysis& analysis) {
    if (const auto* e64le = dyn_cast<ELF64LEObjectFile>(&elf))
        inspect_elf_template(*e64le, analysis);
    else if (const auto* e32le = dyn_cast<ELF32LEObjectFile>(&elf))
        inspect_elf_template(*e32le, analysis);
    else if (const auto* e64be = dyn_cast<ELF64BEObjectFile>(&elf))
        inspect_elf_template(*e64be, analysis);
    else if (const auto* e32be = dyn_cast<ELF32BEObjectFile>(&elf))
        inspect_elf_template(*e32be, analysis);
}

void inspect_macho(const MachOObjectFile& macho, ElfAnalysis& analysis, const Corpus& corpus) {
    switch (macho.getHeader().filetype) {
    case MachO::MH_DYLIB: analysis.file_type = "Mach-O Dynamic Library (.dylib)"; break;
    case MachO::MH_EXECUTE: analysis.file_type = "Mach-O Executable (MH_EXECUTE)"; break;
    case MachO::MH_OBJECT: analysis.file_type = "Mach-O Relocatable Object (.o)"; break;
    case MachO::MH_BUNDLE: analysis.file_type = "Mach-O Bundle"; break;
    case MachO::MH_DYLINKER: analysis.file_type = "Mach-O Dynamic Linker"; break;
    case MachO::MH_DSYM: analysis.file_type = "Mach-O Companion Debug Symbols (.dSYM)"; break;
    default: analysis.file_type = "Mach-O Type " + std::to_string(macho.getHeader().filetype); break;
    }

    switch (macho.getHeader().cputype) {
    case MachO::CPU_TYPE_ARM64: analysis.machine = "ARM64 (Apple Silicon / 64-bit)"; break;
    case MachO::CPU_TYPE_X86_64: analysis.machine = "x86_64 (Intel 64-bit)"; break;
    case MachO::CPU_TYPE_ARM: analysis.machine = "ARM (32-bit)"; break;
    case MachO::CPU_TYPE_I386: analysis.machine = "i386 (Intel 32-bit)"; break;
    case MachO::CPU_TYPE_POWERPC64: analysis.machine = "PowerPC 64"; break;
    case MachO::CPU_TYPE_POWERPC: analysis.machine = "PowerPC 32"; break;
    default: analysis.machine = "Mach-O CPU " + std::to_string(macho.getHeader().cputype); break;
    }

    analysis.elf_class = macho.is64Bit() ? "Mach-O 64-bit" : "Mach-O 32-bit";
    analysis.endianness = macho.isLittleEndian() ? "Little-endian (LSB)" : "Big-endian (MSB)";
    analysis.osabi = "macOS / Darwin (Mach-O)";

    bool has_code_signature = false;
    for (const auto& L : macho.load_commands()) {
        if (L.C.cmd == MachO::LC_SEGMENT_64) {
            auto seg64 = macho.getSegment64LoadCommand(L);
            ElfSegment seg;
            char segname[17] = {0};
            std::memcpy(segname, seg64.segname, 16);
            seg.type = segname;
            seg.vaddr = seg64.vmaddr;
            seg.paddr = seg64.fileoff;
            seg.file_size = seg64.filesize;
            seg.mem_size = seg64.vmsize;
            seg.align = 0;
            std::string flags;
            if (seg64.initprot & MachO::VM_PROT_READ) flags += 'R';
            if (seg64.initprot & MachO::VM_PROT_WRITE) flags += 'W';
            if (seg64.initprot & MachO::VM_PROT_EXECUTE) flags += 'X';
            seg.flags = flags.empty() ? "-" : flags;
            analysis.segments.push_back(seg);
        } else if (L.C.cmd == MachO::LC_SEGMENT) {
            auto seg32 = macho.getSegmentLoadCommand(L);
            ElfSegment seg;
            char segname[17] = {0};
            std::memcpy(segname, seg32.segname, 16);
            seg.type = segname;
            seg.vaddr = seg32.vmaddr;
            seg.paddr = seg32.fileoff;
            seg.file_size = seg32.filesize;
            seg.mem_size = seg32.vmsize;
            seg.align = 0;
            std::string flags;
            if (seg32.initprot & MachO::VM_PROT_READ) flags += 'R';
            if (seg32.initprot & MachO::VM_PROT_WRITE) flags += 'W';
            if (seg32.initprot & MachO::VM_PROT_EXECUTE) flags += 'X';
            seg.flags = flags.empty() ? "-" : flags;
            analysis.segments.push_back(seg);
        } else if (L.C.cmd == MachO::LC_ID_DYLIB) {
            auto dylib_cmd = macho.getDylibIDLoadCommand(L);
            if (dylib_cmd.dylib.name < L.C.cmdsize) {
                analysis.soname = L.Ptr + dylib_cmd.dylib.name;
            }
        } else if (L.C.cmd == MachO::LC_LOAD_DYLIB || L.C.cmd == MachO::LC_LOAD_WEAK_DYLIB ||
                   L.C.cmd == MachO::LC_REEXPORT_DYLIB) {
            auto dylib_cmd = macho.getDylibIDLoadCommand(L);
            if (dylib_cmd.dylib.name < L.C.cmdsize) {
                analysis.needed_libraries.push_back(L.Ptr + dylib_cmd.dylib.name);
            }
        } else if (L.C.cmd == MachO::LC_RPATH) {
            auto rpath_cmd = macho.getRpathCommand(L);
            if (rpath_cmd.path < L.C.cmdsize) {
                if (!analysis.rpath.empty()) analysis.rpath += ":";
                analysis.rpath += (L.Ptr + rpath_cmd.path);
            }
        } else if (L.C.cmd == MachO::LC_MAIN) {
            auto main_cmd = macho.getEntryPointCommand(L);
            analysis.entry_point = main_cmd.entryoff;
        } else if (L.C.cmd == MachO::LC_CODE_SIGNATURE) {
            has_code_signature = true;
        }
    }

    if (macho.getHeader().flags & MachO::MH_PIE)
        analysis.security_features.push_back("PIE / Position Independent Executable");
    if (macho.getHeader().flags & MachO::MH_TWOLEVEL)
        analysis.security_features.push_back("Two-level namespace (MH_TWOLEVEL)");
    if (macho.getHeader().flags & MachO::MH_BINDATLOAD)
        analysis.security_features.push_back("BINDATLOAD (Immediate binding)");
    if (has_code_signature)
        analysis.security_features.push_back("Code signature present (LC_CODE_SIGNATURE)");
    if (macho.getHeader().flags & MachO::MH_ALLOW_STACK_EXECUTION)
        analysis.security_features.push_back("Executable stack allowed (MH_ALLOW_STACK_EXECUTION - security alert)");
    else
        analysis.security_features.push_back("NX stack (non-executable by Darwin kernel policy)");

    for (const auto& sec : macho.sections()) {
        ElfSection s;
        s.name = checked(sec.getName()).str();
        s.address = sec.getAddress();
        s.size = sec.getSize();
        s.alignment = sec.getAlignment().value();
        if (macho.is64Bit()) {
            auto sec64 = macho.getSection64(sec.getRawDataRefImpl());
            s.offset = sec64.offset;
            uint32_t flags = sec64.flags;
            uint32_t type = flags & MachO::SECTION_TYPE;
            switch (type) {
            case MachO::S_REGULAR: s.type = "REGULAR"; break;
            case MachO::S_ZEROFILL: s.type = "ZEROFILL"; break;
            case MachO::S_CSTRING_LITERALS: s.type = "CSTRING_LITERALS"; break;
            case MachO::S_4BYTE_LITERALS: s.type = "4BYTE_LITERALS"; break;
            case MachO::S_8BYTE_LITERALS: s.type = "8BYTE_LITERALS"; break;
            case MachO::S_LITERAL_POINTERS: s.type = "LITERAL_POINTERS"; break;
            case MachO::S_NON_LAZY_SYMBOL_POINTERS: s.type = "NON_LAZY_PTRS"; break;
            case MachO::S_LAZY_SYMBOL_POINTERS: s.type = "LAZY_PTRS"; break;
            case MachO::S_SYMBOL_STUBS: s.type = "SYMBOL_STUBS"; break;
            case MachO::S_MOD_INIT_FUNC_POINTERS: s.type = "MOD_INIT_FUNCS"; break;
            case MachO::S_MOD_TERM_FUNC_POINTERS: s.type = "MOD_TERM_FUNCS"; break;
            case MachO::S_COALESCED: s.type = "COALESCED"; break;
            case MachO::S_THREAD_LOCAL_REGULAR: s.type = "THREAD_LOCAL"; break;
            case MachO::S_THREAD_LOCAL_ZEROFILL: s.type = "THREAD_LOCAL_ZEROFILL"; break;
            default: s.type = "0x" + utohexstr(type); break;
            }
            std::string f;
            if (flags & MachO::S_ATTR_PURE_INSTRUCTIONS) f += (f.empty() ? "" : " ") + std::string("PURE_INSTR");
            if (flags & MachO::S_ATTR_NO_TOC) f += (f.empty() ? "" : " ") + std::string("NO_TOC");
            if (flags & MachO::S_ATTR_STRIP_STATIC_SYMS) f += (f.empty() ? "" : " ") + std::string("STRIP_STATIC");
            if (flags & MachO::S_ATTR_NO_DEAD_STRIP) f += (f.empty() ? "" : " ") + std::string("NO_DEAD_STRIP");
            if (flags & MachO::S_ATTR_LIVE_SUPPORT) f += (f.empty() ? "" : " ") + std::string("LIVE_SUPPORT");
            if (flags & MachO::S_ATTR_SELF_MODIFYING_CODE) f += (f.empty() ? "" : " ") + std::string("SELF_MODIFYING");
            if (flags & MachO::S_ATTR_DEBUG) f += (f.empty() ? "" : " ") + std::string("DEBUG");
            s.flags = f.empty() ? "-" : f;
        } else {
            auto sec32 = macho.getSection(sec.getRawDataRefImpl());
            s.offset = sec32.offset;
            uint32_t type = sec32.flags & MachO::SECTION_TYPE;
            s.type = "0x" + utohexstr(type);
            s.flags = "-";
        }
        analysis.sections.push_back(s);
    }

    auto ifaces = interfaces(corpus);
    for (const auto& symbol : macho.symbols()) {
        auto flags = checked(symbol.getFlags());
        auto n = checked(symbol.getName()).str();
        if (n.empty())
            continue;
        if (flags & SymbolRef::SF_Undefined) {
            analysis.imports.push_back(n);
        } else if (flags & SymbolRef::SF_Global) {
            ElfSymbolInfo sym_info;
            sym_info.name = n;
            sym_info.demangled = llvm::demangle(n);
            sym_info.type = (flags & SymbolRef::SF_Executable) ? "FUNC" : "OBJECT";
            sym_info.binding = (flags & SymbolRef::SF_Weak) ? "WEAK" : "GLOBAL";
            sym_info.visibility = (flags & SymbolRef::SF_Hidden) ? "HIDDEN" : "DEFAULT";
            sym_info.size = 0;
            if (auto addr = symbol.getAddress())
                sym_info.address = *addr;
            auto it = ifaces.find(n);
            if (it != ifaces.end())
                sym_info.signature = describe_node(*it->second.graph, it->second.id);
            else if (n.starts_with('_')) {
                auto it2 = ifaces.find(n.substr(1));
                if (it2 != ifaces.end())
                    sym_info.signature = describe_node(*it2->second.graph, it2->second.id);
            }
            analysis.exports.push_back(sym_info);
        }
    }

    Error trie_err = Error::success();
    for (const auto& exp : macho.exports(trie_err)) {
        auto n = exp.name().str();
        bool exists = false;
        for (const auto& ex : analysis.exports) {
            if (ex.name == n) {
                exists = true;
                break;
            }
        }
        if (!exists && !n.empty()) {
            ElfSymbolInfo sym_info;
            sym_info.name = n;
            sym_info.demangled = llvm::demangle(n);
            sym_info.type = "FUNC";
            sym_info.binding = "GLOBAL";
            sym_info.visibility = "DEFAULT";
            sym_info.address = exp.address();
            auto it = ifaces.find(n);
            if (it != ifaces.end())
                sym_info.signature = describe_node(*it->second.graph, it->second.id);
            else if (n.starts_with('_')) {
                auto it2 = ifaces.find(n.substr(1));
                if (it2 != ifaces.end())
                    sym_info.signature = describe_node(*it2->second.graph, it2->second.id);
            }
            analysis.exports.push_back(sym_info);
        }
    }
    if (trie_err)
        consumeError(std::move(trie_err));
}

ElfAnalysis inspect_binary(const std::filesystem::path& path, const Corpus& corpus) {
    ElfAnalysis analysis;
    analysis.path = path.string();
    auto binary = checked(createBinary(path.string()));
    if (dyn_cast<Archive>(binary.getBinary())) {
        analysis.file_type = corpus.target.starts_with("macho") ? "Mach-O Static Archive (.a)" : "Archive (.a)";
        analysis.machine = corpus.target;
        analysis.type_layouts = corpus.type_layouts;
        for (const auto& exp : corpus.exports) {
            ElfSymbolInfo si;
            si.name = exp;
            si.demangled = llvm::demangle(exp);
            si.type = "FUNC";
            si.binding = "GLOBAL";
            analysis.exports.push_back(si);
        }
        std::sort(analysis.exports.begin(), analysis.exports.end(),
                  [](const auto& a, const auto& b) { return a.name < b.name; });
        return analysis;
    }
    if (auto* universal = dyn_cast<MachOUniversalBinary>(binary.getBinary())) {
        std::string preferred_arch;
#if defined(__arm64__) || defined(__aarch64__)
        preferred_arch = "arm64";
#elif defined(__x86_64__)
        preferred_arch = "x86_64";
#endif
        std::unique_ptr<MachOObjectFile> selected_macho;
        if (!preferred_arch.empty()) {
            if (auto obj_for_arch = universal->getObjectForArch(preferred_arch)) {
                if (auto macho_exp = obj_for_arch->getAsObjectFile())
                    selected_macho = std::move(*macho_exp);
            } else {
                consumeError(obj_for_arch.takeError());
            }
        }
        if (!selected_macho) {
            for (const auto& slice : universal->objects()) {
                if (auto macho_exp = slice.getAsObjectFile()) {
                    selected_macho = std::move(*macho_exp);
                    break;
                }
            }
        }
        if (selected_macho) {
            inspect_macho(*selected_macho, analysis, corpus);
        } else {
            fail("Universal binary contains no valid Mach-O slices");
        }
    } else if (auto* macho = dyn_cast<MachOObjectFile>(binary.getBinary())) {
        inspect_macho(*macho, analysis, corpus);
    } else if (auto* elf = dyn_cast<ELFObjectFileBase>(binary.getBinary())) {
        analysis.file_type = file_type_name(elf->getEType());
        analysis.machine = machine_name(elf->getEMachine());
        analysis.elf_class = elf->getBytesInAddress() == 8 ? "ELF64 (64-bit)" : "ELF32 (32-bit)";
        analysis.endianness = elf->isLittleEndian() ? "Little-endian (LSB)" : "Big-endian (MSB)";
        auto osabi = static_cast<unsigned char>(elf->getMemoryBufferRef().getBuffer()[ELF::EI_OSABI]);
        analysis.osabi = osabi_name(osabi);
        analysis.abi_version = elf->getEIdentABIVersion();

        if (const auto* e64le = dyn_cast<ELF64LEObjectFile>(elf))
            analysis.entry_point = e64le->getELFFile().getHeader().e_entry;
        else if (const auto* e32le = dyn_cast<ELF32LEObjectFile>(elf))
            analysis.entry_point = e32le->getELFFile().getHeader().e_entry;
        else if (const auto* e64be = dyn_cast<ELF64BEObjectFile>(elf))
            analysis.entry_point = e64be->getELFFile().getHeader().e_entry;
        else if (const auto* e32be = dyn_cast<ELF32BEObjectFile>(elf))
            analysis.entry_point = e32be->getELFFile().getHeader().e_entry;

        inspect_segments_and_dynamic(*elf, analysis);

        for (const auto& sec : elf->sections()) {
            ELFSectionRef elf_sec(sec);
            ElfSection s;
            s.name = checked(sec.getName()).str();
            s.address = sec.getAddress();
            s.size = sec.getSize();
            s.alignment = sec.getAlignment().value();
            s.type = section_type_name(elf_sec.getType());
            s.flags = section_flags_str(elf_sec.getFlags());
            s.offset = elf_sec.getOffset();
            analysis.sections.push_back(s);
        }

        auto ifaces = interfaces(corpus);
        auto syms = corpus.archive ? elf->symbols() : elf->getDynamicSymbolIterators();
        for (const auto& symbol : syms) {
            auto flags = checked(symbol.getFlags());
            auto n = checked(symbol.getName()).str();
            if (n.empty())
                continue;
            if (flags & SymbolRef::SF_Undefined) {
                analysis.imports.push_back(n);
            } else {
                auto kind = symbol.getELFType();
                if (kind == ELF::STT_FILE || kind == ELF::STT_SECTION)
                    continue;
                ElfSymbolInfo sym_info;
                sym_info.name = n;
                sym_info.demangled = llvm::demangle(n);
                sym_info.type = symbol_type_name(kind);
                sym_info.binding = (flags & SymbolRef::SF_Weak)
                                       ? "WEAK"
                                       : ((flags & SymbolRef::SF_Global) ? "GLOBAL" : "LOCAL");
                sym_info.visibility = visibility_name(symbol.getOther() & 3);
                sym_info.size = symbol.getSize();
                if (auto addr = symbol.getAddress())
                    sym_info.address = *addr;
                auto it = ifaces.find(n);
                if (it != ifaces.end())
                    sym_info.signature = describe_node(*it->second.graph, it->second.id);
                analysis.exports.push_back(sym_info);
            }
        }
    } else {
        fail("Expected ELF or Mach-O file or archive");
    }

    std::sort(analysis.exports.begin(), analysis.exports.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    std::sort(analysis.imports.begin(), analysis.imports.end());
    analysis.imports.erase(std::unique(analysis.imports.begin(), analysis.imports.end()),
                           analysis.imports.end());

    analysis.type_layouts = corpus.type_layouts;
    std::sort(analysis.type_layouts.begin(), analysis.type_layouts.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });

    return analysis;
}

} // namespace

Report analyze(const Options& options) {
    auto target_corpus = load(options.old_path, /*allow_exec=*/true);
    std::string scope_str = target_corpus.archive ? "static-archive-analysis"
                          : (target_corpus.target.starts_with("macho") ? "macho-binary-analysis" : "elf-binary-analysis");
    Report result{options.old_path.string(),
                  "",
                  scope_str,
                  "pass",
                  "Binary and interface analysis completed",
                  {},
                  {},
                  {"A static binary analysis reflects embedded headers, symbols and debug information.",
                   "Dynamic symbols and direct imports reflect binary metadata; runtime resolution "
                   "and plugins require execution or separate tracing."},
                  std::nullopt};

    auto elf_data = inspect_binary(options.old_path, target_corpus);

    for (const auto& w : target_corpus.warnings)
        result.findings.push_back({"ELF002", "warning", "", w, {}, {}});

    for (const auto& feat : elf_data.security_features) {
        if (feat.find("security alert") != std::string::npos) {
            result.findings.push_back(
                {"SEC001", "warning", "", "Security Alert: " + feat, {feat}, {}});
        }
    }

    for (const auto& path : options.consumers) {
        auto app = consumer(path, target_corpus);
        std::set<std::string> missing;
        auto binary = checked(createBinary(path.string()));
        if (auto* elf = dyn_cast<ELFObjectFileBase>(binary.getBinary())) {
            for (auto symbol : elf->getDynamicSymbolIterators()) {
                if (checked(symbol.getFlags()) & SymbolRef::SF_Undefined) {
                    auto n = checked(symbol.getName()).str();
                    if (!target_corpus.exports.contains(n))
                        missing.insert(n);
                }
            }
        } else if (auto* macho = dyn_cast<MachOObjectFile>(binary.getBinary())) {
            for (const auto& symbol : macho->symbols()) {
                if (checked(symbol.getFlags()) & SymbolRef::SF_Undefined) {
                    auto n = checked(symbol.getName()).str();
                    if (!target_corpus.exports.contains(n) &&
                        !(n.starts_with('_') && target_corpus.exports.contains(n.substr(1)))) {
                        missing.insert(n);
                    }
                }
            }
        }
        if (!missing.empty()) {
            for (const auto& m : missing) {
                result.findings.push_back(
                    {"CON001",
                     "error",
                     m,
                     "Consumer requires symbol not exported by target library",
                     {"Consumer: " + path.string()},
                     {path.string()}});
            }
        }
        result.consumers.push_back(std::move(app));
    }

    if (std::any_of(result.findings.begin(), result.findings.end(),
                    [](auto& f) { return f.severity == "error"; })) {
        result.decision = "break";
        result.reason = "Consumer import requirements are unsatisfied by this binary.";
    } else if (std::any_of(result.findings.begin(), result.findings.end(),
                           [](auto& f) { return f.severity == "warning"; })) {
        result.decision = "review";
        result.reason = "Analysis contains warnings or incomplete coverage requiring review.";
    }

    result.elf_analysis = std::move(elf_data);
    return result;
}

Report compare(const Options& options) {
    if (options.new_path.empty() || options.analyze_mode)
        return analyze(options);

    auto old = load(options.old_path);
    auto now = load(options.new_path);
    if (old.archive != now.archive)
        fail("Compare two shared libraries or two archives; mixed input kinds are unsupported");
    if (old.archive && !options.consumers.empty())
        fail("Consumer dynamic-import analysis applies to shared libraries, not static archives");
    Report result{options.old_path.string(),
                  options.new_path.string(),
                  old.archive ? "static-relink" : "shared-library",
                  "pass",
                  "No differences found in the supported ABI surface",
                  {},
                  {},
                  {"A structural comparison cannot establish runtime compatibility.",
                   "Consumer evidence uses direct dynamic imports; dlsym, plugins, transitive "
                   "dependencies, inlined code and copy relocations are not resolved.",
                   "C++ inheritance/vtables, symbol versions and compiler-specific ABI extensions "
                   "are outside the analyzed scope and require separate review.",
                   "SONAME, dependency resolution, loader search paths, exception/RTTI "
                   "interoperability and runtime behavior are not verified.",
                   "Use matching compiler/target settings and embedded, complete DWARF; "
                   "optimized/LTO debug information can omit ABI details."},
                  std::nullopt};
    if (old.target != now.target)
        result.findings.push_back({"ABI001",
                                   "error",
                                   "",
                                   "Target architecture/ELF ABI changed",
                                   {old.target + " -> " + now.target},
                                   {}});
    for (const auto& symbol : old.exports) {
        if (!now.exports.contains(symbol))
            result.findings.push_back({"ABI002",
                                       "error",
                                       symbol,
                                       "Exported symbol removed",
                                       {"Present in old library; absent from new library"},
                                       {}});
        else if (old.symbols.at(symbol) != now.symbols.at(symbol))
            result.findings.push_back({"ABI003",
                                       "warning",
                                       symbol,
                                       "ELF symbol kind or binding changed",
                                       {old.symbols.at(symbol) + " -> " + now.symbols.at(symbol)},
                                       {}});
    }
    for (const auto& symbol : now.exports)
        if (!old.exports.contains(symbol))
            result.findings.push_back({"ABI004", "note", symbol, "Exported symbol added", {}, {}});
    auto left = interfaces(old), right = interfaces(now);
    for (const auto& [symbol, interface] : left) {
        if (!now.exports.contains(symbol))
            continue;
        auto it = right.find(symbol);
        if (it == right.end())
            continue;
        const auto& other = it->second;
        std::set<std::pair<size_t, size_t>> seen;
        std::vector<std::string> changes;
        difference(*interface.graph, interface.id, *other.graph, other.id, symbol, seen, changes);
        if (!changes.empty())
            result.findings.push_back({"ABI005",
                                       "error",
                                       symbol,
                                       "Function signature or reachable type layout changed",
                                       std::move(changes),
                                       {}});
    }
    std::set<std::string> warnings = old.warnings;
    warnings.insert(now.warnings.begin(), now.warnings.end());
    if (old.producers != now.producers)
        warnings.insert(
            "DWARF compiler producers differ; cross-compiler ABI equivalence requires review");
    for (auto* corpus : {&old, &now}) {
        std::set<std::string> seen;
        for (const auto& graph : corpus->graphs)
            for (const auto& [symbol, _] : graph.interfaces)
                if (!seen.insert(symbol).second)
                    warnings.insert(
                        "Multiple DWARF definitions of exported symbol require review: " + symbol);
    }
    for (auto* table : {&left, &right})
        for (const auto& [symbol, interface] : *table) {
            std::set<size_t> seen;
            std::set<std::string> reasons;
            unknowns(*interface.graph, interface.id, seen, reasons);
            if (!reasons.empty())
                result.findings.push_back(
                    {"ABI006",
                     "warning",
                     symbol,
                     "ABI surface contains unsupported or incomplete type information",
                     {reasons.begin(), reasons.end()},
                     {}});
        }
    for (const auto& warning : warnings)
        result.findings.push_back({"ABI007", "warning", "", warning, {}, {}});
    for (const auto& path : options.consumers) {
        auto app = consumer(path, old);
        std::set<std::string> affected;
        for (auto& finding : result.findings) {
            if (finding.severity == "note")
                continue;
            if (!finding.symbol.empty() &&
                std::binary_search(app.imports.begin(), app.imports.end(), finding.symbol)) {
                finding.consumers.push_back(app.path);
                affected.insert(finding.symbol);
            }
        }
        app.affected.assign(affected.begin(), affected.end());
        result.consumers.push_back(std::move(app));
    }
    if (std::any_of(result.findings.begin(), result.findings.end(),
                    [](auto& f) { return f.severity == "error"; })) {
        result.decision = "break";
        result.reason = "Structural ABI differences were detected in the analyzed interfaces.";
    } else if (std::any_of(result.findings.begin(), result.findings.end(),
                           [](auto& f) { return f.severity == "warning"; })) {
        result.decision = "review";
        result.reason = "The comparison has unresolved coverage or changes that require review.";
    }
    std::sort(result.findings.begin(), result.findings.end(), [](auto& a, auto& b) {
        return std::tie(a.rule, a.symbol, a.message, a.evidence) <
               std::tie(b.rule, b.symbol, b.message, b.evidence);
    });
    result.findings.erase(std::unique(result.findings.begin(), result.findings.end(),
                                      [](auto& a, auto& b) {
                                          return a.rule == b.rule && a.symbol == b.symbol &&
                                                 a.message == b.message && a.evidence == b.evidence;
                                      }),
                          result.findings.end());
    return result;
}

int Report::exit_code() const {
    return decision == "break" ? 1 : decision == "review" ? 3 : 0;
}
} // namespace abitur
