#pragma once

#include "lob/matching/matching_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace phase1_test {

enum class TraceCommandKind : std::uint8_t {
  New,
  Cancel,
  Amend,
  Halt,
  Resume,
  Close,
  Open,
};

struct TraceCommand final {
  TraceCommandKind kind{TraceCommandKind::New};
  lob::OrderId order_id{};
  lob::InstrumentId instrument_id{};
  lob::Side side{lob::Side::Invalid};
  lob::PriceTicks price{};
  lob::Quantity quantity{};

  constexpr bool operator==(const TraceCommand&) const noexcept = default;
};

struct StepResult final {
  lob::OrderBookResult result{lob::OrderBookResult::CapacityExhausted};
  lob::CommandSequence command_sequence{};
  std::vector<lob::ExecutionReport> synchronous_reports{};
};

struct ObservableState final {
  lob::InstrumentState instrument_state{lob::InstrumentState::Invalid};
  lob::CommandSequence last_command{};
  lob::EngineSequence last_engine{};
  lob::MatchId last_match{};
  std::size_t active_order_count{};
  std::size_t bid_level_count{};
  std::size_t ask_level_count{};
  std::optional<lob::PriceTicks> best_bid{};
  std::optional<lob::PriceTicks> best_ask{};
  std::vector<lob::DepthEntry> bids{};
  std::vector<lob::DepthEntry> asks{};
  std::vector<lob::RestingOrderView> bid_orders{};
  std::vector<lob::RestingOrderView> ask_orders{};

  constexpr bool operator==(const ObservableState&) const noexcept = default;
};

[[nodiscard]] std::string serialize_command(const TraceCommand& command);
[[nodiscard]] std::optional<TraceCommand> parse_command(
    std::string_view line);
[[nodiscard]] std::string serialize_trace(
    const std::vector<TraceCommand>& trace);
[[nodiscard]] std::optional<std::vector<TraceCommand>> parse_trace(
    std::string_view text);
[[nodiscard]] std::optional<std::vector<TraceCommand>> load_trace(
    const std::string& path);

class ReferenceModel final {
 public:
  explicit ReferenceModel(lob::InstrumentId instrument_id,
                          lob::StorageLimits storage_limits = {},
                          lob::LosslessOutboxLimits outbox_limits = {});

  [[nodiscard]] StepResult apply(const TraceCommand& command);
  [[nodiscard]] ObservableState observable_state() const;
  [[nodiscard]] std::vector<lob::ExecutionReport> drain_execution_reports();
  [[nodiscard]] std::vector<lob::SystemStatus> drain_statuses();
  [[nodiscard]] const std::vector<lob::ExecutionReport>&
  pending_execution_reports() const noexcept;
  [[nodiscard]] const std::vector<lob::SystemStatus>& pending_statuses()
      const noexcept;
  [[nodiscard]] std::vector<lob::RestingOrderView> active_orders() const;
  [[nodiscard]] bool validate_invariants() const noexcept;

 public:
  // Deliberately simple test-only representation. Exposing it within the test
  // binary keeps the model transparent without exposing production storage.
  struct ModelOrder final {
    lob::OrderId order_id{};
    lob::Side side{lob::Side::Invalid};
    lob::PriceTicks price{};
    lob::Quantity leaves{};
  };

  using Level = std::vector<ModelOrder>;
  using BidLevels =
      std::map<std::int64_t, Level, std::greater<std::int64_t>>;
  using AskLevels = std::map<std::int64_t, Level, std::less<std::int64_t>>;

 private:

  struct LocatedOrder final {
    lob::Side side{lob::Side::Invalid};
    std::int64_t price{};
    std::size_t index{};
  };

  struct PlannedReport final {
    lob::OrderId resting_order_id{};
    lob::PriceTicks resting_price{};
    lob::Quantity match_quantity{};
  };

  [[nodiscard]] StepResult apply_new(const TraceCommand& command);
  [[nodiscard]] StepResult apply_cancel(const TraceCommand& command);
  [[nodiscard]] StepResult apply_amend(const TraceCommand& command);
  [[nodiscard]] StepResult apply_lifecycle(const TraceCommand& command);
  [[nodiscard]] StepResult execute_aggressive(
      const TraceCommand& command, lob::Side side,
      std::optional<lob::OrderId> replaced_order);
  [[nodiscard]] std::optional<LocatedOrder> locate(
      lob::OrderId order_id) const noexcept;
  [[nodiscard]] const ModelOrder* find(lob::OrderId order_id) const noexcept;
  [[nodiscard]] ModelOrder* find(lob::OrderId order_id) noexcept;
  [[nodiscard]] lob::OrderBookResult insert(ModelOrder order);
  [[nodiscard]] bool erase(lob::OrderId order_id) noexcept;
  [[nodiscard]] std::size_t active_order_count() const noexcept;
  [[nodiscard]] lob::CommandSequence accept_command() noexcept;
  void fail_closed();
  void append_status(lob::InstrumentState resulting_state,
                     lob::StatusEventKind event_kind,
                     lob::StatusReason reason);

  lob::InstrumentId instrument_id_{};
  std::size_t active_order_capacity_{};
  std::size_t price_level_capacity_{};
  std::size_t execution_outbox_capacity_{};
  std::size_t status_outbox_capacity_{};
  lob::InstrumentState instrument_state_{lob::InstrumentState::Active};
  std::uint64_t last_command_{};
  std::uint64_t last_engine_{};
  std::uint64_t last_match_{};
  BidLevels bids_{};
  AskLevels asks_{};
  std::vector<lob::ExecutionReport> execution_outbox_{};
  std::vector<lob::SystemStatus> status_outbox_{};
};

[[nodiscard]] bool execution_report_equal(
    const lob::ExecutionReport& left,
    const lob::ExecutionReport& right) noexcept;
[[nodiscard]] bool system_status_equal(const lob::SystemStatus& left,
                                       const lob::SystemStatus& right) noexcept;
[[nodiscard]] std::string describe_state(const ObservableState& state);

}  // namespace phase1_test
