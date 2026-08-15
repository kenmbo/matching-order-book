#include "lob/matching/matching_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace lob {

struct MatchingEngineTestAccess final {
  static void set_counters(MatchingEngine& engine,
                           CommandSequence last_command,
                           EngineSequence last_engine,
                           MatchId last_match) noexcept {
    engine.sequences_ = SequenceState(last_command, last_engine);
    engine.last_match_id_ = last_match;
  }
};

}  // namespace lob

namespace {

class Checks final {
 public:
  void require(bool condition, std::string_view message) {
    if (!condition) {
      std::cerr << "phase1 hardening failure: " << message << '\n';
      failed_ = true;
    }
  }

  [[nodiscard]] bool passed() const noexcept { return !failed_; }

 private:
  bool failed_{};
};

template <typename Domain, typename Source>
[[nodiscard]] Domain make_domain(Source value) {
  const auto converted = lob::checked_domain_cast<Domain>(value);
  if (!converted.has_value()) {
    std::abort();
  }
  return converted.value;
}

[[nodiscard]] lob::OrderId order_id(std::uint64_t value) {
  return make_domain<lob::OrderId>(value);
}

[[nodiscard]] lob::InstrumentId instrument_id(std::uint32_t value) {
  return make_domain<lob::InstrumentId>(value);
}

[[nodiscard]] lob::PriceTicks price(std::int64_t value) {
  return make_domain<lob::PriceTicks>(value);
}

[[nodiscard]] lob::Quantity quantity(std::uint64_t value) {
  return make_domain<lob::Quantity>(value);
}

[[nodiscard]] lob::CommandSequence command_sequence(std::uint64_t value) {
  return make_domain<lob::CommandSequence>(value);
}

[[nodiscard]] lob::EngineSequence engine_sequence(std::uint64_t value) {
  return make_domain<lob::EngineSequence>(value);
}

[[nodiscard]] lob::MatchId match_id(std::uint64_t value) {
  return make_domain<lob::MatchId>(value);
}

struct Snapshot final {
  lob::InstrumentState instrument_state{lob::InstrumentState::Invalid};
  lob::CommandSequence command{};
  lob::EngineSequence engine{};
  lob::MatchId match{};
  std::vector<lob::DepthEntry> bids{};
  std::vector<lob::DepthEntry> asks{};
  std::vector<lob::RestingOrderView> bid_orders{};
  std::vector<lob::RestingOrderView> ask_orders{};
  std::size_t pending_reports{};
  std::size_t pending_statuses{};
};

[[nodiscard]] Snapshot snapshot(const lob::MatchingEngine& engine) {
  Snapshot result;
  result.instrument_state = engine.instrument_state();
  result.command = engine.last_command_sequence();
  result.engine = engine.last_engine_sequence();
  result.match = engine.last_match_id();
  result.bids = engine.depth(lob::Side::Buy);
  result.asks = engine.depth(lob::Side::Sell);
  for (const auto& level : result.bids) {
    const auto orders =
        engine.orders_at_level(lob::Side::Buy, level.price);
    result.bid_orders.insert(result.bid_orders.end(), orders.begin(),
                             orders.end());
  }
  for (const auto& level : result.asks) {
    const auto orders =
        engine.orders_at_level(lob::Side::Sell, level.price);
    result.ask_orders.insert(result.ask_orders.end(), orders.begin(),
                             orders.end());
  }
  result.pending_reports = engine.pending_execution_report_count();
  result.pending_statuses = engine.pending_status_event_count();
  return result;
}

[[nodiscard]] bool same_book(const Snapshot& left,
                             const Snapshot& right) noexcept {
  return left.bids == right.bids && left.asks == right.asks &&
         left.bid_orders == right.bid_orders &&
         left.ask_orders == right.ask_orders;
}

[[nodiscard]] bool same_aborted_order_transaction(
    const Snapshot& before, const Snapshot& after) noexcept {
  return before.instrument_state == after.instrument_state &&
         before.engine == after.engine && before.match == after.match &&
         same_book(before, after) &&
         before.pending_reports == after.pending_reports &&
         before.pending_statuses == after.pending_statuses;
}

[[nodiscard]] lob::NewOrderResult submit(lob::MatchingEngine& engine,
                                         lob::OrderId id, lob::Side side,
                                         lob::PriceTicks limit,
                                         lob::Quantity leaves) {
  return engine.process(
      lob::NewOrder{id, engine.instrument_id(), side, limit, leaves});
}

void require_valid(Checks& checks, const lob::MatchingEngine& engine,
                   std::string_view context) {
  checks.require(engine.validate_invariants(), context);
}

void test_last_command_sequence(Checks& checks) {
  const auto instrument = instrument_id(1);
  lob::MatchingEngine engine(instrument);
  lob::MatchingEngineTestAccess::set_counters(
      engine,
      command_sequence(std::numeric_limits<std::uint64_t>::max() - 1), {}, {});

  const auto final = submit(engine, order_id(1), lob::Side::Buy, price(90),
                            quantity(1));
  checks.require(final.result == lob::OrderBookResult::Accepted,
                 "last command sequence should be assignable");
  checks.require(final.command_sequence.value() ==
                     std::numeric_limits<std::uint64_t>::max(),
                 "last command sequence value");
  checks.require(engine.last_engine_sequence().value() == 0,
                 "zero-event success at command exhaustion");

  const auto before = snapshot(engine);
  const auto exhausted = submit(engine, order_id(2), lob::Side::Buy, price(89),
                                quantity(1));
  const auto after = snapshot(engine);
  checks.require(exhausted.result == lob::OrderBookResult::CapacityExhausted,
                 "command sequence exhaustion result");
  checks.require(!exhausted.command_sequence.is_valid(),
                 "exhausted command is not accepted");
  checks.require(before.command == after.command && same_book(before, after) &&
                     before.engine == after.engine &&
                     before.match == after.match,
                 "command exhaustion is non-mutating and does not wrap");
  require_valid(checks, engine, "command exhaustion invariants");
}

void test_last_engine_and_match_ids(Checks& checks) {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto instrument = instrument_id(2);
  lob::MatchingEngine engine(instrument);
  checks.require(submit(engine, order_id(1), lob::Side::Sell, price(100),
                        quantity(1))
                     .result == lob::OrderBookResult::Accepted,
                 "setup final engine/match assignment");
  lob::MatchingEngineTestAccess::set_counters(
      engine, command_sequence(10), engine_sequence(maximum - 1),
      match_id(maximum - 1));

  const auto final = submit(engine, order_id(2), lob::Side::Buy, price(100),
                            quantity(1));
  checks.require(final.result == lob::OrderBookResult::Accepted &&
                     final.reports().size() == 1,
                 "last engine and match IDs should be assignable");
  checks.require(final.reports()[0].engine_sequence.value() == maximum &&
                     final.reports()[0].match_id.value() == maximum,
                 "last engine and match ID values");
  checks.require(engine.last_engine_sequence().value() == maximum &&
                     engine.last_match_id().value() == maximum,
                 "last counters committed without wrap");

  checks.require(submit(engine, order_id(3), lob::Side::Sell, price(100),
                        quantity(1))
                     .result == lob::OrderBookResult::Accepted,
                 "zero-event success after event counter exhaustion");
  const auto before = snapshot(engine);
  const auto exhausted = submit(engine, order_id(4), lob::Side::Buy, price(100),
                                quantity(1));
  const auto after = snapshot(engine);
  checks.require(exhausted.result == lob::OrderBookResult::CapacityExhausted,
                 "event counter exhaustion result");
  checks.require(exhausted.command_sequence.value() ==
                     before.command.value() + 1,
                 "accepted command retained on event preflight failure");
  checks.require(same_aborted_order_transaction(before, after),
                 "event exhaustion preserves order transaction state");
  require_valid(checks, engine, "event exhaustion invariants");
}

void test_independent_counter_and_batch_preflight(Checks& checks) {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto instrument = instrument_id(3);

  const auto run_one_fill_failure = [&](lob::EngineSequence engine_value,
                                        lob::MatchId match_value,
                                        std::string_view label) {
    lob::MatchingEngine engine(instrument);
    checks.require(submit(engine, order_id(1), lob::Side::Sell, price(100),
                          quantity(1))
                       .result == lob::OrderBookResult::Accepted,
                   "one-fill preflight setup");
    lob::MatchingEngineTestAccess::set_counters(
        engine, command_sequence(5), engine_value, match_value);
    const auto before = snapshot(engine);
    const auto result = submit(engine, order_id(2), lob::Side::Buy, price(100),
                               quantity(1));
    const auto after = snapshot(engine);
    checks.require(result.result == lob::OrderBookResult::CapacityExhausted,
                   label);
    checks.require(result.command_sequence.value() == 6,
                   "counter preflight retains accepted command");
    checks.require(same_aborted_order_transaction(before, after),
                   "counter preflight failure atomicity");
    require_valid(checks, engine, "counter preflight invariants");
  };

  run_one_fill_failure(engine_sequence(maximum), match_id(5),
                       "engine sequence exhaustion isolated");
  run_one_fill_failure(engine_sequence(5), match_id(maximum),
                       "match ID exhaustion isolated");

  const auto run_two_fill_failure = [&](lob::EngineSequence engine_value,
                                        lob::MatchId match_value,
                                        std::string_view label) {
    lob::MatchingEngine engine(instrument);
    checks.require(submit(engine, order_id(10), lob::Side::Sell, price(100),
                          quantity(1))
                       .result == lob::OrderBookResult::Accepted,
                   "two-fill first setup order");
    checks.require(submit(engine, order_id(11), lob::Side::Sell, price(100),
                          quantity(1))
                       .result == lob::OrderBookResult::Accepted,
                   "two-fill second setup order");
    lob::MatchingEngineTestAccess::set_counters(
        engine, command_sequence(8), engine_value, match_value);
    const auto before = snapshot(engine);
    const auto result = submit(engine, order_id(12), lob::Side::Buy, price(100),
                               quantity(2));
    const auto after = snapshot(engine);
    checks.require(result.result == lob::OrderBookResult::CapacityExhausted,
                   label);
    checks.require(same_aborted_order_transaction(before, after),
                   "multi-event range preflight is atomic");
  };

  run_two_fill_failure(engine_sequence(maximum - 1), {},
                       "engine contiguous range shortage by one");
  run_two_fill_failure({}, match_id(maximum - 1),
                       "match ID contiguous range shortage by one");
}

void test_lifecycle_engine_sequence_preflight(Checks& checks) {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto instrument = instrument_id(4);
  lob::MatchingEngine engine(instrument);
  checks.require(submit(engine, order_id(1), lob::Side::Buy, price(90),
                        quantity(3))
                     .result == lob::OrderBookResult::Accepted,
                 "lifecycle sequence setup order");
  lob::MatchingEngineTestAccess::set_counters(
      engine, command_sequence(20), engine_sequence(maximum), match_id(7));
  const auto before = snapshot(engine);
  const auto result = engine.process(lob::HaltInstrument{instrument});
  const auto after = snapshot(engine);

  checks.require(result.result == lob::OrderBookResult::CapacityExhausted,
                 "lifecycle engine-sequence exhaustion result");
  checks.require(result.command_sequence.value() == 21 &&
                     after.command.value() == 21,
                 "lifecycle accepted command remains consumed");
  checks.require(after.instrument_state == before.instrument_state &&
                     same_book(before, after) && after.engine == before.engine &&
                     after.match == before.match &&
                     after.pending_statuses == before.pending_statuses,
                 "lifecycle preflight failure is atomic");
  require_valid(checks, engine, "lifecycle preflight invariants");
}

[[nodiscard]] bool report_equal(const lob::ExecutionReport& left,
                                const lob::ExecutionReport& right) noexcept {
  return left.match_id == right.match_id &&
         left.instrument_id == right.instrument_id &&
         left.aggressive_order_id == right.aggressive_order_id &&
         left.resting_order_id == right.resting_order_id &&
         left.match_price == right.match_price &&
         left.match_quantity == right.match_quantity &&
         left.engine_sequence == right.engine_sequence;
}

void test_execution_outbox_exact_fit_and_shortage(Checks& checks) {
  const auto instrument = instrument_id(5);
  const auto one = quantity(1);
  const auto level_price = price(100);

  lob::MatchingEngine exact(instrument, {},
                            lob::LosslessOutboxLimits{256, 2});
  for (std::uint64_t value = 1; value <= 256; ++value) {
    checks.require(submit(exact, order_id(value), lob::Side::Sell,
                          level_price, one)
                       .result == lob::OrderBookResult::Accepted,
                   "exact-fit setup order");
  }
  const auto exact_result = submit(exact, order_id(1'000), lob::Side::Buy,
                                   level_price, quantity(256));
  checks.require(exact_result.result == lob::OrderBookResult::Accepted &&
                     exact_result.reports().size() == 256 &&
                     exact.pending_execution_report_count() == 256 &&
                     exact.available_execution_outbox_capacity() == 0,
                 "execution outbox accepts exact 256-report fit");
  require_valid(checks, exact, "exact-fit execution outbox invariants");

  lob::MatchingEngine short_one(instrument, {},
                                lob::LosslessOutboxLimits{256, 2});
  checks.require(submit(short_one, order_id(1), lob::Side::Sell, price(90), one)
                     .result == lob::OrderBookResult::Accepted,
                 "shortage warmup resting order");
  const auto warmup = submit(short_one, order_id(2), lob::Side::Buy, price(90),
                             one);
  checks.require(warmup.result == lob::OrderBookResult::Accepted &&
                     warmup.reports().size() == 1,
                 "shortage warmup report");
  for (std::uint64_t value = 0; value < 256; ++value) {
    checks.require(submit(short_one, order_id(100 + value), lob::Side::Sell,
                          level_price, one)
                       .result == lob::OrderBookResult::Accepted,
                   "shortage setup order");
  }
  const auto before = snapshot(short_one);
  const auto failure = submit(short_one, order_id(1'001), lob::Side::Buy,
                              level_price, quantity(256));
  const auto after = snapshot(short_one);
  checks.require(failure.result == lob::OrderBookResult::LosslessOutboxFull,
                 "255 slots reject a 256-report batch");
  checks.require(failure.command_sequence.value() ==
                     before.command.value() + 1,
                 "outbox failure retains accepted command sequence");
  checks.require(same_book(before, after) && before.match == after.match &&
                     before.pending_reports == after.pending_reports,
                 "outbox shortage aborts all order mutation/publication");
  checks.require(after.instrument_state == lob::InstrumentState::Halted &&
                     after.engine.value() == before.engine.value() + 1 &&
                     after.pending_statuses == before.pending_statuses + 1,
                 "outbox shortage commits exactly one automatic halt");

  lob::ExecutionReport observed;
  checks.require(short_one.try_consume_execution_report(observed) &&
                     report_equal(observed, warmup.reports()[0]) &&
                     !short_one.try_consume_execution_report(observed),
                 "aborted batch leaves prior committed execution unchanged");
  lob::SystemStatus status;
  checks.require(short_one.try_consume_status(status) &&
                     status.reason == lob::StatusReason::LosslessOutboxFull &&
                     status.previous_state == lob::InstrumentState::Active &&
                     status.resulting_state == lob::InstrumentState::Halted &&
                     status.engine_sequence == after.engine &&
                     !short_one.try_consume_status(status),
                 "automatic halt status is exact and unique");
  const auto command_after_halt = short_one.last_command_sequence();
  const auto rejected = submit(short_one, order_id(2'000), lob::Side::Buy,
                               level_price, one);
  checks.require(rejected.result == lob::OrderBookResult::MarketHalted &&
                     short_one.last_command_sequence() == command_after_halt &&
                     short_one.pending_status_event_count() == 0,
                 "already halted engine emits no duplicate automatic halt");
  require_valid(checks, short_one, "outbox shortage invariants");
}

void test_maximum_values_and_aggregate_failure(Checks& checks) {
  const auto instrument = instrument_id(6);
  lob::MatchingEngine engine(instrument, lob::StorageLimits{2, 1});
  const auto maximum_price =
      price(std::numeric_limits<std::int64_t>::max());
  const auto maximum_quantity =
      quantity(std::numeric_limits<std::uint64_t>::max());
  checks.require(submit(engine, order_id(1), lob::Side::Buy, maximum_price,
                        maximum_quantity)
                     .result == lob::OrderBookResult::Accepted,
                 "maximum valid price and quantity are accepted");
  const auto before = snapshot(engine);
  const auto overflow = submit(engine, order_id(2), lob::Side::Buy,
                               maximum_price, quantity(1));
  const auto after = snapshot(engine);
  checks.require(overflow.result == lob::OrderBookResult::CapacityExhausted,
                 "maximum aggregate plus one is rejected");
  checks.require(same_aborted_order_transaction(before, after),
                 "aggregate overflow preflight is atomic");
  require_valid(checks, engine, "maximum value invariants");
}

}  // namespace

int main() {
  Checks checks;
  test_last_command_sequence(checks);
  test_last_engine_and_match_ids(checks);
  test_independent_counter_and_batch_preflight(checks);
  test_lifecycle_engine_sequence_preflight(checks);
  test_execution_outbox_exact_fit_and_shortage(checks);
  test_maximum_values_and_aggregate_failure(checks);
  return checks.passed() ? 0 : 1;
}
