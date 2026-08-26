#pragma once

#include <optional>
#include <string>

namespace lob::benchmark {

enum class BenchmarkBinaryRole { Latency, AllocationAudit };

struct BuildProvenance final {
  std::string source_root{};
  std::string source_commit{};
  bool source_dirty_at_build{true};
  std::string build_type{};
  std::string compiler_id{};
  std::string compiler_version{};
  std::string compiler_banner{};
  std::string latency_executable{};
  std::string latency_sha256{};
  std::string allocation_executable{};
  std::string allocation_sha256{};
  std::string latency_compile_command{};
  std::string latency_compile_flags{};
  std::string latency_link_command{};
  std::string latency_link_flags{};
  std::string allocation_compile_command{};
  std::string allocation_compile_flags{};
  std::string allocation_link_command{};
  std::string allocation_link_flags{};
};

struct ProvenanceVerificationInputs final {
  std::string execution_commit{};
  bool execution_tree_dirty{true};
  std::string actual_latency_sha256{};
  std::string actual_allocation_sha256{};
  bool executing_expected_binary{};
};

struct ExecutionProvenance final {
  BuildProvenance build{};
  ProvenanceVerificationInputs execution{};
  bool manifest_loaded{};
  bool source_commit_matches{};
  bool both_binary_hashes_match{};
  bool release_configuration_valid{};
  bool canonical_eligible{};
};

[[nodiscard]] std::optional<BuildProvenance> read_build_provenance(
    const std::string& path);
[[nodiscard]] ExecutionProvenance verify_build_provenance(
    BuildProvenance build, ProvenanceVerificationInputs execution) noexcept;
[[nodiscard]] ExecutionProvenance collect_execution_provenance(
    const std::string& manifest_path, BenchmarkBinaryRole role);
[[nodiscard]] std::string default_provenance_path();

}  // namespace lob::benchmark
