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
#include "hyperdeduce/io/csv.h"

DEFINE_string(data_dir, "", "directory of <predicate>.csv base relations");
DEFINE_string(rules, "", "path to a Datalog rules file");
DEFINE_string(output_dir, "", "directory to write derived relations");
DEFINE_string(backend, "cpu", "execution backend: cpu or gpu");
DEFINE_bool(explain, false, "print the execution plans before running");
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
  hyperdeduce::CsvOptions options;
  options.has_header = FLAGS_has_header;

  if (!FLAGS_data_dir.empty()) {
    for (const auto &entry :
         std::filesystem::directory_iterator(FLAGS_data_dir)) {
      if (entry.path().extension() != ".csv") {
        continue;
      }
      const std::string name = entry.path().stem().string();
      Status loaded = engine.LoadCsv(entry.path().string(), name, options);
      if (!loaded.ok()) {
        std::cerr << "load failed: " << loaded.message() << "\n";
        return 1;
      }
    }
  }

  auto rules = ReadFile(FLAGS_rules);
  if (!rules.ok()) {
    std::cerr << rules.status().message() << "\n";
    return 1;
  }
  Status added = engine.AddRules(rules.value());
  if (!added.ok()) {
    std::cerr << "parse failed: " << added.message() << "\n";
    return 1;
  }

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
      std::cout << name << ": " << engine.GetRelation(name)->num_rows()
                << " rows\n";
    }
  }

  gflags::ShutDownCommandLineFlags();
  return 0;
}