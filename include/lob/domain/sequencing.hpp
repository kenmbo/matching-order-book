#pragma once

#include "lob/domain/types.hpp"

#include <cstdint>

namespace lob {

enum class SequenceAllocationResult : std::uint8_t {
  Assigned = 0,
  Exhausted = 1,
};

struct CommandSequenceAssignment final {
  SequenceAllocationResult result{SequenceAllocationResult::Exhausted};
  CommandSequence sequence{};

  [[nodiscard]] constexpr bool assigned() const noexcept {
    return result == SequenceAllocationResult::Assigned;
  }
};

struct EngineSequenceBatch final {
  SequenceAllocationResult result{SequenceAllocationResult::Exhausted};
  EngineSequence first{};
  EngineSequence last{};
  std::uint64_t event_count{};

  [[nodiscard]] constexpr bool assigned() const noexcept {
    return result == SequenceAllocationResult::Assigned;
  }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return assigned() && event_count == 0;
  }
};

class SequenceState final {
 public:
  constexpr SequenceState() noexcept = default;
  constexpr SequenceState(CommandSequence last_command,
                          EngineSequence last_engine) noexcept
      : last_command_(last_command), last_engine_(last_engine) {}

  constexpr void reject_before_acceptance() noexcept {}

  [[nodiscard]] constexpr CommandSequenceAssignment accept_command() noexcept {
    if (last_command_.value() == CommandSequence::maximum_value) {
      return {};
    }

    const auto next = last_command_.value() + std::uint64_t{1};
    last_command_ = detail::DomainAccess::from_rep<CommandSequence>(next);
    return {SequenceAllocationResult::Assigned, last_command_};
  }

  constexpr void abort_event_batch() noexcept {}

  [[nodiscard]] constexpr EngineSequenceBatch commit_event_batch(
      std::uint64_t event_count) noexcept {
    if (event_count == 0) {
      return {SequenceAllocationResult::Assigned, {}, {}, 0};
    }

    const auto remaining = EngineSequence::maximum_value - last_engine_.value();
    if (event_count > remaining) {
      return {};
    }

    const auto first_value = last_engine_.value() + std::uint64_t{1};
    const auto last_value = last_engine_.value() + event_count;
    const auto first =
        detail::DomainAccess::from_rep<EngineSequence>(first_value);
    last_engine_ = detail::DomainAccess::from_rep<EngineSequence>(last_value);
    return {SequenceAllocationResult::Assigned, first, last_engine_, event_count};
  }

  [[nodiscard]] constexpr CommandSequence last_command() const noexcept {
    return last_command_;
  }
  [[nodiscard]] constexpr EngineSequence last_engine() const noexcept {
    return last_engine_;
  }

 private:
  CommandSequence last_command_{};
  EngineSequence last_engine_{};
};

static_assert(std::is_trivially_copyable_v<CommandSequenceAssignment>);
static_assert(std::is_trivially_copyable_v<EngineSequenceBatch>);
static_assert(std::is_trivially_copyable_v<SequenceState>);
static_assert(std::is_standard_layout_v<CommandSequenceAssignment>);
static_assert(std::is_standard_layout_v<EngineSequenceBatch>);
static_assert(std::is_standard_layout_v<SequenceState>);

}  // namespace lob
