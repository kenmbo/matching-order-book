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

[[nodiscard]] std::size_t validate_status_outbox_capacity(
    std::size_t capacity) noexcept {
  if (capacity < kMinimumStatusOutboxCapacity) {
    std::abort();
  }
  return capacity;
}

}  // namespace

MatchingEngine::MatchingEngine(InstrumentId instrument_id,
                               StorageLimits storage_limits,
                               LosslessOutboxLimits outbox_limits)
    : storage_(instrument_id, storage_limits),
      execution_outbox_(outbox_limits.execution_reports),
      status_outbox_(
          validate_status_outbox_capacity(outbox_limits.status_events)) {}

NewOrderResult MatchingEngine::process(const NewOrder& order) noexcept {
  NewOrderResult result;
  result.result = validate_new_order(order);
  if (result.result != OrderBookResult::Accepted) {
    sequences_.reject_before_acceptance();
    return result;
  }

  if (!command_sequence_available()) {
    result.result = OrderBookResult::CapacityExhausted;
    sequences_.reject_before_acceptance();
    return result;
  }

  result.command_sequence = accept_command();
  const LogicalOrder logical_order{order.order_id, order.instrument_id,
                                   order.side, order.limit_price,
                                   order.quantity};

  const auto planned = plan_order(logical_order);
  if (planned.result != OrderBookResult::Accepted) {
    result.result = planned.result;
    sequences_.abort_event_batch();
    return result;
  }

  result.result = preflight_plan(logical_order, planned.plan, std::nullopt);
  if (result.result != OrderBookResult::Accepted) {
    sequences_.abort_event_batch();
    return result;
  }

  auto reservation = execution_outbox_.reserve(planned.plan.fill_count);
  if (!reservation.valid()) {
    result.result = OrderBookResult::LosslessOutboxFull;
    sequences_.abort_event_batch();
    fail_closed_outbox_full();
#ifndef NDEBUG
    assert(validate_invariants());
#endif
    return result;
  }

  execute_plan(logical_order, planned.plan, std::nullopt);
  commit_reports(logical_order, planned.plan, reservation, result);
  result.result = OrderBookResult::Accepted;

#ifndef NDEBUG
  assert(validate_invariants());
#endif
  return result;
}

CancelOrderResult MatchingEngine::process(const CancelOrder& order) noexcept {
  CancelOrderResult result;
  result.result = validate_cancel_order(order);
  if (result.result != OrderBookResult::Accepted) {
    sequences_.reject_before_acceptance();
    return result;
  }
  if (!command_sequence_available()) {
    result.result = OrderBookResult::CapacityExhausted;
    sequences_.reject_before_acceptance();
    return result;
  }

  result.command_sequence = accept_command();
  if (storage_.remove_resting(order.order_id) != OrderBookResult::Accepted) {
    fail_plan_execution();
  }
  const auto event_batch = sequences_.commit_event_batch(0);
  if (!event_batch.empty()) {
    fail_plan_execution();
  }
  result.result = OrderBookResult::Accepted;

#ifndef NDEBUG
  assert(validate_invariants());
#endif
  return result;
}

AmendOrderResult MatchingEngine::process(const AmendOrder& order) noexcept {
  AmendOrderResult result;
  const auto resting = storage_.find_order(order.order_id);
  result.result = validate_amend_order(order, resting);
  if (result.result != OrderBookResult::Accepted) {
    sequences_.reject_before_acceptance();
    return result;
  }
  if (!command_sequence_available()) {
    result.result = OrderBookResult::CapacityExhausted;
    sequences_.reject_before_acceptance();
    return result;
  }

  result.command_sequence = accept_command();
  if (order.new_price == resting->price) {
    if (order.new_leaves_quantity != resting->leaves_quantity) {
      result.result = storage_.update_resting_leaves(
          order.order_id, order.new_leaves_quantity);
      if (result.result == OrderBookResult::CapacityExhausted) {
        sequences_.abort_event_batch();
        return result;
      }
      if (result.result != OrderBookResult::Accepted) {
        fail_plan_execution();
      }

      if (order.new_leaves_quantity > resting->leaves_quantity &&
          storage_.move_resting_to_back(order.order_id) !=
              OrderBookResult::Accepted) {
        fail_plan_execution();
      }
    }

    const auto event_batch = sequences_.commit_event_batch(0);
    if (!event_batch.empty()) {
      fail_plan_execution();
    }
    result.result = OrderBookResult::Accepted;

#ifndef NDEBUG
    assert(validate_invariants());
#endif
    return result;
  }

  const LogicalOrder logical_order{order.order_id, order.instrument_id,
                                   resting->side, order.new_price,
                                   order.new_leaves_quantity};
  const auto planned = plan_order(logical_order);
  if (planned.result != OrderBookResult::Accepted) {
    result.result = planned.result;
    sequences_.abort_event_batch();
    return result;
  }

  result.result = preflight_plan(logical_order, planned.plan, resting);
  if (result.result != OrderBookResult::Accepted) {
    sequences_.abort_event_batch();
    return result;
  }

  auto reservation = execution_outbox_.reserve(planned.plan.fill_count);
  if (!reservation.valid()) {
    result.result = OrderBookResult::LosslessOutboxFull;
    sequences_.abort_event_batch();
    fail_closed_outbox_full();
#ifndef NDEBUG
    assert(validate_invariants());
#endif
    return result;
  }

  execute_plan(logical_order, planned.plan, order.order_id);
  commit_reports(logical_order, planned.plan, reservation, result);
  result.result = OrderBookResult::Accepted;

#ifndef NDEBUG
  assert(validate_invariants());
#endif
  return result;
}

LifecycleCommandResult MatchingEngine::process(
    const HaltInstrument& command) noexcept {
  if (!storage_.instrument_id().is_valid() ||
      command.instrument_id != storage_.instrument_id()) {
    return reject_lifecycle(OrderBookResult::InvalidInstrument);
  }
  if (instrument_state_ != InstrumentState::Active) {
    return reject_lifecycle(OrderBookResult::InvalidStateTransition);
  }
  return execute_lifecycle(InstrumentState::Halted,
                           StatusEventKind::StateTransition,
                           StatusReason::TradingHalt, false, false);
}

LifecycleCommandResult MatchingEngine::process(
    const ResumeInstrument& command) noexcept {
  if (!storage_.instrument_id().is_valid() ||
      command.instrument_id != storage_.instrument_id()) {
    return reject_lifecycle(OrderBookResult::InvalidInstrument);
  }
  if (instrument_state_ != InstrumentState::Halted) {
    return reject_lifecycle(OrderBookResult::InvalidStateTransition);
  }
  return execute_lifecycle(InstrumentState::Active,
                           StatusEventKind::StateTransition,
                           StatusReason::TradingResume, false, true);
}

LifecycleCommandResult MatchingEngine::process(
    const CloseInstrument& command) noexcept {
  if (!storage_.instrument_id().is_valid() ||
      command.instrument_id != storage_.instrument_id()) {
    return reject_lifecycle(OrderBookResult::InvalidInstrument);
  }
  if (instrument_state_ != InstrumentState::Active &&
      instrument_state_ != InstrumentState::Halted) {
    return reject_lifecycle(OrderBookResult::InvalidStateTransition);
  }
  return execute_lifecycle(InstrumentState::Closed, StatusEventKind::Reset,
                           StatusReason::EndOfDay, true, false);
}

LifecycleCommandResult MatchingEngine::process(
    const OpenInstrument& command) noexcept {
  if (!storage_.instrument_id().is_valid() ||
      command.instrument_id != storage_.instrument_id()) {
    return reject_lifecycle(OrderBookResult::InvalidInstrument);
  }
  if (instrument_state_ != InstrumentState::Closed) {
    return reject_lifecycle(OrderBookResult::InvalidStateTransition);
  }
  return execute_lifecycle(InstrumentState::Active,
                           StatusEventKind::StateTransition,
                           StatusReason::SessionOpen, false, true);
}

OrderBookResult MatchingEngine::validate_new_order(
    const NewOrder& order) const noexcept {
  if (!storage_.instrument_id().is_valid() ||
      order.instrument_id != storage_.instrument_id()) {
    return OrderBookResult::InvalidInstrument;
  }
  if (instrument_state_ == InstrumentState::Halted) {
    return OrderBookResult::MarketHalted;
  }
  if (instrument_state_ != InstrumentState::Active) {
    return OrderBookResult::InstrumentUnavailable;
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

OrderBookResult MatchingEngine::validate_cancel_order(
    const CancelOrder& order) const noexcept {
  if (!storage_.instrument_id().is_valid() ||
      order.instrument_id != storage_.instrument_id()) {
    return OrderBookResult::InvalidInstrument;
  }
  if (instrument_state_ == InstrumentState::Closed) {
    return OrderBookResult::InstrumentUnavailable;
  }
  if (instrument_state_ != InstrumentState::Active &&
      instrument_state_ != InstrumentState::Halted) {
    return OrderBookResult::InstrumentUnavailable;
  }
  if (!order.order_id.is_valid() || !storage_.find_order(order.order_id)) {
    return OrderBookResult::OrderNotFound;
  }
  return OrderBookResult::Accepted;
}

OrderBookResult MatchingEngine::validate_amend_order(
    const AmendOrder& order,
    const std::optional<RestingOrderView>& resting) const noexcept {
  if (!storage_.instrument_id().is_valid() ||
      order.instrument_id != storage_.instrument_id()) {
    return OrderBookResult::InvalidInstrument;
  }
  if (instrument_state_ == InstrumentState::Halted) {
    return OrderBookResult::MarketHalted;
  }
  if (instrument_state_ != InstrumentState::Active) {
    return OrderBookResult::InstrumentUnavailable;
  }
  if (!order.order_id.is_valid() || !resting) {
    return OrderBookResult::OrderNotFound;
  }
  if (!order.new_price.is_valid()) {
    return OrderBookResult::InvalidPrice;
  }
  if (!order.new_leaves_quantity.is_valid()) {
    return OrderBookResult::InvalidAmendment;
  }
  return OrderBookResult::Accepted;
}

MatchingEngine::PlanResult MatchingEngine::plan_order(
    const LogicalOrder& order) const noexcept {
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
    const LogicalOrder& order, const MatchPlan& plan,
    const std::optional<RestingOrderView>& replaced_order) const noexcept {
  const auto replaced_order_count =
      replaced_order ? std::size_t{1} : std::size_t{0};
  if (plan.fully_filled_resting_orders + replaced_order_count >
      storage_.active_order_count()) {
    return OrderBookResult::CapacityExhausted;
  }
  const auto final_active_orders =
      storage_.active_order_count() - plan.fully_filled_resting_orders -
      replaced_order_count +
      (plan.rest_remainder ? std::size_t{1} : std::size_t{0});
  if (final_active_orders > storage_.active_order_capacity()) {
    return OrderBookResult::CapacityExhausted;
  }

  if (plan.rest_remainder) {
    std::optional<OrderId> first_removed_order;
    if (replaced_order) {
      first_removed_order = replaced_order->order_id;
    } else {
      for (std::size_t index = 0; index < plan.fill_count; ++index) {
        const auto& fill = plan.fills[index];
        const auto resting = storage_.find_order(fill.resting_order_id);
        if (!resting) {
          return OrderBookResult::CapacityExhausted;
        }
        if (resting->leaves_quantity == fill.match_quantity) {
          first_removed_order = fill.resting_order_id;
          break;
        }
      }
    }
    if (!storage_.can_insert_after_removing(first_removed_order)) {
      return OrderBookResult::CapacityExhausted;
    }
  }

  if (plan.rest_remainder) {
    auto final_level_count = storage_.price_level_count(order.side);
    bool old_level_removed = false;
    if (replaced_order) {
      const auto old_level =
          storage_.level(replaced_order->side, replaced_order->price);
      if (!old_level || old_level->order_count == 0 ||
          replaced_order->side != order.side) {
        return OrderBookResult::CapacityExhausted;
      }
      if (old_level->order_count == 1) {
        if (final_level_count == 0) {
          return OrderBookResult::CapacityExhausted;
        }
        --final_level_count;
        old_level_removed = true;
      }
    }

    const auto destination_level =
        storage_.level(order.side, order.limit_price);
    const bool replacement_is_destination =
        replaced_order && replaced_order->side == order.side &&
        replaced_order->price == order.limit_price;
    const bool destination_exists_after_removal =
        destination_level && !(replacement_is_destination && old_level_removed);

    if (!destination_exists_after_removal) {
      ++final_level_count;
    }
    if (final_level_count > storage_.price_level_capacity_per_side()) {
      return OrderBookResult::CapacityExhausted;
    }

    if (destination_exists_after_removal) {
      Quantity destination_aggregate =
          destination_level->aggregate_leaves_quantity;
      if (replacement_is_destination) {
        const auto without_replaced = checked_subtract(
            destination_aggregate, replaced_order->leaves_quantity);
        if (without_replaced.result == QuantityArithmeticResult::Zero) {
          destination_aggregate = {};
        } else if (without_replaced.has_value()) {
          destination_aggregate = without_replaced.value;
        } else {
          return OrderBookResult::CapacityExhausted;
        }
      }
      if (destination_aggregate.is_valid() &&
          !checked_add(destination_aggregate, plan.aggressive_remainder)
               .has_value()) {
        return OrderBookResult::CapacityExhausted;
      }
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

void MatchingEngine::execute_plan(
    const LogicalOrder& order, const MatchPlan& plan,
    const std::optional<OrderId>& replaced_order_id) {
  if (replaced_order_id &&
      storage_.remove_resting(*replaced_order_id) !=
          OrderBookResult::Accepted) {
    fail_plan_execution();
  }

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

void MatchingEngine::commit_reports(
    const LogicalOrder& order, const MatchPlan& plan,
    LosslessOutbox<ExecutionReport>::Reservation& reservation,
    NewOrderResult& result) noexcept {
  result.execution_report_count = plan.fill_count;
  for (std::size_t index = 0; index < plan.fill_count; ++index) {
    const auto offset = static_cast<std::uint64_t>(index) + std::uint64_t{1};
    const auto match_id_value = last_match_id_.value() + offset;
    const auto match_id = checked_domain_cast<MatchId>(match_id_value);
    const auto engine_sequence = checked_domain_cast<EngineSequence>(
        sequences_.last_engine().value() + offset);
    if (!match_id.has_value() || !engine_sequence.has_value()) {
      fail_plan_execution();
    }

    const auto& fill = plan.fills[index];
    const ExecutionReport report{
        match_id.value,       order.instrument_id, order.order_id,
        fill.resting_order_id, fill.resting_price,  fill.match_quantity,
        engine_sequence.value};
    result.execution_reports[index] = report;
    if (!reservation.write(report)) {
      fail_plan_execution();
    }
  }

  const auto event_batch = sequences_.commit_event_batch(
      static_cast<std::uint64_t>(plan.fill_count));
  if (!event_batch.assigned()) {
    fail_plan_execution();
  }
  if (plan.fill_count == 0) {
    if (!event_batch.empty()) {
      fail_plan_execution();
    }
  } else {
    const auto last_match_id = checked_domain_cast<MatchId>(
        last_match_id_.value() + static_cast<std::uint64_t>(plan.fill_count));
    if (!last_match_id.has_value() ||
        event_batch.first != result.execution_reports[0].engine_sequence ||
        event_batch.last !=
            result.execution_reports[plan.fill_count - 1].engine_sequence) {
      fail_plan_execution();
    }
    last_match_id_ = last_match_id.value;
  }

  if (!reservation.commit()) {
    fail_plan_execution();
  }
}

LifecycleCommandResult MatchingEngine::reject_lifecycle(
    OrderBookResult result) const noexcept {
  return {result, {}};
}

LifecycleCommandResult MatchingEngine::execute_lifecycle(
    InstrumentState resulting_state, StatusEventKind event_kind,
    StatusReason reason, bool reset_book,
    bool preserve_safety_headroom) noexcept {
  if (!command_sequence_available()) {
    sequences_.reject_before_acceptance();
    return reject_lifecycle(OrderBookResult::CapacityExhausted);
  }

  LifecycleCommandResult result;
  result.command_sequence = accept_command();
  result.result = commit_lifecycle_transition(
      resulting_state, event_kind, reason, reset_book,
      preserve_safety_headroom);

#ifndef NDEBUG
  assert(validate_invariants());
#endif
  return result;
}

OrderBookResult MatchingEngine::commit_lifecycle_transition(
    InstrumentState resulting_state, StatusEventKind event_kind,
    StatusReason reason, bool reset_book,
    bool preserve_safety_headroom) noexcept {
  if (reset_book && !storage_.can_clear()) {
    sequences_.abort_event_batch();
    return OrderBookResult::CapacityExhausted;
  }
  const auto required_available = preserve_safety_headroom
                                      ? std::size_t{2}
                                      : std::size_t{1};
  if (status_outbox_.available_capacity() < required_available) {
    sequences_.abort_event_batch();
    return OrderBookResult::StatusOutboxFull;
  }
  if (sequences_.last_engine().value() == EngineSequence::maximum_value) {
    sequences_.abort_event_batch();
    return OrderBookResult::CapacityExhausted;
  }

  auto reservation = status_outbox_.reserve(1);
  const auto engine_sequence = checked_domain_cast<EngineSequence>(
      sequences_.last_engine().value() + std::uint64_t{1});
  if (!reservation.valid() || !engine_sequence.has_value()) {
    fail_plan_execution();
  }

  const SystemStatus status{StatusScope::Instrument,
                            storage_.instrument_id(),
                            instrument_state_,
                            resulting_state,
                            event_kind,
                            reason,
                            engine_sequence.value};
  if (!reservation.write(status)) {
    fail_plan_execution();
  }

  if (reset_book) {
    storage_.clear();
  }
  instrument_state_ = resulting_state;

  const auto event_batch = sequences_.commit_event_batch(1);
  if (!event_batch.assigned() || event_batch.first != engine_sequence.value ||
      event_batch.last != engine_sequence.value || !reservation.commit()) {
    fail_plan_execution();
  }
  return OrderBookResult::Accepted;
}

void MatchingEngine::fail_closed_outbox_full() noexcept {
  if (instrument_state_ != InstrumentState::Active) {
    fail_plan_execution();
  }
  if (commit_lifecycle_transition(InstrumentState::Halted,
                                  StatusEventKind::StateTransition,
                                  StatusReason::LosslessOutboxFull, false,
                                  false) != OrderBookResult::Accepted) {
    fail_plan_execution();
  }
}

bool MatchingEngine::command_sequence_available() const noexcept {
  return sequences_.last_command().value() < CommandSequence::maximum_value;
}

CommandSequence MatchingEngine::accept_command() noexcept {
  const auto assignment = sequences_.accept_command();
  if (!assignment.assigned()) {
    fail_plan_execution();
  }
  return assignment.sequence;
}

InstrumentId MatchingEngine::instrument_id() const noexcept {
  return storage_.instrument_id();
}

InstrumentState MatchingEngine::instrument_state() const noexcept {
  return instrument_state_;
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

std::size_t MatchingEngine::pending_execution_report_count() const noexcept {
  return execution_outbox_.size();
}

std::size_t MatchingEngine::execution_outbox_capacity() const noexcept {
  return execution_outbox_.capacity();
}

std::size_t MatchingEngine::available_execution_outbox_capacity()
    const noexcept {
  return execution_outbox_.available_capacity();
}

bool MatchingEngine::try_consume_execution_report(
    ExecutionReport& report) noexcept {
  return execution_outbox_.try_pop(report);
}

std::size_t MatchingEngine::pending_status_event_count() const noexcept {
  return status_outbox_.size();
}

std::size_t MatchingEngine::status_outbox_capacity() const noexcept {
  return status_outbox_.capacity();
}

std::size_t MatchingEngine::available_status_outbox_capacity() const noexcept {
  return status_outbox_.available_capacity();
}

bool MatchingEngine::try_consume_status(SystemStatus& status) noexcept {
  return status_outbox_.try_pop(status);
}

std::size_t MatchingEngine::active_order_count() const noexcept {
  return storage_.active_order_count();
}

std::size_t MatchingEngine::price_level_count(Side side) const noexcept {
  return storage_.price_level_count(side);
}

StorageDiagnostics MatchingEngine::storage_diagnostics() const noexcept {
  return storage_.diagnostics();
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
  if (!storage_.validate_invariants() ||
      !execution_outbox_.validate_invariants() ||
      !status_outbox_.validate_invariants() ||
      (instrument_state_ != InstrumentState::Active &&
       instrument_state_ != InstrumentState::Halted &&
       instrument_state_ != InstrumentState::Closed) ||
      (instrument_state_ == InstrumentState::Active &&
       status_outbox_.available_capacity() == 0) ||
      (instrument_state_ == InstrumentState::Closed &&
       storage_.active_order_count() != 0)) {
    return false;
  }
  const auto bid = storage_.best_bid();
  const auto ask = storage_.best_ask();
  return !bid || !ask || *bid < *ask;
}

}  // namespace lob
