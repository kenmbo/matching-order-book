#pragma once

#include "lob/domain/contracts.hpp"
#include "lob/domain/sequencing.hpp"
#include "lob/storage/order_book_storage.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace lob {

inline constexpr std::size_t kMaximumFillsPerCommand = std::size_t{1} << 8;

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

class MatchingEngine final {
 public:
  explicit MatchingEngine(InstrumentId instrument_id,
                          StorageLimits limits = {});

  MatchingEngine(const MatchingEngine&) = delete;
  MatchingEngine& operator=(const MatchingEngine&) = delete;
  MatchingEngine(MatchingEngine&&) = delete;
  MatchingEngine& operator=(MatchingEngine&&) = delete;
  ~MatchingEngine() = default;

  [[nodiscard]] NewOrderResult process(const NewOrder& order);
  [[nodiscard]] CancelOrderResult process(const CancelOrder& order);
  [[nodiscard]] AmendOrderResult process(const AmendOrder& order);

  [[nodiscard]] InstrumentId instrument_id() const noexcept;
  [[nodiscard]] CommandSequence last_command_sequence() const noexcept;
  [[nodiscard]] EngineSequence last_engine_sequence() const noexcept;
  [[nodiscard]] MatchId last_match_id() const noexcept;
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
  void materialize_reports(const LogicalOrder& order, const MatchPlan& plan,
                           NewOrderResult& result) noexcept;
  [[nodiscard]] bool command_sequence_available() const noexcept;
  [[nodiscard]] CommandSequence accept_command();

  OrderBookStorage storage_;
  SequenceState sequences_{};
  MatchId last_match_id_{};
};

static_assert(kMaximumFillsPerCommand == 256);

}  // namespace lob
