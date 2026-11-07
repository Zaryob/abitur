// SPDX-FileCopyrightText: 2026 Suleyman Poyraz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "abitur/analyzer.hpp"
#include "abitur/version.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        abitur::Options options;
        std::string format = "text", output;
        bool have_old = false, have_new = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--version") {
                std::cout << "abitur " << abitur::version << "\n";
                return 0;
            }
            if (arg == "--license") {
                std::cout << "Abitur " << abitur::version
                          << "\n"
                             "Copyright (C) 2026 Suleyman Poyraz\n"
                             "License: GNU GPL version 3 or later (GPL-3.0-or-later).\n"
                             "This is free software: you may modify and redistribute it under that "
                             "license.\n"
                             "This program comes with ABSOLUTELY NO WARRANTY.\n"
                             "See LICENSE and https://www.gnu.org/licenses/gpl-3.0.html\n"
                             "Third-party components retain their own licenses; see "
                             "THIRD_PARTY_NOTICES.md.\n";
                return 0;
            }
            if (arg == "--help" || arg == "-h") {
                std::cout
                    << "Usage: abitur OLD.so NEW.so [--consumer APP]... [--format text|json|sarif] "
                       "[--output PATH]\n"
                       "       abitur OLD.a NEW.a [--format text|json|sarif] [--output PATH]\n"
                       "       abitur --version | --license | --help\n"
                       "Exit: 0 pass, 1 break, 2 input/tool error, 3 review required.\n"
                       "Embedded DWARF required. Consumer imports are evidence, not a "
                       "runtime-safety proof.\n";
                return 0;
            }
            if (arg == "--consumer" || arg == "--format" || arg == "--output") {
                if (++i == argc)
                    throw std::runtime_error("Missing value for " + arg);
                if (arg == "--consumer")
                    options.consumers.emplace_back(argv[i]);
                else if (arg == "--format")
                    format = argv[i];
                else
                    output = argv[i];
            } else if (arg.starts_with('-'))
                throw std::runtime_error("Unknown option: " + arg);
            else if (!have_old) {
                options.old_path = arg;
                have_old = true;
            } else if (!have_new) {
                options.new_path = arg;
                have_new = true;
            } else
                throw std::runtime_error("Unexpected argument: " + arg);
        }
        if (!have_old || !have_new)
            throw std::runtime_error("Provide old and new libraries; see --help");
        if (format != "text" && format != "json" && format != "sarif")
            throw std::runtime_error("Format must be text, json or sarif");
        auto report = abitur::compare(options);
        auto contents = format == "json"    ? abitur::json(report)
                        : format == "sarif" ? abitur::sarif(report)
                                            : abitur::text(report);
        if (output.empty())
            std::cout << contents;
        else
            abitur::write_file(output, contents);
        return report.exit_code();
    } catch (const std::exception& e) {
        std::cerr << "abitur: " << e.what() << "\n";
        return 2;
    }
}
