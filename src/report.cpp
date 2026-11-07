// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/analyzer.hpp"
#include "abitur/version.hpp"
#include <fstream>
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
    return pretty(Object{{"schema_version", 1},
                         {"tool", std::string("abitur/") + version},
                         {"old", r.old_path},
                         {"new", r.new_path},
                         {"scope", r.scope},
                         {"decision", r.decision},
                         {"decision_reason", r.reason},
                         {"findings", std::move(findings)},
                         {"consumers", std::move(consumers)},
                         {"limitations", strings(r.limitations)}});
}
std::string sarif(const Report& r) {
    Array results;
    for (const auto& f : r.findings) {
        std::string detail = f.message;
        if (!f.symbol.empty())
            detail += " [" + f.symbol + "]";
        for (const auto& e : f.evidence)
            detail += "\n" + e;
        // Binary findings intentionally have no invented source locations.
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
                             {"ABI007", "CoverageRequiresReview"}})
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
