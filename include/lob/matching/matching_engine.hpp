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
  [[nodiscard]] PlanResult plan_new_order(const NewOrder& order) const noexcept;
  [[nodiscard]] OrderBookResult preflight_plan(
      const NewOrder& order, const MatchPlan& plan) const noexcept;
  void execute_plan(const NewOrder& order, const MatchPlan& plan);
  void materialize_reports(const NewOrder& order, const MatchPlan& plan,
                           NewOrderResult& result) noexcept;

  OrderBookStorage storage_;
  SequenceState sequences_{};
  MatchId last_match_id_{};
};

static_assert(kMaximumFillsPerCommand == 256);

}  // namespace lob
