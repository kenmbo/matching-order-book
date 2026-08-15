#pragma once

#include "lob/domain/contracts.hpp"
#include "lob/domain/sequencing.hpp"
#include "lob/egress/lossless_outbox.hpp"
#include "lob/storage/order_book_storage.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace lob {

struct MatchingEngineTestAccess;

inline constexpr std::size_t kMaximumFillsPerCommand = std::size_t{1} << 8;
inline constexpr std::size_t kDefaultExecutionOutboxCapacity =
    std::size_t{1} << 10;
inline constexpr std::size_t kDefaultControlOutboxCapacity =
    std::size_t{1} << 4;
inline constexpr std::size_t kMinimumStatusOutboxCapacity = 2;

struct LosslessOutboxLimits final {
  std::size_t execution_reports{kDefaultExecutionOutboxCapacity};
  std::size_t status_events{kDefaultControlOutboxCapacity};
};

struct NewOrderResult final {
  OrderBookResult result{OrderBookResult::CapacityExhausted};
  CommandSequence command_sequence{};
  std::array<ExecutionReport, kMaximumFillsPerCommand> execution_reports{};
  std::size_t execution_report_count{};

  [[nodiscard]] std::span<const ExecutionReport> reports() const noexcept {
    return {execution_reports.data(), execution_report_count};
  }
};

using CancelOrderResult = NewOrderResult;
using AmendOrderResult = NewOrderResult;

struct LifecycleCommandResult final {
  OrderBookResult result{OrderBookResult::CapacityExhausted};
  CommandSequence command_sequence{};
};

class MatchingEngine final {
 public:
  explicit MatchingEngine(InstrumentId instrument_id,
                          StorageLimits storage_limits = {},
                          LosslessOutboxLimits outbox_limits = {});

  MatchingEngine(const MatchingEngine&) = delete;
  MatchingEngine& operator=(const MatchingEngine&) = delete;
  MatchingEngine(MatchingEngine&&) = delete;
  MatchingEngine& operator=(MatchingEngine&&) = delete;
  ~MatchingEngine() = default;

  [[nodiscard]] NewOrderResult process(const NewOrder& order);
  [[nodiscard]] CancelOrderResult process(const CancelOrder& order);
  [[nodiscard]] AmendOrderResult process(const AmendOrder& order);
  [[nodiscard]] LifecycleCommandResult process(
      const HaltInstrument& command) noexcept;
  [[nodiscard]] LifecycleCommandResult process(
      const ResumeInstrument& command) noexcept;
  [[nodiscard]] LifecycleCommandResult process(
      const CloseInstrument& command) noexcept;
  [[nodiscard]] LifecycleCommandResult process(
      const OpenInstrument& command) noexcept;

  [[nodiscard]] InstrumentId instrument_id() const noexcept;
  [[nodiscard]] InstrumentState instrument_state() const noexcept;
  [[nodiscard]] CommandSequence last_command_sequence() const noexcept;
  [[nodiscard]] EngineSequence last_engine_sequence() const noexcept;
  [[nodiscard]] MatchId last_match_id() const noexcept;
  [[nodiscard]] std::size_t pending_execution_report_count() const noexcept;
  [[nodiscard]] std::size_t execution_outbox_capacity() const noexcept;
  [[nodiscard]] std::size_t available_execution_outbox_capacity()
      const noexcept;
  [[nodiscard]] bool try_consume_execution_report(
      ExecutionReport& report) noexcept;
  [[nodiscard]] std::size_t pending_status_event_count() const noexcept;
  [[nodiscard]] std::size_t status_outbox_capacity() const noexcept;
  [[nodiscard]] std::size_t available_status_outbox_capacity() const noexcept;
  [[nodiscard]] bool try_consume_status(SystemStatus& status) noexcept;
  [[nodiscard]] std::size_t active_order_count() const noexcept;
  [[nodiscard]] std::size_t price_level_count(Side side) const noexcept;
  [[nodiscard]] std::optional<RestingOrderView> find_order(
      OrderId order_id) const noexcept;
  [[nodiscard]] std::optional<PriceTicks> best_bid() const noexcept;
  [[nodiscard]] std::optional<PriceTicks> best_ask() const noexcept;
  [[nodiscard]] std::optional<DepthEntry> level(
      Side side, PriceTicks price) const noexcept;
  [[nodiscard]] std::vector<DepthEntry> depth(Side side) const;
  [[nodiscard]] std::vector<RestingOrderView> orders_at_level(
      Side side, PriceTicks price) const;
  [[nodiscard]] bool validate_invariants() const noexcept;

 private:
  // Milestone 7 uses one narrow test fixture to construct near-exhausted
  // counters without adding public sequence mutation to the engine contract.
  friend struct MatchingEngineTestAccess;

  struct LogicalOrder final {
    OrderId order_id{};
    InstrumentId instrument_id{};
    Side side{Side::Invalid};
    PriceTicks limit_price{};
    Quantity quantity{};
  };

  struct PlannedFill final {
    OrderId resting_order_id{};
    PriceTicks resting_price{};
    Quantity match_quantity{};
  };

  struct MatchPlan final {
    std::array<PlannedFill, kMaximumFillsPerCommand> fills{};
    std::size_t fill_count{};
    std::size_t fully_filled_resting_orders{};
    Quantity aggressive_remainder{};
    bool rest_remainder{};
  };

  struct PlanResult final {
    OrderBookResult result{OrderBookResult::Accepted};
    MatchPlan plan{};
  };

  [[nodiscard]] OrderBookResult validate_new_order(
      const NewOrder& order) const noexcept;
  [[nodiscard]] OrderBookResult validate_cancel_order(
      const CancelOrder& order) const noexcept;
  [[nodiscard]] OrderBookResult validate_amend_order(
      const AmendOrder& order,
      const std::optional<RestingOrderView>& resting) const noexcept;
  [[nodiscard]] PlanResult plan_order(
      const LogicalOrder& order) const noexcept;
  [[nodiscard]] OrderBookResult preflight_plan(
      const LogicalOrder& order, const MatchPlan& plan,
      const std::optional<RestingOrderView>& replaced_order) const noexcept;
  void execute_plan(const LogicalOrder& order, const MatchPlan& plan,
                    const std::optional<OrderId>& replaced_order_id);
  void commit_reports(
      const LogicalOrder& order, const MatchPlan& plan,
      LosslessOutbox<ExecutionReport>::Reservation& reservation,
      NewOrderResult& result) noexcept;
  [[nodiscard]] LifecycleCommandResult reject_lifecycle(
      OrderBookResult result) const noexcept;
  [[nodiscard]] LifecycleCommandResult execute_lifecycle(
      InstrumentState resulting_state, StatusEventKind event_kind,
      StatusReason reason, bool reset_book,
      bool preserve_safety_headroom) noexcept;
  [[nodiscard]] OrderBookResult commit_lifecycle_transition(
      InstrumentState resulting_state, StatusEventKind event_kind,
      StatusReason reason, bool reset_book,
      bool preserve_safety_headroom) noexcept;
  void fail_closed_outbox_full() noexcept;
  [[nodiscard]] bool command_sequence_available() const noexcept;
  [[nodiscard]] CommandSequence accept_command() noexcept;

  OrderBookStorage storage_;
  LosslessOutbox<ExecutionReport> execution_outbox_;
  LosslessOutbox<SystemStatus> status_outbox_;
  SequenceState sequences_{};
  MatchId last_match_id_{};
  InstrumentState instrument_state_{InstrumentState::Active};
};

static_assert(kMaximumFillsPerCommand == 256);
static_assert(kDefaultExecutionOutboxCapacity >= kMaximumFillsPerCommand);
static_assert(kDefaultControlOutboxCapacity >= kMinimumStatusOutboxCapacity);
static_assert((kDefaultExecutionOutboxCapacity &
               (kDefaultExecutionOutboxCapacity - std::size_t{1})) == 0);
static_assert((kDefaultControlOutboxCapacity &
               (kDefaultControlOutboxCapacity - std::size_t{1})) == 0);

}  // namespace lob
