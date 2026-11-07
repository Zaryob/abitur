// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/analyzer.hpp"
#include <algorithm>
#include <functional>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFDie.h>
#include <llvm/DebugInfo/DWARF/DWARFUnit.h>
#include <llvm/Object/Archive.h>
#include <llvm/Object/ELFObjectFile.h>
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
        // Typedef spelling and CV qualifiers do not change the physical ABI.
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
        graph.nodes.emplace_back(); // Reserve ID before following recursive references.
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
                    continue; // static member
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
            // Value changes affect semantics, but not calling convention/layout.
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
    Graph build(DWARFContext& context, const std::set<std::string>& exports) {
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
        for (auto root : roots)
            walk(root, [&](DWARFDie die) {
                if (die.getTag() != DW_TAG_subprogram && die.getTag() != DW_TAG_variable)
                    return;
                const char* linkage = die.getLinkageName();
                auto symbol = linkage ? std::string(linkage) : name(die);
                if (!exports.contains(symbol))
                    return;
                // Prefer a definition over a prototype. Optimized definitions may
                // inherit their type/parameters from specification or abstract origin.
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
    // e_ident byte fields are endian-independent. Reading this field from the
    // validated ELF header also supports LLVM versions without getEIdentOSABI.
    auto osabi = static_cast<unsigned char>(object.getMemoryBufferRef().getBuffer()[ELF::EI_OSABI]);
    return std::to_string(object.getEMachine()) + "/" +
           std::to_string(object.getBytesInAddress() * 8) +
           (object.isLittleEndian() ? "/little" : "/big") + "/osabi=" + std::to_string(osabi);
}

void read_object(const ObjectFile& object, Corpus& corpus) {
    auto* elf = dyn_cast<ELFObjectFileBase>(&object);
    if (!elf)
        fail("Only ELF inputs are supported");
    if (elf->getEType() != (corpus.archive ? ELF::ET_REL : ELF::ET_DYN))
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
    auto graph = GraphBuilder(corpus.warnings).build(*context, local_exports);
    if (!diagnostics.empty())
        fail("DWARF parsing failed: " + diagnostics.front());
    corpus.exports.insert(local_exports.begin(), local_exports.end());
    corpus.graphs.push_back(std::move(graph));
}

Corpus load(const std::filesystem::path& path) {
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
            read_object(*object, corpus);
        }
        if (error)
            fail(toString(std::move(error)));
        if (corpus.graphs.empty())
            fail("Archive has no ELF members");
    } else {
        auto* object = dyn_cast<ObjectFile>(binary.getBinary());
        if (!object)
            fail("Expected ELF shared library or regular archive");
        read_object(*object, corpus);
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
    // Match members by name so reordering reports concrete offset changes,
    // rather than pairing unrelated fields at the same declaration index.
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
    auto* elf = dyn_cast<ELFObjectFileBase>(binary.getBinary());
    if (!elf || (elf->getEType() != ELF::ET_EXEC && elf->getEType() != ELF::ET_DYN))
        fail("Consumer must be an ELF executable or shared library");
    if (target(*elf) != library.target)
        fail("Consumer target differs from library");
    Consumer result{path.string(), {}, {}};
    std::set<std::string> imports;
    for (auto symbol : elf->getDynamicSymbolIterators())
        if (checked(symbol.getFlags()) & SymbolRef::SF_Undefined) {
            auto n = checked(symbol.getName()).str();
            if (library.exports.contains(n))
                imports.insert(n);
        }
    result.imports.assign(imports.begin(), imports.end());
    return result;
}
} // namespace

Report compare(const Options& options) {
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
                   "optimized/LTO debug information can omit ABI details."}};
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
            continue; // Missing DWARF is reported below.
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
