#include "lob/matching/matching_engine.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace lob {

namespace {

[[nodiscard]] constexpr Side opposite_side(Side side) noexcept {
  return side == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr bool crosses(Side aggressive_side,
                                     PriceTicks aggressive_limit,
                                     PriceTicks resting_price) noexcept {
  if (aggressive_side == Side::Buy) {
    return aggressive_limit >= resting_price;
  }
  return aggressive_limit <= resting_price;
}

[[noreturn]] void fail_plan_execution() noexcept {
  std::abort();
}

}  // namespace

MatchingEngine::MatchingEngine(InstrumentId instrument_id, StorageLimits limits)
    : storage_(instrument_id, limits) {}

NewOrderResult MatchingEngine::process(const NewOrder& order) {
  NewOrderResult result;
  result.result = validate_new_order(order);
  if (result.result != OrderBookResult::Accepted) {
    sequences_.reject_before_acceptance();
    return result;
  }

  if (sequences_.last_command().value() ==
      CommandSequence::maximum_value) {
    result.result = OrderBookResult::CapacityExhausted;
    sequences_.reject_before_acceptance();
    return result;
  }

  const auto command_assignment = sequences_.accept_command();
  if (!command_assignment.assigned()) {
    fail_plan_execution();
  }
  result.command_sequence = command_assignment.sequence;

  const auto planned = plan_new_order(order);
  if (planned.result != OrderBookResult::Accepted) {
    result.result = planned.result;
    sequences_.abort_event_batch();
    return result;
  }

  result.result = preflight_plan(order, planned.plan);
  if (result.result != OrderBookResult::Accepted) {
    sequences_.abort_event_batch();
    return result;
  }

  execute_plan(order, planned.plan);
  materialize_reports(order, planned.plan, result);
  result.result = OrderBookResult::Accepted;

#ifndef NDEBUG
  assert(validate_invariants());
#endif
  return result;
}

OrderBookResult MatchingEngine::validate_new_order(
    const NewOrder& order) const noexcept {
  if (!storage_.instrument_id().is_valid() ||
      order.instrument_id != storage_.instrument_id()) {
    return OrderBookResult::InvalidInstrument;
  }
  if (!order.order_id.is_valid() || storage_.find_order(order.order_id)) {
    return OrderBookResult::DuplicateOrderId;
  }
  if (order.side != Side::Buy && order.side != Side::Sell) {
    return OrderBookResult::InvalidSide;
  }
  if (!order.limit_price.is_valid()) {
    return OrderBookResult::InvalidPrice;
  }
  if (!order.quantity.is_valid()) {
    return OrderBookResult::InvalidQuantity;
  }
  return OrderBookResult::Accepted;
}

MatchingEngine::PlanResult MatchingEngine::plan_new_order(
    const NewOrder& order) const noexcept {
  PlanResult result;
  result.plan.aggressive_remainder = order.quantity;
  bool fill_capacity_exhausted = false;

  storage_.visit_orders_by_priority(
      opposite_side(order.side), [&](const RestingOrderView& resting) noexcept {
        if (!crosses(order.side, order.limit_price, resting.price)) {
          return false;
        }
        if (result.plan.fill_count == kMaximumFillsPerCommand) {
          fill_capacity_exhausted = true;
          return false;
        }

        const Quantity fill_quantity =
            result.plan.aggressive_remainder < resting.leaves_quantity
                ? result.plan.aggressive_remainder
                : resting.leaves_quantity;
        if (!fill_quantity.is_valid()) {
          fill_capacity_exhausted = true;
          return false;
        }

        result.plan.fills[result.plan.fill_count] =
            {resting.order_id, resting.price, fill_quantity};
        ++result.plan.fill_count;
        if (fill_quantity == resting.leaves_quantity) {
          ++result.plan.fully_filled_resting_orders;
        }

        const auto remainder =
            checked_subtract(result.plan.aggressive_remainder, fill_quantity);
        if (remainder.result == QuantityArithmeticResult::Zero) {
          result.plan.aggressive_remainder = {};
          return false;
        }
        if (!remainder.has_value()) {
          fill_capacity_exhausted = true;
          return false;
        }
        result.plan.aggressive_remainder = remainder.value;
        return true;
      });

  if (fill_capacity_exhausted) {
    result.result = OrderBookResult::CapacityExhausted;
    return result;
  }
  result.plan.rest_remainder = result.plan.aggressive_remainder.is_valid();
  return result;
}

OrderBookResult MatchingEngine::preflight_plan(
    const NewOrder& order, const MatchPlan& plan) const noexcept {
  if (plan.fully_filled_resting_orders > storage_.active_order_count()) {
    return OrderBookResult::CapacityExhausted;
  }
  const auto final_active_orders =
      storage_.active_order_count() - plan.fully_filled_resting_orders +
      (plan.rest_remainder ? std::size_t{1} : std::size_t{0});
  if (final_active_orders > storage_.active_order_capacity()) {
    return OrderBookResult::CapacityExhausted;
  }

  if (plan.rest_remainder) {
    const auto resting_level = storage_.level(order.side, order.limit_price);
    if (!resting_level) {
      if (storage_.price_level_count(order.side) >=
          storage_.price_level_capacity_per_side()) {
        return OrderBookResult::CapacityExhausted;
      }
    } else if (!checked_add(resting_level->aggregate_leaves_quantity,
                            plan.aggressive_remainder)
                    .has_value()) {
      return OrderBookResult::CapacityExhausted;
    }
  }

  const auto fill_count = static_cast<std::uint64_t>(plan.fill_count);
  if (fill_count >
          EngineSequence::maximum_value - sequences_.last_engine().value() ||
      fill_count > MatchId::maximum_value - last_match_id_.value()) {
    return OrderBookResult::CapacityExhausted;
  }
  return OrderBookResult::Accepted;
}

void MatchingEngine::execute_plan(const NewOrder& order,
                                  const MatchPlan& plan) {
  for (std::size_t index = 0; index < plan.fill_count; ++index) {
    const auto& fill = plan.fills[index];
    const auto resting = storage_.find_order(fill.resting_order_id);
    if (!resting || resting->side != opposite_side(order.side) ||
        resting->price != fill.resting_price ||
        resting->leaves_quantity < fill.match_quantity) {
      fail_plan_execution();
    }
    if (storage_.reduce_resting_by(fill.resting_order_id,
                                   fill.match_quantity) !=
        OrderBookResult::Accepted) {
      fail_plan_execution();
    }
  }

  if (plan.rest_remainder &&
      storage_.insert_resting(order.order_id, order.instrument_id, order.side,
                              order.limit_price,
                              plan.aggressive_remainder) !=
          OrderBookResult::Accepted) {
    fail_plan_execution();
  }
}

void MatchingEngine::materialize_reports(const NewOrder& order,
                                         const MatchPlan& plan,
                                         NewOrderResult& result) noexcept {
  const auto sequences = sequences_.commit_event_batch(
      static_cast<std::uint64_t>(plan.fill_count));
  if (!sequences.assigned()) {
    fail_plan_execution();
  }

  result.execution_report_count = plan.fill_count;
  for (std::size_t index = 0; index < plan.fill_count; ++index) {
    const auto match_id_value = last_match_id_.value() + std::uint64_t{1};
    const auto match_id = checked_domain_cast<MatchId>(match_id_value);
    const auto engine_sequence = checked_domain_cast<EngineSequence>(
        sequences.first.value() + static_cast<std::uint64_t>(index));
    if (!match_id.has_value() || !engine_sequence.has_value()) {
      fail_plan_execution();
    }
    last_match_id_ = match_id.value;

    const auto& fill = plan.fills[index];
    result.execution_reports[index] = {
        last_match_id_,       order.instrument_id, order.order_id,
        fill.resting_order_id, fill.resting_price,  fill.match_quantity,
        engine_sequence.value};
  }
}

InstrumentId MatchingEngine::instrument_id() const noexcept {
  return storage_.instrument_id();
}

CommandSequence MatchingEngine::last_command_sequence() const noexcept {
  return sequences_.last_command();
}

EngineSequence MatchingEngine::last_engine_sequence() const noexcept {
  return sequences_.last_engine();
}

MatchId MatchingEngine::last_match_id() const noexcept {
  return last_match_id_;
}

std::size_t MatchingEngine::active_order_count() const noexcept {
  return storage_.active_order_count();
}

std::size_t MatchingEngine::price_level_count(Side side) const noexcept {
  return storage_.price_level_count(side);
}

std::optional<RestingOrderView> MatchingEngine::find_order(
    OrderId order_id) const noexcept {
  return storage_.find_order(order_id);
}

std::optional<PriceTicks> MatchingEngine::best_bid() const noexcept {
  return storage_.best_bid();
}

std::optional<PriceTicks> MatchingEngine::best_ask() const noexcept {
  return storage_.best_ask();
}

std::optional<DepthEntry> MatchingEngine::level(
    Side side, PriceTicks price) const noexcept {
  return storage_.level(side, price);
}

std::vector<DepthEntry> MatchingEngine::depth(Side side) const {
  return storage_.depth(side);
}

std::vector<RestingOrderView> MatchingEngine::orders_at_level(
    Side side, PriceTicks price) const {
  return storage_.orders_at_level(side, price);
}

bool MatchingEngine::validate_invariants() const noexcept {
  if (!storage_.validate_invariants()) {
    return false;
  }
  const auto bid = storage_.best_bid();
  const auto ask = storage_.best_ask();
  return !bid || !ask || *bid < *ask;
}

}  // namespace lob
