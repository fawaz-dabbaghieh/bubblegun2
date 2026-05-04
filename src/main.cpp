#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <argparse/argparse.hpp>

#include "bchains/bchains.h"
#include "graph/packed_graph.h"
#include "util/logger.h"

namespace {

std::string join_argv(int argc, char** argv) {
    std::ostringstream out;
    for (int i = 0; i < argc; ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << argv[i];
    }
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* version = "0.1.0";

    argparse::ArgumentParser program("bubblegun_cpp", version);
    program.add_description("BubbleGun C++ rewrite focused on bubble-chain detection.");

    program.add_argument("-g", "--in_graph")
        .default_value(std::string(""))
        .help("Input GFA graph");

    program.add_argument("-v", "--version")
        .default_value(false)
        .implicit_value(true)
        .help("Print the program version");

    program.add_argument("--log_file")
        .default_value(std::string("log.log"))
        .help("Path to the log file");

    program.add_argument("--log")
        .default_value(std::string("INFO"))
        .help("Accepted for compatibility; the current implementation always writes info-level logs");

    argparse::ArgumentParser bchains_cmd("bchains");
    bchains_cmd.add_description("Detect bubble chains");
    bchains_cmd.add_argument("--bubble_json")
        .default_value(std::string(""))
        .help("Write the detected chains as JSON");
    bchains_cmd.add_argument("--only_simple")
        .default_value(false)
        .implicit_value(true)
        .help("Keep only simple bubbles");
    bchains_cmd.add_argument("--only_super")
        .default_value(false)
        .implicit_value(true)
        .help("Keep only superbubbles");
    bchains_cmd.add_argument("--save_memory")
        .default_value(false)
        .implicit_value(true)
        .help("Skip sequence-heavy outputs and suppress coverage statistics");
    bchains_cmd.add_argument("--chains_gfa")
        .default_value(std::string(""))
        .help("Write only chain nodes and edges as a GFA");

    program.add_subparser(bchains_cmd);

    if (argc == 1) {
        std::cout << "You did not provide any arguments\nTry to use -h or --help for help\n";
        return 1;
    }

    if (argc == 2 && std::string(argv[1]) == "bchains") {
        std::cerr << bchains_cmd;
        return 1;
    }

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n' << program;
        return 1;
    }

    if (program.get<bool>("--version")) {
        std::cout << "bubblegun_cpp version " << version << '\n';
        return 0;
    }

    if (!program.is_subcommand_used("bchains")) {
        std::cerr << program;
        return 1;
    }

    const std::string input_graph = program.get<std::string>("--in_graph");
    if (input_graph.empty()) {
        std::cout << "You need to provide an input graph with -g\n";
        return 1;
    }

    const bool only_simple = bchains_cmd.get<bool>("--only_simple");
    const bool only_super = bchains_cmd.get<bool>("--only_super");
    const bool low_memory = bchains_cmd.get<bool>("--save_memory");
    const std::string json_path = bchains_cmd.get<std::string>("--bubble_json");
    const std::string chains_gfa = bchains_cmd.get<std::string>("--chains_gfa");

    if (only_simple && only_super) {
        std::cout << "You can't mix both only_super and only_simple, choose one or not add these arguments to detect both\n";
        return 1;
    }
    if (low_memory && !chains_gfa.empty()) {
        std::cout << "You cannot combine memory saving with --chains_gfa\n";
        return 1;
    }

    bubblegun::util::Logger logger(program.get<std::string>("--log_file"));
    logger.info(std::string("argument given: ") + join_argv(argc, argv));

    try {
        logger.info("Reading Graph...");
        bubblegun::graph::PackedGraph::LoadOptions load_options;
        load_options.store_sequences = !chains_gfa.empty();
        load_options.store_optional_fields = !chains_gfa.empty();

        bubblegun::graph::PackedGraph graph =
            bubblegun::graph::PackedGraph::load_from_gfa(input_graph, load_options);

        logger.info("Finding bubbles...");
        bubblegun::bchains::Options options;
        options.only_simple = only_simple;
        options.only_super = only_super;
        bubblegun::bchains::Result result = bubblegun::bchains::run(graph, options, &logger);

        std::cout << "The number of Simple Bubbles is " << result.stats.simple_bubbles << '\n'
                  << "The number of Superbubbles is " << result.stats.superbubbles << '\n'
                  << "The number of insertions is " << result.stats.insertions << '\n';

        if (!low_memory &&
            (result.stats.simple_bubbles != 0 || result.stats.superbubbles != 0 || result.stats.insertions != 0)) {
            std::cout << std::setprecision(15)
                      << "Sequence coverage of the bubble chains is " << result.stats.sequence_coverage_pct << "%\n"
                      << "Node coverage of the bubble chains is " << result.stats.node_coverage_pct << "%\n"
                      << "The longest chain seq-wise has " << result.stats.longest_chain_sequence_bp << " bp\n"
                      << "The longest chain bubble_wise has " << result.stats.longest_chain_bubbles << " bubbles\n";
        }

        if (!json_path.empty()) {
            logger.info("Outputting bubble chains json...");
            bubblegun::bchains::write_json(graph, result, json_path);
        }

        if (!chains_gfa.empty()) {
            logger.info("Outputting bubble chains gfa...");
            bubblegun::bchains::write_chain_gfa(graph, result, chains_gfa);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        logger.error(error.what());
        return 1;
    }

    return 0;
}
