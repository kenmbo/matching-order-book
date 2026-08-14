#include "lob/matching/matching_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using lob::AmendOrder;
using lob::CancelOrder;
using lob::CloseInstrument;
using lob::CommandSequence;
using lob::DepthEntry;
using lob::EngineSequence;
using lob::ExecutionReport;
using lob::HaltInstrument;
using lob::InstrumentId;
using lob::InstrumentState;
using lob::LifecycleCommandResult;
using lob::LosslessOutboxLimits;
using lob::MatchId;
using lob::MatchingEngine;
using lob::NewOrder;
using lob::OpenInstrument;
using lob::OrderBookResult;
using lob::OrderId;
using lob::PriceTicks;
using lob::Quantity;
using lob::RestingOrderView;
using lob::ResumeInstrument;
using lob::Side;
using lob::StatusEventKind;
using lob::StatusReason;
using lob::StatusScope;
using lob::SystemStatus;

static_assert(noexcept(std::declval<MatchingEngine&>().process(
    std::declval<const HaltInstrument&>())));
static_assert(noexcept(std::declval<MatchingEngine&>().process(
    std::declval<const ResumeInstrument&>())));
static_assert(noexcept(std::declval<MatchingEngine&>().process(
    std::declval<const CloseInstrument&>())));
static_assert(noexcept(std::declval<MatchingEngine&>().process(
    std::declval<const OpenInstrument&>())));

class Checks final {
 public:
  void require(bool condition) noexcept {
    if (!condition) {
      failed_ = true;
    }
  }

  [[nodiscard]] bool passed() const noexcept { return !failed_; }

 private:
  bool failed_{};
};

template <typename Domain, typename Source>
Domain make_domain(Checks& checks, Source source) noexcept {
  const auto converted = lob::checked_domain_cast<Domain>(source);
  checks.require(converted.has_value());
  return converted.value;
}

OrderId order_id(Checks& checks, std::uint64_t value) noexcept {
  return make_domain<OrderId>(checks, value);
}

InstrumentId instrument_id(Checks& checks, std::uint32_t value) noexcept {
  return make_domain<InstrumentId>(checks, value);
}

PriceTicks price(Checks& checks, std::int64_t value) noexcept {
  return make_domain<PriceTicks>(checks, value);
}

Quantity quantity(Checks& checks, std::uint64_t value) noexcept {
  return make_domain<Quantity>(checks, value);
}

lob::NewOrderResult submit(Checks& checks, MatchingEngine& engine, OrderId id,
                           Side side, PriceTicks limit, Quantity leaves) {
  const auto result = engine.process(
      NewOrder{id, engine.instrument_id(), side, limit, leaves});
  checks.require(engine.validate_invariants());
  return result;
}

lob::CancelOrderResult cancel(Checks& checks, MatchingEngine& engine,
                              OrderId id) {
  const auto result = engine.process(CancelOrder{id, engine.instrument_id()});
  checks.require(engine.validate_invariants());
  return result;
}

lob::AmendOrderResult amend(Checks& checks, MatchingEngine& engine, OrderId id,
                            PriceTicks new_price, Quantity new_leaves) {
  const auto result = engine.process(
      AmendOrder{id, engine.instrument_id(), new_price, new_leaves});
  checks.require(engine.validate_invariants());
  return result;
}

std::vector<OrderId> fifo_ids(const std::vector<RestingOrderView>& orders) {
  std::vector<OrderId> result;
  result.reserve(orders.size());
  for (const auto& order : orders) {
    result.push_back(order.order_id);
  }
  return result;
}

struct LogicalState final {
  std::size_t active_orders{};
  std::vector<DepthEntry> bids{};
  std::vector<DepthEntry> asks{};
  std::vector<RestingOrderView> bid_orders{};
  std::vector<RestingOrderView> ask_orders{};

  bool operator==(const LogicalState&) const noexcept = default;
};

LogicalState logical_state(const MatchingEngine& engine) {
  LogicalState state;
  state.active_orders = engine.active_order_count();
  state.bids = engine.depth(Side::Buy);
  state.asks = engine.depth(Side::Sell);
  for (const auto& level : state.bids) {
    const auto orders = engine.orders_at_level(Side::Buy, level.price);
    state.bid_orders.insert(state.bid_orders.end(), orders.begin(), orders.end());
  }
  for (const auto& level : state.asks) {
    const auto orders = engine.orders_at_level(Side::Sell, level.price);
    state.ask_orders.insert(state.ask_orders.end(), orders.begin(), orders.end());
  }
  return state;
}

std::vector<SystemStatus> drain_statuses(MatchingEngine& engine) {
  std::vector<SystemStatus> statuses;
  statuses.reserve(engine.pending_status_event_count());
  SystemStatus status;
  while (engine.try_consume_status(status)) {
    statuses.push_back(status);
  }
  return statuses;
}

std::vector<ExecutionReport> drain_reports(MatchingEngine& engine) {
  std::vector<ExecutionReport> reports;
  reports.reserve(engine.pending_execution_report_count());
  ExecutionReport report;
  while (engine.try_consume_execution_report(report)) {
    reports.push_back(report);
  }
  return reports;
}

bool status_equals(const SystemStatus& status, InstrumentId instrument,
                   InstrumentState previous, InstrumentState resulting,
                   StatusEventKind kind, StatusReason reason,
                   std::uint64_t sequence) noexcept {
  return status.scope == StatusScope::Instrument &&
         status.instrument_id == instrument &&
         status.previous_state == previous &&
         status.resulting_state == resulting && status.kind == kind &&
         status.reason == reason &&
         status.engine_sequence.value() == sequence;
}

void require_rejected_transition(Checks& checks, MatchingEngine& engine,
                                 LifecycleCommandResult result,
                                 InstrumentState expected_state,
                                 const LogicalState& expected_book) {
  checks.require(result.result == OrderBookResult::InvalidStateTransition);
  checks.require(!result.command_sequence.is_valid());
  checks.require(engine.instrument_state() == expected_state);
  checks.require(logical_state(engine) == expected_book);
  checks.require(engine.validate_invariants());
}

void test_valid_transition_matrix_and_statuses(Checks& checks) {
  const auto instrument = instrument_id(checks, 1);
  MatchingEngine engine(instrument);
  checks.require(engine.instrument_state() == InstrumentState::Active);
  checks.require(engine.status_outbox_capacity() >= 2);
  checks.require(engine.available_status_outbox_capacity() ==
                 engine.status_outbox_capacity());

  const auto halt = engine.process(HaltInstrument{instrument});
  const auto resume = engine.process(ResumeInstrument{instrument});
  const auto close_active = engine.process(CloseInstrument{instrument});
  const auto open = engine.process(OpenInstrument{instrument});
  const auto second_halt = engine.process(HaltInstrument{instrument});
  const auto close_halted = engine.process(CloseInstrument{instrument});

  for (const auto* result :
       {&halt, &resume, &close_active, &open, &second_halt, &close_halted}) {
    checks.require(result->result == OrderBookResult::Accepted);
  }
  checks.require(halt.command_sequence.value() == 1);
  checks.require(resume.command_sequence.value() == 2);
  checks.require(close_active.command_sequence.value() == 3);
  checks.require(open.command_sequence.value() == 4);
  checks.require(second_halt.command_sequence.value() == 5);
  checks.require(close_halted.command_sequence.value() == 6);
  checks.require(engine.instrument_state() == InstrumentState::Closed);
  checks.require(engine.last_command_sequence().value() == 6);
  checks.require(engine.last_engine_sequence().value() == 6);
  checks.require(engine.last_match_id().value() == 0);

  const auto statuses = drain_statuses(engine);
  checks.require(statuses.size() == 6);
  checks.require(status_equals(statuses[0], instrument, InstrumentState::Active,
                               InstrumentState::Halted,
                               StatusEventKind::StateTransition,
                               StatusReason::TradingHalt, 1));
  checks.require(status_equals(statuses[1], instrument, InstrumentState::Halted,
                               InstrumentState::Active,
                               StatusEventKind::StateTransition,
                               StatusReason::TradingResume, 2));
  checks.require(status_equals(statuses[2], instrument, InstrumentState::Active,
                               InstrumentState::Closed,
                               StatusEventKind::Reset,
                               StatusReason::EndOfDay, 3));
  checks.require(status_equals(statuses[3], instrument, InstrumentState::Closed,
                               InstrumentState::Active,
                               StatusEventKind::StateTransition,
                               StatusReason::SessionOpen, 4));
  checks.require(status_equals(statuses[4], instrument, InstrumentState::Active,
                               InstrumentState::Halted,
                               StatusEventKind::StateTransition,
                               StatusReason::TradingHalt, 5));
  checks.require(status_equals(statuses[5], instrument, InstrumentState::Halted,
                               InstrumentState::Closed,
                               StatusEventKind::Reset,
                               StatusReason::EndOfDay, 6));
}

void test_invalid_transition_matrix(Checks& checks) {
  const auto instrument = instrument_id(checks, 2);
  const auto other = instrument_id(checks, 3);

  MatchingEngine active(instrument);
  checks.require(submit(checks, active, order_id(checks, 1), Side::Buy,
                        price(checks, 90), quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  const auto active_book = logical_state(active);
  const auto active_command = active.last_command_sequence();
  const auto active_engine = active.last_engine_sequence();
  require_rejected_transition(checks, active,
                              active.process(ResumeInstrument{instrument}),
                              InstrumentState::Active, active_book);
  require_rejected_transition(checks, active,
                              active.process(OpenInstrument{instrument}),
                              InstrumentState::Active, active_book);
  checks.require(active.last_command_sequence() == active_command);
  checks.require(active.last_engine_sequence() == active_engine);
  checks.require(active.pending_status_event_count() == 0);

  const auto wrong = active.process(HaltInstrument{other});
  checks.require(wrong.result == OrderBookResult::InvalidInstrument);
  checks.require(!wrong.command_sequence.is_valid());
  checks.require(active.last_command_sequence() == active_command);
  checks.require(active.last_engine_sequence() == active_engine);
  checks.require(active.instrument_state() == InstrumentState::Active);
  checks.require(logical_state(active) == active_book);
  checks.require(active.pending_status_event_count() == 0);

  MatchingEngine halted(instrument);
  checks.require(submit(checks, halted, order_id(checks, 2), Side::Sell,
                        price(checks, 110), quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  checks.require(halted.process(HaltInstrument{instrument}).result ==
                 OrderBookResult::Accepted);
  const auto halted_book = logical_state(halted);
  const auto halted_command = halted.last_command_sequence();
  const auto halted_engine = halted.last_engine_sequence();
  const auto halted_statuses = halted.pending_status_event_count();
  require_rejected_transition(checks, halted,
                              halted.process(HaltInstrument{instrument}),
                              InstrumentState::Halted, halted_book);
  require_rejected_transition(checks, halted,
                              halted.process(OpenInstrument{instrument}),
                              InstrumentState::Halted, halted_book);
  checks.require(halted.last_command_sequence() == halted_command);
  checks.require(halted.last_engine_sequence() == halted_engine);
  checks.require(halted.pending_status_event_count() == halted_statuses);

  MatchingEngine closed(instrument);
  checks.require(submit(checks, closed, order_id(checks, 3), Side::Buy,
                        price(checks, 80), quantity(checks, 4))
                     .result == OrderBookResult::Accepted);
  checks.require(closed.process(CloseInstrument{instrument}).result ==
                 OrderBookResult::Accepted);
  const auto closed_book = logical_state(closed);
  const auto closed_command = closed.last_command_sequence();
  const auto closed_engine = closed.last_engine_sequence();
  const auto closed_statuses = closed.pending_status_event_count();
  require_rejected_transition(checks, closed,
                              closed.process(HaltInstrument{instrument}),
                              InstrumentState::Closed, closed_book);
  require_rejected_transition(checks, closed,
                              closed.process(ResumeInstrument{instrument}),
                              InstrumentState::Closed, closed_book);
  require_rejected_transition(checks, closed,
                              closed.process(CloseInstrument{instrument}),
                              InstrumentState::Closed, closed_book);
  checks.require(closed.last_command_sequence() == closed_command);
  checks.require(closed.last_engine_sequence() == closed_engine);
  checks.require(closed.pending_status_event_count() == closed_statuses);
}

void test_halted_order_gating_cancels_and_priority(Checks& checks) {
  const auto instrument = instrument_id(checks, 4);
  const auto bid_price = price(checks, 90);
  const auto lower_bid = price(checks, 89);
  const auto ask_price = price(checks, 100);
  const auto ask_one = order_id(checks, 1);
  const auto ask_two = order_id(checks, 2);
  const auto bid_one = order_id(checks, 10);
  const auto bid_two = order_id(checks, 11);
  const auto bid_three = order_id(checks, 12);
  const auto bid_four = order_id(checks, 13);
  const auto sole = order_id(checks, 14);
  MatchingEngine engine(instrument);

  checks.require(submit(checks, engine, ask_one, Side::Sell, ask_price,
                        quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask_two, Side::Sell, ask_price,
                        quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  for (const auto id : {bid_one, bid_two, bid_three, bid_four}) {
    checks.require(submit(checks, engine, id, Side::Buy, bid_price,
                          quantity(checks, 1))
                       .result == OrderBookResult::Accepted);
  }
  checks.require(submit(checks, engine, sole, Side::Buy, lower_bid,
                        quantity(checks, 1))
                     .result == OrderBookResult::Accepted);

  const auto before_halt = logical_state(engine);
  checks.require(engine.process(HaltInstrument{instrument}).result ==
                 OrderBookResult::Accepted);
  checks.require(logical_state(engine) == before_halt);
  const auto command_after_halt = engine.last_command_sequence();
  const auto engine_after_halt = engine.last_engine_sequence();
  const auto match_after_halt = engine.last_match_id();
  const auto reports_after_halt = engine.pending_execution_report_count();

  const auto marketable = submit(checks, engine, order_id(checks, 100),
                                 Side::Buy, ask_price, quantity(checks, 1));
  const auto non_marketable = submit(checks, engine, order_id(checks, 101),
                                     Side::Sell, price(checks, 110),
                                     quantity(checks, 1));
  const auto noop = amend(checks, engine, bid_two, bid_price,
                          quantity(checks, 1));
  const auto reduction = amend(checks, engine, ask_two, ask_price,
                               quantity(checks, 1));
  for (const auto* result : {&marketable, &non_marketable, &noop, &reduction}) {
    checks.require(result->result == OrderBookResult::MarketHalted);
    checks.require(!result->command_sequence.is_valid());
    checks.require(result->reports().empty());
  }
  checks.require(engine.last_command_sequence() == command_after_halt);
  checks.require(engine.last_engine_sequence() == engine_after_halt);
  checks.require(engine.last_match_id() == match_after_halt);
  checks.require(engine.pending_execution_report_count() == reports_after_halt);
  checks.require(logical_state(engine) == before_halt);

  checks.require(cancel(checks, engine, bid_one).result ==
                 OrderBookResult::Accepted);
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, bid_price)) ==
                 std::vector<OrderId>{bid_two, bid_three, bid_four});
  checks.require(cancel(checks, engine, bid_three).result ==
                 OrderBookResult::Accepted);
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, bid_price)) ==
                 std::vector<OrderId>{bid_two, bid_four});
  checks.require(cancel(checks, engine, bid_four).result ==
                 OrderBookResult::Accepted);
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, bid_price)) ==
                 std::vector<OrderId>{bid_two});
  checks.require(cancel(checks, engine, bid_two).result ==
                 OrderBookResult::Accepted);
  checks.require(engine.orders_at_level(Side::Buy, bid_price).empty());
  checks.require(cancel(checks, engine, sole).result ==
                 OrderBookResult::Accepted);
  checks.require(!engine.best_bid());
  checks.require(fifo_ids(engine.orders_at_level(Side::Sell, ask_price)) ==
                 std::vector<OrderId>{ask_one, ask_two});

  checks.require(engine.process(ResumeInstrument{instrument}).result ==
                 OrderBookResult::Accepted);
  checks.require(fifo_ids(engine.orders_at_level(Side::Sell, ask_price)) ==
                 std::vector<OrderId>{ask_one, ask_two});
  const auto aggressive = submit(checks, engine, order_id(checks, 200),
                                 Side::Buy, ask_price, quantity(checks, 4));
  checks.require(aggressive.result == OrderBookResult::Accepted);
  checks.require(aggressive.reports().size() == 2);
  checks.require(aggressive.reports()[0].resting_order_id == ask_one);
  checks.require(aggressive.reports()[1].resting_order_id == ask_two);
  checks.require(engine.find_order(ask_two)->leaves_quantity ==
                 quantity(checks, 1));

  const auto reused = submit(checks, engine, bid_one, Side::Buy,
                             price(checks, 95), quantity(checks, 1));
  checks.require(reused.result == OrderBookResult::Accepted);
  checks.require(engine.find_order(bid_one).has_value());
}

void test_buy_priority_survives_halt_resume(Checks& checks) {
  const auto instrument = instrument_id(checks, 5);
  const auto level_price = price(checks, 100);
  const auto first = order_id(checks, 1);
  const auto second = order_id(checks, 2);
  MatchingEngine engine(instrument);
  checks.require(submit(checks, engine, first, Side::Buy, level_price,
                        quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, second, Side::Buy, level_price,
                        quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  checks.require(engine.process(HaltInstrument{instrument}).result ==
                 OrderBookResult::Accepted);
  checks.require(engine.process(ResumeInstrument{instrument}).result ==
                 OrderBookResult::Accepted);
  const auto result = submit(checks, engine, order_id(checks, 10), Side::Sell,
                             level_price, quantity(checks, 4));
  checks.require(result.reports().size() == 2);
  checks.require(result.reports()[0].resting_order_id == first);
  checks.require(result.reports()[1].resting_order_id == second);
}

void test_close_reset_and_new_session_preserve_counters_and_outboxes(
    Checks& checks) {
  const auto instrument = instrument_id(checks, 6);
  const auto execution_price = price(checks, 100);
  const auto reset_id = order_id(checks, 20);
  MatchingEngine engine(instrument);
  checks.require(submit(checks, engine, order_id(checks, 1), Side::Sell,
                        execution_price, quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  const auto execution = submit(checks, engine, order_id(checks, 2), Side::Buy,
                                execution_price, quantity(checks, 1));
  checks.require(execution.reports().size() == 1);
  checks.require(submit(checks, engine, reset_id, Side::Buy, price(checks, 90),
                        quantity(checks, 4))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, order_id(checks, 21), Side::Sell,
                        price(checks, 110), quantity(checks, 5))
                     .result == OrderBookResult::Accepted);

  const auto match_before_close = engine.last_match_id();
  const auto execution_count_before_close =
      engine.pending_execution_report_count();
  const auto close = engine.process(CloseInstrument{instrument});
  checks.require(close.result == OrderBookResult::Accepted);
  checks.require(engine.instrument_state() == InstrumentState::Closed);
  checks.require(engine.active_order_count() == 0);
  checks.require(engine.depth(Side::Buy).empty() &&
                 engine.depth(Side::Sell).empty());
  checks.require(!engine.best_bid() && !engine.best_ask());
  checks.require(engine.last_match_id() == match_before_close);
  checks.require(engine.pending_execution_report_count() ==
                 execution_count_before_close);

  const auto command_before_rejections = engine.last_command_sequence();
  const auto engine_before_rejections = engine.last_engine_sequence();
  const auto closed_new = submit(checks, engine, order_id(checks, 30),
                                 Side::Buy, price(checks, 90),
                                 quantity(checks, 1));
  const auto closed_cancel = cancel(checks, engine, reset_id);
  const auto closed_amend = amend(checks, engine, reset_id, price(checks, 90),
                                  quantity(checks, 1));
  for (const auto* result : {&closed_new, &closed_cancel, &closed_amend}) {
    checks.require(result->result == OrderBookResult::InstrumentUnavailable);
    checks.require(!result->command_sequence.is_valid());
    checks.require(result->reports().empty());
  }
  checks.require(engine.last_command_sequence() == command_before_rejections);
  checks.require(engine.last_engine_sequence() == engine_before_rejections);
  checks.require(engine.last_match_id() == match_before_close);

  const auto open = engine.process(OpenInstrument{instrument});
  checks.require(open.result == OrderBookResult::Accepted);
  checks.require(open.command_sequence.value() ==
                 command_before_rejections.value() + 1);
  checks.require(engine.last_engine_sequence().value() ==
                 engine_before_rejections.value() + 1);
  checks.require(engine.last_match_id() == match_before_close);
  checks.require(engine.pending_execution_report_count() ==
                 execution_count_before_close);
  checks.require(engine.pending_status_event_count() == 2);
  checks.require(engine.instrument_state() == InstrumentState::Active);
  checks.require(engine.active_order_count() == 0);

  const auto reused = submit(checks, engine, reset_id, Side::Sell,
                             price(checks, 110), quantity(checks, 2));
  checks.require(reused.result == OrderBookResult::Accepted);
  const auto reports = drain_reports(engine);
  checks.require(reports.size() == 1 &&
                 reports[0].match_id == match_before_close);
  const auto statuses = drain_statuses(engine);
  checks.require(statuses.size() == 2);
  checks.require(statuses[0].reason == StatusReason::EndOfDay);
  checks.require(statuses[1].reason == StatusReason::SessionOpen);
}

void test_status_headroom_saturation_wraparound_and_reuse(Checks& checks) {
  const auto instrument = instrument_id(checks, 7);
  MatchingEngine engine(instrument, {}, LosslessOutboxLimits{2, 2});
  const auto preserved_order = order_id(checks, 1);
  checks.require(submit(checks, engine, preserved_order, Side::Buy,
                        price(checks, 90), quantity(checks, 3))
                     .result == OrderBookResult::Accepted);

  const auto halt = engine.process(HaltInstrument{instrument});
  checks.require(halt.result == OrderBookResult::Accepted);
  checks.require(engine.available_status_outbox_capacity() == 1);
  const auto engine_after_halt = engine.last_engine_sequence();
  const auto failed_resume = engine.process(ResumeInstrument{instrument});
  checks.require(failed_resume.result == OrderBookResult::StatusOutboxFull);
  checks.require(failed_resume.command_sequence.value() == 3);
  checks.require(engine.instrument_state() == InstrumentState::Halted);
  checks.require(engine.find_order(preserved_order).has_value());
  checks.require(engine.last_engine_sequence() == engine_after_halt);
  checks.require(engine.pending_status_event_count() == 1);

  SystemStatus observed;
  checks.require(engine.try_consume_status(observed));
  checks.require(observed.reason == StatusReason::TradingHalt);
  checks.require(engine.available_status_outbox_capacity() == 2);
  const auto resume = engine.process(ResumeInstrument{instrument});
  checks.require(resume.result == OrderBookResult::Accepted);
  checks.require(resume.command_sequence.value() == 4);
  checks.require(engine.instrument_state() == InstrumentState::Active);
  checks.require(engine.available_status_outbox_capacity() == 1);

  const auto final_slot_halt = engine.process(HaltInstrument{instrument});
  checks.require(final_slot_halt.result == OrderBookResult::Accepted);
  checks.require(final_slot_halt.command_sequence.value() == 5);
  checks.require(engine.available_status_outbox_capacity() == 0);
  const auto engine_before_failed_close = engine.last_engine_sequence();
  const auto book_before_failed_close = logical_state(engine);
  const auto failed_close = engine.process(CloseInstrument{instrument});
  checks.require(failed_close.result == OrderBookResult::StatusOutboxFull);
  checks.require(failed_close.command_sequence.value() == 6);
  checks.require(engine.instrument_state() == InstrumentState::Halted);
  checks.require(engine.last_engine_sequence() == engine_before_failed_close);
  checks.require(logical_state(engine) == book_before_failed_close);

  checks.require(engine.try_consume_status(observed));
  checks.require(observed.reason == StatusReason::TradingResume);
  checks.require(engine.available_status_outbox_capacity() == 1);
  const auto final_slot_close = engine.process(CloseInstrument{instrument});
  checks.require(final_slot_close.result == OrderBookResult::Accepted);
  checks.require(final_slot_close.command_sequence.value() == 7);
  checks.require(engine.instrument_state() == InstrumentState::Closed);
  checks.require(engine.active_order_count() == 0);
  checks.require(engine.available_status_outbox_capacity() == 0);

  const auto engine_before_failed_open = engine.last_engine_sequence();
  const auto failed_open = engine.process(OpenInstrument{instrument});
  checks.require(failed_open.result == OrderBookResult::StatusOutboxFull);
  checks.require(failed_open.command_sequence.value() == 8);
  checks.require(engine.instrument_state() == InstrumentState::Closed);
  checks.require(engine.last_engine_sequence() == engine_before_failed_open);

  checks.require(engine.try_consume_status(observed));
  checks.require(observed.reason == StatusReason::TradingHalt);
  checks.require(engine.try_consume_status(observed));
  checks.require(observed.reason == StatusReason::EndOfDay);
  checks.require(engine.available_status_outbox_capacity() == 2);
  const auto open = engine.process(OpenInstrument{instrument});
  checks.require(open.result == OrderBookResult::Accepted);
  checks.require(open.command_sequence.value() == 9);
  checks.require(engine.instrument_state() == InstrumentState::Active);
  checks.require(engine.available_status_outbox_capacity() == 1);
  checks.require(engine.pending_status_event_count() == 1);
  checks.require(engine.validate_invariants());
}

void test_resume_then_repeated_automatic_halt(Checks& checks) {
  const auto instrument = instrument_id(checks, 8);
  const auto level_price = price(checks, 100);
  const auto one = quantity(checks, 1);
  MatchingEngine engine(instrument, {}, LosslessOutboxLimits{1, 2});

  checks.require(submit(checks, engine, order_id(checks, 1), Side::Sell,
                        level_price, one)
                     .result == OrderBookResult::Accepted);
  const auto first_fill = submit(checks, engine, order_id(checks, 2), Side::Buy,
                                 level_price, one);
  checks.require(first_fill.result == OrderBookResult::Accepted);
  checks.require(first_fill.reports().size() == 1);
  checks.require(submit(checks, engine, order_id(checks, 3), Side::Sell,
                        level_price, one)
                     .result == OrderBookResult::Accepted);

  const auto before_first_failure = logical_state(engine);
  const auto match_before = engine.last_match_id();
  const auto first_failure = submit(checks, engine, order_id(checks, 4),
                                    Side::Buy, level_price, one);
  checks.require(first_failure.result == OrderBookResult::LosslessOutboxFull);
  checks.require(logical_state(engine) == before_first_failure);
  checks.require(engine.last_match_id() == match_before);
  checks.require(engine.instrument_state() == InstrumentState::Halted);
  checks.require(engine.pending_status_event_count() == 1);

  SystemStatus first_halt_status;
  checks.require(engine.try_consume_status(first_halt_status));
  checks.require(first_halt_status.reason == StatusReason::LosslessOutboxFull);
  checks.require(first_halt_status.engine_sequence.value() == 2);

  const auto command_after_failure = engine.last_command_sequence();
  const auto engine_after_failure = engine.last_engine_sequence();
  const auto rejected = submit(checks, engine, order_id(checks, 5), Side::Buy,
                               level_price, one);
  checks.require(rejected.result == OrderBookResult::MarketHalted);
  checks.require(engine.last_command_sequence() == command_after_failure);
  checks.require(engine.last_engine_sequence() == engine_after_failure);
  checks.require(engine.pending_status_event_count() == 0);

  checks.require(engine.process(ResumeInstrument{instrument}).result ==
                 OrderBookResult::Accepted);
  checks.require(engine.instrument_state() == InstrumentState::Active);
  checks.require(engine.available_status_outbox_capacity() >= 1);
  const auto before_second_failure = logical_state(engine);
  const auto second_failure = submit(checks, engine, order_id(checks, 6),
                                     Side::Buy, level_price, one);
  checks.require(second_failure.result == OrderBookResult::LosslessOutboxFull);
  checks.require(logical_state(engine) == before_second_failure);
  checks.require(engine.last_match_id() == match_before);
  checks.require(engine.instrument_state() == InstrumentState::Halted);
  checks.require(engine.pending_status_event_count() == 2);
  checks.require(engine.available_status_outbox_capacity() == 0);

  const auto statuses = drain_statuses(engine);
  checks.require(statuses.size() == 2);
  checks.require(statuses[0].reason == StatusReason::TradingResume);
  checks.require(statuses[1].reason == StatusReason::LosslessOutboxFull);
  checks.require(statuses[0].engine_sequence.value() == 3);
  checks.require(statuses[1].engine_sequence.value() == 4);
  checks.require(engine.pending_execution_report_count() == 1);
  checks.require(drain_reports(engine).size() == 1);
}

}  // namespace

int main() {
  Checks checks;
  test_valid_transition_matrix_and_statuses(checks);
  test_invalid_transition_matrix(checks);
  test_halted_order_gating_cancels_and_priority(checks);
  test_buy_priority_survives_halt_resume(checks);
  test_close_reset_and_new_session_preserve_counters_and_outboxes(checks);
  test_status_headroom_saturation_wraparound_and_reuse(checks);
  test_resume_then_repeated_automatic_halt(checks);
  return checks.passed() ? 0 : 1;
}
