#include "benchmark_provenance.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace lob::benchmark {
namespace {

[[nodiscard]] bool valid_sha256(std::string_view value) noexcept {
  if (value.size() != 64) {
    return false;
  }
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool valid_git_commit(std::string_view value) noexcept {
  if (value.size() != 40) {
    return false;
  }
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool contains_flag(const std::string& flags,
                                 std::string_view flag) {
  std::istringstream input(flags);
  std::string value;
  while (input >> value) {
    if (value == flag) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool contains_flag_prefix(const std::string& flags,
                                        std::string_view prefix) {
  std::istringstream input(flags);
  std::string value;
  while (input >> value) {
    if (value.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool release_compile_flags_valid(const std::string& flags,
                                               bool allocation_audit) {
  const bool required =
      contains_flag(flags, "-O3") && contains_flag(flags, "-march=native") &&
      contains_flag(flags, "-ffast-math") &&
      contains_flag(flags, "-Wall") && contains_flag(flags, "-Wextra") &&
      contains_flag(flags, "-Werror") && contains_flag(flags, "-std=c++20") &&
      contains_flag(flags, "-DNDEBUG") &&
      (!allocation_audit ||
       contains_flag(flags, "-DLOB_ENABLE_ALLOCATION_AUDIT=1"));
  const bool incompatible =
      contains_flag(flags, "-O0") || contains_flag(flags, "-O1") ||
      contains_flag(flags, "-O2") || contains_flag(flags, "-Og") ||
      contains_flag_prefix(flags, "-fsanitize=");
  return required && !incompatible;
}

[[nodiscard]] bool release_link_flags_valid(const std::string& flags) {
  return contains_flag(flags, "-O3") && contains_flag(flags, "-DNDEBUG") &&
         !contains_flag_prefix(flags, "-fsanitize=");
}

[[nodiscard]] bool release_configuration_valid(
    const BuildProvenance& build) {
  return build.build_type == "Release" && build.compiler_id == "GNU" &&
         build.compiler_version == "12.2.0" &&
         !build.compiler_banner.empty() &&
         release_compile_flags_valid(build.latency_compile_flags, false) &&
         release_compile_flags_valid(build.allocation_compile_flags, true) &&
         release_link_flags_valid(build.latency_link_flags) &&
         release_link_flags_valid(build.allocation_link_flags);
}

[[nodiscard]] std::string shell_quote(std::string_view value) {
  std::string result{"'"};
  for (const char character : value) {
    if (character == '\'') {
      result += "'\\''";
    } else {
      result.push_back(character);
    }
  }
  result.push_back('\'');
  return result;
}

[[nodiscard]] std::optional<std::string> run_command(
    const std::string& command) {
  std::array<char, 4096> buffer{};
  std::string result;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return std::nullopt;
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr) {
    result.append(buffer.data());
  }
  const int status = pclose(pipe);
  if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return std::nullopt;
  }
  while (!result.empty() &&
         (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
}

[[nodiscard]] std::string executable_path() {
  std::array<char, 4096> path{};
  const auto length = readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (length <= 0) {
    return {};
  }
  path[static_cast<std::size_t>(length)] = '\0';
  return path.data();
}

[[nodiscard]] std::string sha256_file(const std::string& path) {
  const auto output = run_command("sha256sum -- " + shell_quote(path));
  if (!output || output->size() < 64) {
    return {};
  }
  const auto digest = output->substr(0, 64);
  return valid_sha256(digest) ? digest : std::string{};
}

[[nodiscard]] bool read_value(std::istringstream& input, std::string& value) {
  input >> std::quoted(value);
  input >> std::ws;
  return static_cast<bool>(input) && input.eof();
}

}  // namespace

std::optional<BuildProvenance> read_build_provenance(
    const std::string& path) {
  std::ifstream input(path);
  std::string line;
  if (!std::getline(input, line) || line != "LOB_BENCHMARK_PROVENANCE_V1") {
    return std::nullopt;
  }
  BuildProvenance result;
  bool dirty_seen = false;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string key;
    fields >> key;
    std::string value;
    if (!read_value(fields, value)) {
      return std::nullopt;
    }
    if (key == "SOURCE_ROOT") {
      result.source_root = std::move(value);
    } else if (key == "SOURCE_COMMIT") {
      result.source_commit = std::move(value);
    } else if (key == "SOURCE_DIRTY_AT_BUILD") {
      if (value != "true" && value != "false") {
        return std::nullopt;
      }
      result.source_dirty_at_build = value == "true";
      dirty_seen = true;
    } else if (key == "BUILD_TYPE") {
      result.build_type = std::move(value);
    } else if (key == "COMPILER_ID") {
      result.compiler_id = std::move(value);
    } else if (key == "COMPILER_VERSION") {
      result.compiler_version = std::move(value);
    } else if (key == "COMPILER_BANNER") {
      result.compiler_banner = std::move(value);
    } else if (key == "LATENCY_EXECUTABLE") {
      result.latency_executable = std::move(value);
    } else if (key == "LATENCY_SHA256") {
      result.latency_sha256 = std::move(value);
    } else if (key == "ALLOCATION_EXECUTABLE") {
      result.allocation_executable = std::move(value);
    } else if (key == "ALLOCATION_SHA256") {
      result.allocation_sha256 = std::move(value);
    } else if (key == "LATENCY_COMPILE_COMMAND") {
      result.latency_compile_command = std::move(value);
    } else if (key == "LATENCY_COMPILE_FLAGS") {
      result.latency_compile_flags = std::move(value);
    } else if (key == "LATENCY_LINK_COMMAND") {
      result.latency_link_command = std::move(value);
    } else if (key == "LATENCY_LINK_FLAGS") {
      result.latency_link_flags = std::move(value);
    } else if (key == "ALLOCATION_COMPILE_COMMAND") {
      result.allocation_compile_command = std::move(value);
    } else if (key == "ALLOCATION_COMPILE_FLAGS") {
      result.allocation_compile_flags = std::move(value);
    } else if (key == "ALLOCATION_LINK_COMMAND") {
      result.allocation_link_command = std::move(value);
    } else if (key == "ALLOCATION_LINK_FLAGS") {
      result.allocation_link_flags = std::move(value);
    } else {
      return std::nullopt;
    }
  }
  if (!dirty_seen || result.source_root.empty() ||
      !valid_git_commit(result.source_commit) ||
      result.compiler_id.empty() || result.compiler_version.empty() ||
      result.compiler_banner.empty() || result.latency_executable.empty() ||
      result.allocation_executable.empty() ||
      !valid_sha256(result.latency_sha256) ||
      !valid_sha256(result.allocation_sha256) ||
      result.latency_compile_command.empty() ||
      result.latency_compile_flags.empty() ||
      result.latency_link_command.empty() ||
      result.allocation_compile_command.empty() ||
      result.allocation_compile_flags.empty() ||
      result.allocation_link_command.empty()) {
    return std::nullopt;
  }
  return result;
}

ExecutionProvenance verify_build_provenance(
    BuildProvenance build, ProvenanceVerificationInputs execution) noexcept {
  ExecutionProvenance result;
  result.manifest_loaded = true;
  result.source_commit_matches =
      !execution.execution_commit.empty() &&
      execution.execution_commit == build.source_commit;
  result.both_binary_hashes_match =
      execution.actual_latency_sha256 == build.latency_sha256 &&
      execution.actual_allocation_sha256 == build.allocation_sha256;
  result.release_configuration_valid = release_configuration_valid(build);
  result.canonical_eligible =
      !build.source_dirty_at_build && !execution.execution_tree_dirty &&
      result.source_commit_matches && result.both_binary_hashes_match &&
      execution.executing_expected_binary &&
      result.release_configuration_valid;
  result.build = std::move(build);
  result.execution = std::move(execution);
  return result;
}

ExecutionProvenance collect_execution_provenance(
    const std::string& manifest_path, BenchmarkBinaryRole role) {
  const auto manifest = read_build_provenance(manifest_path);
  if (!manifest) {
    return {};
  }
  ProvenanceVerificationInputs execution;
  const auto commit = run_command("git -C " + shell_quote(manifest->source_root) +
                                  " rev-parse HEAD");
  if (commit) {
    execution.execution_commit = *commit;
  }
  const auto status = run_command(
      "git -C " + shell_quote(manifest->source_root) +
      " status --porcelain --untracked-files=normal");
  execution.execution_tree_dirty = !status || !status->empty();
  execution.actual_latency_sha256 = sha256_file(manifest->latency_executable);
  execution.actual_allocation_sha256 =
      sha256_file(manifest->allocation_executable);
  const auto expected = role == BenchmarkBinaryRole::Latency
                            ? manifest->latency_executable
                            : manifest->allocation_executable;
  std::error_code error;
  execution.executing_expected_binary = std::filesystem::equivalent(
      executable_path(), expected, error);
  return verify_build_provenance(*manifest, std::move(execution));
}

std::string default_provenance_path() {
  const auto executable = executable_path();
  if (executable.empty()) {
    return {};
  }
  return (std::filesystem::path(executable).parent_path() /
          "phase2_benchmark.provenance")
      .string();
}

}  // namespace lob::benchmark
