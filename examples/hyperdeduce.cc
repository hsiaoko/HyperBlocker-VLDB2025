// Command-line driver for the hyperdeduce library. Loads base relations from a
// directory of <predicate>.csv files, reads Datalog rules, builds an execution
// plan, evaluates to a fixpoint on the chosen backend, and writes derived
// relations to CSV.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <gflags/gflags.h>

#include "hyperdeduce/backend/cpu/cpu_backend.h"
#include "hyperdeduce/engine/engine.h"
#include "hyperdeduce/input/relation_loader.h"
#include "hyperdeduce/input/rule_frontend.h"
#include "hyperdeduce/io/csv.h"
#ifdef HYPERDEDUCE_WITH_CUDA
#include "hyperdeduce/backend/gpu/gpu_backend.h"
#endif

DEFINE_string(data_dir, "", "directory of <predicate>.csv base relations");
DEFINE_string(rules, "", "path to a Datalog (.dl) or legacy YAML (.yaml) rules file");
DEFINE_string(tables, "tableA,tableB",
              "comma-separated relation names for YAML table indices 0,1");
DEFINE_string(output_dir, "", "directory to write derived relations");
DEFINE_string(backend, "cpu", "execution backend: cpu or gpu");
DEFINE_int32(gpus, 0, "number of GPUs to use (0 = all visible)");
DEFINE_bool(explain, false, "print the execution plans before running");
DEFINE_bool(report, true, "print the run report (load/plan/eval stats and timing)");
DEFINE_bool(mqo, true, "share common relational bodies across rules (multi-query optimization)");
DEFINE_bool(has_header, false, "treat the first CSV row as column names");

namespace {

hyperdeduce::StatusOr<std::string> ReadFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    return hyperdeduce::Status::Error("cannot open rules file: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

} // namespace

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  using hyperdeduce::Status;
  using hyperdeduce::StatusOr;

  hyperdeduce::Engine engine;
  hyperdeduce::LoadOptions options;
  options.has_header = FLAGS_has_header;

  if (!FLAGS_data_dir.empty()) {
    for (const auto &entry :
         std::filesystem::directory_iterator(FLAGS_data_dir)) {
      std::string extension = entry.path().extension().string();
      if (!extension.empty()) {
        extension = extension.substr(1);
      }
      // The loader registry decides which files are data; rule files and other
      // unrecognized extensions are skipped.
      if (hyperdeduce::LoaderRegistry::Instance().ByExtension(extension) ==
          nullptr) {
        continue;
      }
      const std::string name = entry.path().stem().string();
      auto loaded = engine.LoadRelation(entry.path().string(), name, options);
      if (!loaded.ok()) {
        std::cerr << "load failed: " << loaded.status().message() << "\n";
        return 1;
      }
    }
  }

  hyperdeduce::RuleInputContext context;
  std::stringstream stream(FLAGS_tables);
  std::string table_name;
  while (std::getline(stream, table_name, ',')) {
    if (!table_name.empty()) {
      context.table_names.push_back(table_name);
    }
  }

  // The input module dispatches by file extension; the CLI does not branch on
  // the format itself. Unknown extensions fall back to Datalog text.
  auto add_rule_file = [&](const std::string &path) -> Status {
    auto text = ReadFile(path);
    if (!text.ok()) {
      return text.status();
    }
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty()) {
      ext = ext.substr(1);
    }
    const hyperdeduce::RuleFrontend *frontend =
        hyperdeduce::FrontendRegistry::Instance().ByExtension(ext);
    if (frontend == nullptr) {
      frontend = hyperdeduce::FrontendRegistry::Instance().ByName("datalog");
    }
    return engine.AddRules(text.value(), *frontend, context);
  };

  // --rules accepts a single file or a folder of rule files. A folder enables
  // multiple queries that the optimizer can share common subexpressions across.
  if (std::filesystem::is_directory(FLAGS_rules)) {
    for (const auto &entry :
         std::filesystem::directory_iterator(FLAGS_rules)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      std::string ext = entry.path().extension().string();
      if (!ext.empty()) {
        ext = ext.substr(1);
      }
      if (hyperdeduce::FrontendRegistry::Instance().ByExtension(ext) == nullptr) {
        continue;
      }
      Status added = add_rule_file(entry.path().string());
      if (!added.ok()) {
        std::cerr << "parse failed (" << entry.path().string()
                  << "): " << added.message() << "\n";
        return 1;
      }
    }
  } else {
    Status added = add_rule_file(FLAGS_rules);
    if (!added.ok()) {
      std::cerr << "parse failed: " << added.message() << "\n";
      return 1;
    }
  }

  engine.set_mqo(FLAGS_mqo);
  auto plan = engine.BuildPlan();
  if (!plan.ok()) {
    std::cerr << "plan failed: " << plan.status().message() << "\n";
    return 1;
  }
  if (FLAGS_explain) {
    std::cout << plan.value();
  }

  std::unique_ptr<hyperdeduce::Backend> backend;
  if (FLAGS_backend == "cpu") {
    backend = std::make_unique<hyperdeduce::CpuBackend>();
  } else if (FLAGS_backend == "gpu") {
#ifdef HYPERDEDUCE_WITH_CUDA
    backend = std::make_unique<hyperdeduce::GpuBackend>(FLAGS_gpus);
#else
    std::cerr << "this build has no CUDA support\n";
    return 1;
#endif
  } else {
    std::cerr << "unknown backend: " << FLAGS_backend << "\n";
    return 1;
  }

  Status executed = engine.Execute(backend.get());
  if (!executed.ok()) {
    std::cerr << "execute failed: " << executed.message() << "\n";
    return 1;
  }

  if (!FLAGS_output_dir.empty()) {
    std::filesystem::create_directories(FLAGS_output_dir);
    for (const std::string &name : engine.IdbNames()) {
      if (name.rfind("__shared_", 0) == 0) {
        continue; // internal MQO intermediate, not a query result
      }
      const hyperdeduce::Relation *relation = engine.GetRelation(name);
      const std::string path = FLAGS_output_dir + "/" + name + ".csv";
      Status written = hyperdeduce::WriteCsv(
          *relation, engine.catalog().dictionary(), path);
      if (!written.ok()) {
        std::cerr << "write failed: " << written.message() << "\n";
        return 1;
      }
      std::cout << name << ": " << relation->num_rows() << " rows -> " << path
                << "\n";
    }
  } else {
    for (const std::string &name : engine.IdbNames()) {
      if (name.rfind("__shared_", 0) == 0) {
        continue;
      }
      std::cout << name << ": " << engine.GetRelation(name)->num_rows()
                << " rows\n";
    }
  }

  if (FLAGS_report) {
    std::cout << hyperdeduce::FormatReport(engine.run_stats());
  }

  gflags::ShutDownCommandLineFlags();
  return 0;
}