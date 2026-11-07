// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/analyzer.hpp"
#include "abitur/version.hpp"
#include <fstream>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>
#include <sstream>
#include <stdexcept>

namespace abitur {
namespace {
using llvm::json::Array;
using llvm::json::Object;
using llvm::json::Value;

Array strings(const std::vector<std::string>& values) {
    Array result;
    for (const auto& value : values)
        result.push_back(value);
    return result;
}

std::string pretty(Value value) {
    return llvm::formatv("{0:2}\n", value).str();
}
} // namespace

std::string json(const Report& r) {
    Array findings, consumers;
    for (const auto& f : r.findings)
        findings.push_back(Object{{"rule_id", f.rule},
                                  {"severity", f.severity},
                                  {"symbol", f.symbol},
                                  {"message", f.message},
                                  {"evidence", strings(f.evidence)},
                                  {"consumers", strings(f.consumers)}});
    for (const auto& c : r.consumers)
        consumers.push_back(Object{{"path", c.path},
                                   {"imported_symbols", strings(c.imports)},
                                   {"affected_symbols", strings(c.affected)}});

    Object root{{"schema_version", 1},
                {"tool", std::string("abitur/") + version},
                {"old", r.old_path},
                {"new", r.new_path},
                {"scope", r.scope},
                {"decision", r.decision},
                {"decision_reason", r.reason},
                {"findings", std::move(findings)},
                {"consumers", std::move(consumers)},
                {"limitations", strings(r.limitations)}};

    if (r.elf_analysis.has_value()) {
        const auto& ea = *r.elf_analysis;
        Array segments;
        for (const auto& seg : ea.segments) {
            segments.push_back(Object{{"type", seg.type},
                                      {"vaddr", static_cast<int64_t>(seg.vaddr)},
                                      {"paddr", static_cast<int64_t>(seg.paddr)},
                                      {"file_size", static_cast<int64_t>(seg.file_size)},
                                      {"mem_size", static_cast<int64_t>(seg.mem_size)},
                                      {"align", static_cast<int64_t>(seg.align)},
                                      {"flags", seg.flags}});
        }
        Array sections;
        for (const auto& sec : ea.sections) {
            sections.push_back(Object{{"name", sec.name},
                                      {"type", sec.type},
                                      {"address", static_cast<int64_t>(sec.address)},
                                      {"offset", static_cast<int64_t>(sec.offset)},
                                      {"size", static_cast<int64_t>(sec.size)},
                                      {"alignment", static_cast<int64_t>(sec.alignment)},
                                      {"flags", sec.flags}});
        }
        Array exports;
        for (const auto& exp : ea.exports) {
            exports.push_back(Object{{"name", exp.name},
                                     {"demangled", exp.demangled},
                                     {"type", exp.type},
                                     {"binding", exp.binding},
                                     {"visibility", exp.visibility},
                                     {"size", static_cast<int64_t>(exp.size)},
                                     {"address", static_cast<int64_t>(exp.address)},
                                     {"signature", exp.signature}});
        }
        Array type_layouts;
        for (const auto& tl : ea.type_layouts) {
            Array members;
            for (const auto& m : tl.members) {
                members.push_back(Object{{"name", m.name},
                                         {"type", m.type},
                                         {"offset", static_cast<int64_t>(m.offset)},
                                         {"size", static_cast<int64_t>(m.size)},
                                         {"is_padding", m.is_padding}});
            }
            type_layouts.push_back(Object{{"name", tl.name},
                                          {"kind", tl.kind},
                                          {"byte_size", static_cast<int64_t>(tl.byte_size)},
                                          {"alignment", static_cast<int64_t>(tl.alignment)},
                                          {"members", std::move(members)}});
        }

        Object elf_obj{{"path", ea.path},
                       {"file_type", ea.file_type},
                       {"machine", ea.machine},
                       {"elf_class", ea.elf_class},
                       {"endianness", ea.endianness},
                       {"osabi", ea.osabi},
                       {"abi_version", ea.abi_version},
                       {"entry_point", static_cast<int64_t>(ea.entry_point)},
                       {"soname", ea.soname},
                       {"rpath", ea.rpath},
                       {"runpath", ea.runpath},
                       {"needed_libraries", strings(ea.needed_libraries)},
                       {"security_features", strings(ea.security_features)},
                       {"segments", std::move(segments)},
                       {"sections", std::move(sections)},
                       {"exports", std::move(exports)},
                       {"imports", strings(ea.imports)},
                       {"type_layouts", std::move(type_layouts)}};
        root["elf_analysis"] = std::move(elf_obj);
    }

    return pretty(std::move(root));
}

std::string sarif(const Report& r) {
    Array results;
    for (const auto& f : r.findings) {
        std::string detail = f.message;
        if (!f.symbol.empty())
            detail += " [" + f.symbol + "]";
        for (const auto& e : f.evidence)
            detail += "\n" + e;
        results.push_back(Object{
            {"ruleId", f.rule},
            {"level", f.severity},
            {"message", Object{{"text", detail}}},
            {"properties", Object{{"symbol", f.symbol}, {"consumers", strings(f.consumers)}}}});
    }
    Array rules;
    for (auto [id, label] : {std::pair{"ABI001", "TargetChanged"},
                             {"ABI002", "SymbolRemoved"},
                             {"ABI003", "SymbolPropertiesChanged"},
                             {"ABI004", "SymbolAdded"},
                             {"ABI005", "SignatureOrLayoutChanged"},
                             {"ABI006", "IncompleteTypeCoverage"},
                             {"ABI007", "CoverageRequiresReview"},
                             {"ELF001", "ElfFormatWarning"},
                             {"ELF002", "DwarfInterfaceNotice"},
                             {"SEC001", "SecurityFeatureAlert"},
                             {"CON001", "ConsumerUnsatisfiedImport"}})
        rules.push_back(
            Object{{"id", id}, {"name", label}, {"shortDescription", Object{{"text", label}}}});
    Array runs;
    runs.push_back(Object{{"tool", Object{{"driver", Object{{"name", "abitur"},
                                                            {"version", version},
                                                            {"rules", std::move(rules)}}}}},
                          {"results", std::move(results)},
                          {"properties", Object{{"decision", r.decision},
                                                {"reason", r.reason},
                                                {"old", r.old_path},
                                                {"new", r.new_path},
                                                {"limitations", strings(r.limitations)}}}});
    return pretty(Object{{"$schema", "https://json.schemastore.org/sarif-2.1.0.json"},
                         {"version", "2.1.0"},
                         {"runs", std::move(runs)}});
}

std::string text(const Report& r) {
    if (r.elf_analysis.has_value()) {
        const auto& elf = *r.elf_analysis;
        bool is_macho = elf.osabi.find("Mach-O") != std::string::npos || elf.file_type.find("Mach-O") != std::string::npos;
        std::ostringstream out;
        out << "================================================================================\n"
            << "ABITUR " << (is_macho ? "MACH-O" : "ELF") << " ANALYSIS: " << elf.path << "\n"
            << "================================================================================\n"
            << "Status:      " << r.decision << " (" << r.reason << ")\n"
            << "Type:        " << elf.file_type << "\n"
            << "Class:       " << elf.elf_class << " | Endian: " << elf.endianness << "\n"
            << "Target:      " << elf.machine << " | OS/ABI: " << elf.osabi
            << " [abi_version=" << static_cast<int>(elf.abi_version) << "]\n";
        if (elf.entry_point)
            out << "Entry:       0x" << llvm::utohexstr(elf.entry_point) << "\n";
        if (!elf.soname.empty())
            out << (is_macho ? "Install Name:" : "SONAME:     ") << " " << elf.soname << "\n";
        if (!elf.needed_libraries.empty()) {
            out << "Needed:      ";
            for (size_t i = 0; i < elf.needed_libraries.size(); ++i)
                out << (i ? ", " : "") << elf.needed_libraries[i];
            out << "\n";
        }
        if (!elf.security_features.empty()) {
            out << "Security:    ";
            for (size_t i = 0; i < elf.security_features.size(); ++i)
                out << (i ? " | " : "") << elf.security_features[i];
            out << "\n";
        }

        out << "\n[Program Headers / Segments] (" << elf.segments.size() << ")\n";
        for (const auto& seg : elf.segments) {
            out << "  "
                << llvm::formatv("{0,-15} vaddr: 0x{1,-8} file: 0x{2,-6} mem: 0x{3,-6} flags: {4,-4} align: 0x{5}\n",
                                 seg.type, llvm::utohexstr(seg.vaddr),
                                 llvm::utohexstr(seg.file_size), llvm::utohexstr(seg.mem_size),
                                 seg.flags, llvm::utohexstr(seg.align)).str();
        }

        out << "\n[Sections] (" << elf.sections.size() << ")\n";
        for (const auto& sec : elf.sections) {
            out << "  "
                << llvm::formatv("{0,-22} {1,-12} addr: 0x{2,-8} size: 0x{3,-6} [{4}]\n",
                                 sec.name, sec.type, llvm::utohexstr(sec.address),
                                 llvm::utohexstr(sec.size), sec.flags).str();
        }

        out << "\n[Exported Interfaces & Symbols] (" << elf.exports.size() << ")\n";
        for (const auto& exp : elf.exports) {
            out << "  [" << exp.binding << " " << exp.type << "] " << exp.name;
            if (!exp.demangled.empty() && exp.demangled != exp.name)
                out << " (" << exp.demangled << ")";
            if (!exp.signature.empty())
                out << " : " << exp.signature;
            out << "  [size: " << exp.size << ", addr: 0x" << llvm::utohexstr(exp.address) << "]\n";
        }

        if (!elf.imports.empty()) {
            out << "\n[Imported / External Dependencies] (" << elf.imports.size() << ")\n  ";
            for (size_t i = 0; i < elf.imports.size(); ++i) {
                out << (i && i % 4 == 0 ? "\n  " : (i ? ", " : "")) << elf.imports[i];
            }
            out << "\n";
        }

        if (!elf.type_layouts.empty()) {
            out << "\n[Reachable Struct / Type Layouts] (" << elf.type_layouts.size() << ")\n";
            for (const auto& tl : elf.type_layouts) {
                out << "  " << tl.kind << " " << tl.name << " (size: " << tl.byte_size
                    << " bytes, align: " << tl.alignment << ")\n";
                for (const auto& m : tl.members) {
                    if (m.is_padding) {
                        out << "    +0x" << llvm::utohexstr(m.offset) << " [" << m.size << " bytes padding]\n";
                    } else {
                        out << "    +0x" << llvm::utohexstr(m.offset) << " " << m.type << " " << m.name
                            << " (" << m.size << " bytes)\n";
                    }
                }
            }
        }

        if (!r.consumers.empty()) {
            out << "\n[Consumer Verification]\n";
            for (const auto& c : r.consumers) {
                out << "  Consumer " << c.path << ": " << c.imports.size() << " matched imports\n";
            }
        }

        if (!r.findings.empty()) {
            out << "\n[Findings & Alerts] (" << r.findings.size() << ")\n";
            for (const auto& f : r.findings) {
                out << "  [" << f.severity << "] " << f.rule << " " << f.symbol << ": " << f.message << "\n";
                for (const auto& e : f.evidence)
                    out << "    " << e << "\n";
            }
        }

        return out.str();
    }

    std::ostringstream out;
    out << "ABITUR  " << r.decision << "\n"
        << r.reason << "\n"
        << r.old_path << " -> " << r.new_path << "\nScope: " << r.scope << "\n";
    for (const auto& f : r.findings) {
        out << "\n[" << f.severity << "] " << f.rule << " " << f.symbol << ": " << f.message
            << "\n";
        for (const auto& e : f.evidence)
            out << "  " << e << "\n";
        for (const auto& c : f.consumers)
            out << "  Direct consumer import: " << c << "\n";
    }
    for (const auto& c : r.consumers)
        out << "\nConsumer " << c.path << ": " << c.affected.size() << " affected direct imports ("
            << c.imports.size() << " matched imports)\n";
    out << "\nLimits:\n";
    for (const auto& l : r.limitations)
        out << "- " << l << "\n";
    return out.str();
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    if (!output || !(output << contents))
        throw std::runtime_error("Cannot write report: " + path.string());
}
} // namespace abitur
