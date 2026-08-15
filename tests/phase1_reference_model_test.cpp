#include "phase1_reference_model.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef LOB_PHASE1_TRACE_FIXTURE
#define LOB_PHASE1_TRACE_FIXTURE ""
#endif

namespace {

using phase1_test::ObservableState;
using phase1_test::ReferenceModel;
using phase1_test::StepResult;
using phase1_test::TraceCommand;
using phase1_test::TraceCommandKind;

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

[[nodiscard]] TraceCommand new_order(lob::OrderId id,
                                     lob::InstrumentId instrument,
                                     lob::Side side, lob::PriceTicks limit,
                                     lob::Quantity leaves) {
  return {TraceCommandKind::New, id, instrument, side, limit, leaves};
}

[[nodiscard]] TraceCommand cancel_order(lob::OrderId id,
                                        lob::InstrumentId instrument) {
  return {TraceCommandKind::Cancel, id, instrument};
}

[[nodiscard]] TraceCommand amend_order(lob::OrderId id,
                                       lob::InstrumentId instrument,
                                       lob::PriceTicks new_price,
                                       lob::Quantity new_leaves) {
  return {TraceCommandKind::Amend, id, instrument, lob::Side::Invalid,
          new_price, new_leaves};
}

[[nodiscard]] TraceCommand lifecycle(TraceCommandKind kind,
                                     lob::InstrumentId instrument) {
  TraceCommand command;
  command.kind = kind;
  command.instrument_id = instrument;
  return command;
}

[[nodiscard]] ObservableState engine_state(const lob::MatchingEngine& engine) {
  ObservableState state;
  state.instrument_state = engine.instrument_state();
  state.last_command = engine.last_command_sequence();
  state.last_engine = engine.last_engine_sequence();
  state.last_match = engine.last_match_id();
  state.active_order_count = engine.active_order_count();
  state.bid_level_count = engine.price_level_count(lob::Side::Buy);
  state.ask_level_count = engine.price_level_count(lob::Side::Sell);
  state.best_bid = engine.best_bid();
  state.best_ask = engine.best_ask();
  state.bids = engine.depth(lob::Side::Buy);
  state.asks = engine.depth(lob::Side::Sell);
  for (const auto& level : state.bids) {
    const auto orders =
        engine.orders_at_level(lob::Side::Buy, level.price);
    state.bid_orders.insert(state.bid_orders.end(), orders.begin(),
                            orders.end());
  }
  for (const auto& level : state.asks) {
    const auto orders =
        engine.orders_at_level(lob::Side::Sell, level.price);
    state.ask_orders.insert(state.ask_orders.end(), orders.begin(),
                            orders.end());
  }
  return state;
}

[[nodiscard]] std::vector<lob::ExecutionReport> drain_reports(
    lob::MatchingEngine& engine) {
  std::vector<lob::ExecutionReport> reports;
  reports.reserve(engine.pending_execution_report_count());
  lob::ExecutionReport report;
  while (engine.try_consume_execution_report(report)) {
    reports.push_back(report);
  }
  return reports;
}

[[nodiscard]] std::vector<lob::SystemStatus> drain_statuses(
    lob::MatchingEngine& engine) {
  std::vector<lob::SystemStatus> statuses;
  statuses.reserve(engine.pending_status_event_count());
  lob::SystemStatus status;
  while (engine.try_consume_status(status)) {
    statuses.push_back(status);
  }
  return statuses;
}

[[nodiscard]] bool reports_equal(
    std::span<const lob::ExecutionReport> left,
    std::span<const lob::ExecutionReport> right) noexcept {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    phase1_test::execution_report_equal);
}

[[nodiscard]] bool statuses_equal(
    std::span<const lob::SystemStatus> left,
    std::span<const lob::SystemStatus> right) noexcept {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    phase1_test::system_status_equal);
}

[[nodiscard]] const lob::RestingOrderView* find_order(
    const ObservableState& state, lob::OrderId id) noexcept {
  const auto find_in = [id](const auto& orders) {
    return std::find_if(orders.begin(), orders.end(),
                        [id](const auto& order) {
                          return order.order_id == id;
                        });
  };
  const auto bid = find_in(state.bid_orders);
  if (bid != state.bid_orders.end()) {
    return &*bid;
  }
  const auto ask = find_in(state.ask_orders);
  return ask == state.ask_orders.end() ? nullptr : &*ask;
}

[[nodiscard]] std::optional<std::string> verify_conservation_and_priority(
    const ObservableState& before, const TraceCommand& command,
    const StepResult& result, const ObservableState& after) {
  if (result.result != lob::OrderBookResult::Accepted ||
      (command.kind != TraceCommandKind::New &&
       command.kind != TraceCommandKind::Amend)) {
    return std::nullopt;
  }

  lob::Side aggressive_side = command.side;
  if (command.kind == TraceCommandKind::Amend) {
    const auto* target = find_order(before, command.order_id);
    if (target == nullptr) {
      return "accepted amendment target absent before command";
    }
    aggressive_side = target->side;
  }

  std::uint64_t filled = 0;
  auto expected_resting = aggressive_side == lob::Side::Buy
                              ? before.ask_orders
                              : before.bid_orders;
  expected_resting.erase(
      std::remove_if(expected_resting.begin(), expected_resting.end(),
                     [&](const auto& resting) {
                       return aggressive_side == lob::Side::Buy
                                  ? command.price < resting.price
                                  : command.price > resting.price;
                     }),
      expected_resting.end());

  std::size_t priority_index = 0;
  std::uint64_t aggressive_remaining = command.quantity.value();
  for (const auto& report : result.synchronous_reports) {
    if (!report.match_quantity.is_valid() ||
        report.aggressive_order_id != command.order_id ||
        priority_index >= expected_resting.size()) {
      return "invalid report identity, quantity, or excess report";
    }
    const auto& expected = expected_resting[priority_index];
    const auto expected_fill =
        std::min(aggressive_remaining, expected.leaves_quantity.value());
    if (report.resting_order_id != expected.order_id ||
        report.match_price != expected.price ||
        report.match_quantity.value() != expected_fill) {
      return "report violated resting-price or price-time priority";
    }
    aggressive_remaining -= expected_fill;
    filled += expected_fill;
    ++priority_index;
  }

  const auto* remainder = find_order(after, command.order_id);
  const auto resting_remainder = remainder == nullptr
                                     ? std::uint64_t{0}
                                     : remainder->leaves_quantity.value();
  if (filled > command.quantity.value() ||
      command.quantity.value() - filled != resting_remainder) {
    return "aggressive quantity conservation failed";
  }

  for (const auto& report : result.synchronous_reports) {
    const auto* old_resting = find_order(before, report.resting_order_id);
    const auto* new_resting = find_order(after, report.resting_order_id);
    if (old_resting == nullptr ||
        report.match_quantity.value() > old_resting->leaves_quantity.value()) {
      return "reported resting reduction exceeded prior leaves";
    }
    const auto expected_leaves = old_resting->leaves_quantity.value() -
                                 report.match_quantity.value();
    if ((expected_leaves == 0 && new_resting != nullptr) ||
        (expected_leaves != 0 &&
         (new_resting == nullptr ||
          new_resting->leaves_quantity.value() != expected_leaves))) {
      return "resting-order report conservation failed";
    }
  }
  return std::nullopt;
}

struct ActualStep final {
  lob::OrderBookResult result{lob::OrderBookResult::CapacityExhausted};
  lob::CommandSequence command_sequence{};
  std::vector<lob::ExecutionReport> synchronous_reports{};
};

[[nodiscard]] ActualStep apply_engine(lob::MatchingEngine& engine,
                                      const TraceCommand& command) {
  ActualStep actual;
  switch (command.kind) {
    case TraceCommandKind::New: {
      const auto result = engine.process(lob::NewOrder{
          command.order_id, command.instrument_id, command.side, command.price,
          command.quantity});
      actual = {result.result, result.command_sequence,
                {result.reports().begin(), result.reports().end()}};
      break;
    }
    case TraceCommandKind::Cancel: {
      const auto result = engine.process(
          lob::CancelOrder{command.order_id, command.instrument_id});
      actual = {result.result, result.command_sequence,
                {result.reports().begin(), result.reports().end()}};
      break;
    }
    case TraceCommandKind::Amend: {
      const auto result = engine.process(lob::AmendOrder{
          command.order_id, command.instrument_id, command.price,
          command.quantity});
      actual = {result.result, result.command_sequence,
                {result.reports().begin(), result.reports().end()}};
      break;
    }
    case TraceCommandKind::Halt: {
      const auto result =
          engine.process(lob::HaltInstrument{command.instrument_id});
      actual.result = result.result;
      actual.command_sequence = result.command_sequence;
      break;
    }
    case TraceCommandKind::Resume: {
      const auto result =
          engine.process(lob::ResumeInstrument{command.instrument_id});
      actual.result = result.result;
      actual.command_sequence = result.command_sequence;
      break;
    }
    case TraceCommandKind::Close: {
      const auto result =
          engine.process(lob::CloseInstrument{command.instrument_id});
      actual.result = result.result;
      actual.command_sequence = result.command_sequence;
      break;
    }
    case TraceCommandKind::Open: {
      const auto result =
          engine.process(lob::OpenInstrument{command.instrument_id});
      actual.result = result.result;
      actual.command_sequence = result.command_sequence;
      break;
    }
  }
  return actual;
}

class DifferentialHarness final {
 public:
  DifferentialHarness(lob::InstrumentId instrument,
                      lob::StorageLimits storage_limits,
                      lob::LosslessOutboxLimits outbox_limits)
      : model_(instrument, storage_limits, outbox_limits),
        engine_(instrument, storage_limits, outbox_limits) {}

  [[nodiscard]] const ReferenceModel& model() const noexcept { return model_; }
  [[nodiscard]] const std::string& transcript() const noexcept {
    return transcript_;
  }

  [[nodiscard]] bool step(const TraceCommand& command, std::uint64_t seed,
                          std::size_t command_index) {
    trace_.push_back(command);
    const auto before = model_.observable_state();
    const auto expected = model_.apply(command);
    const auto actual = apply_engine(engine_, command);
    const auto expected_state = model_.observable_state();
    const auto actual_state = engine_state(engine_);

    std::string mismatch;
    if (actual.result != expected.result) {
      mismatch = "result";
    } else if (actual.command_sequence != expected.command_sequence) {
      mismatch = "command sequence";
    } else if (!reports_equal(actual.synchronous_reports,
                              expected.synchronous_reports)) {
      mismatch = "synchronous execution reports";
    } else if (!engine_.validate_invariants() ||
               !model_.validate_invariants()) {
      mismatch = "invariant validation";
    } else if (actual_state != expected_state) {
      mismatch = "observable state";
    } else if (engine_.pending_execution_report_count() !=
                   model_.pending_execution_reports().size() ||
               engine_.pending_status_event_count() !=
                   model_.pending_statuses().size()) {
      mismatch = "committed outbox occupancy";
    }

    const auto actual_reports = drain_reports(engine_);
    const auto expected_reports = model_.drain_execution_reports();
    const auto actual_statuses = drain_statuses(engine_);
    const auto expected_statuses = model_.drain_statuses();
    if (mismatch.empty() &&
        !reports_equal(actual_reports, expected_reports)) {
      mismatch = "committed execution outbox contents";
    }
    if (mismatch.empty() &&
        !statuses_equal(actual_statuses, expected_statuses)) {
      mismatch = "committed status outbox contents";
    }
    if (mismatch.empty()) {
      const auto property_failure = verify_conservation_and_priority(
          before, command, expected, expected_state);
      if (property_failure) {
        mismatch = *property_failure;
      }
    }

    transcript_ += phase1_test::serialize_command(command);
    transcript_ += " => ";
    transcript_ += std::to_string(static_cast<unsigned>(actual.result));
    transcript_ += ':' + std::to_string(actual.command_sequence.value());
    transcript_ += ':' + std::to_string(actual_reports.size());
    transcript_ += ':' + std::to_string(actual_statuses.size());
    transcript_ += ' ' + phase1_test::describe_state(actual_state) + '\n';

    if (!mismatch.empty()) {
      std::cerr << "differential failure: " << mismatch << "\nseed=" << seed
                << " command_index=" << command_index
                << " command=" << phase1_test::serialize_command(command)
                << "\nexpected_result="
                << static_cast<unsigned>(expected.result)
                << " actual_result=" << static_cast<unsigned>(actual.result)
                << "\nexpected="
                << phase1_test::describe_state(expected_state)
                << "\nactual=" << phase1_test::describe_state(actual_state)
                << "\nreplay_trace:\n"
                << phase1_test::serialize_trace(trace_);
      return false;
    }
    return true;
  }

 private:
  ReferenceModel model_;
  lob::MatchingEngine engine_;
  std::vector<TraceCommand> trace_{};
  std::string transcript_{};
};

[[nodiscard]] std::uint64_t bounded(std::mt19937_64& rng,
                                    std::uint64_t bound) {
  return rng() % bound;
}

[[nodiscard]] const lob::RestingOrderView* choose_active(
    const std::vector<lob::RestingOrderView>& active,
    std::mt19937_64& rng) noexcept {
  if (active.empty()) {
    return nullptr;
  }
  const auto selector = bounded(rng, 4);
  if (selector == 0) {
    return &active.front();
  }
  if (selector == 1) {
    return &active.back();
  }
  if (selector == 2) {
    return &active[active.size() / 2];
  }
  return &active[bounded(rng, active.size())];
}

[[nodiscard]] TraceCommand generate_active_command(
    const ReferenceModel& model, lob::InstrumentId instrument,
    std::mt19937_64& rng) {
  const auto active = model.active_orders();
  const auto roll = bounded(rng, 100);
  if (roll < 40) {
    lob::OrderId id = order_id(1 + bounded(rng, 64));
    if (!active.empty() && bounded(rng, 5) == 0) {
      id = choose_active(active, rng)->order_id;
    }
    return new_order(id, instrument,
                     bounded(rng, 2) == 0 ? lob::Side::Buy : lob::Side::Sell,
                     price(95 + static_cast<std::int64_t>(bounded(rng, 11))),
                     quantity(1 + bounded(rng, 20)));
  }
  if (roll < 64) {
    const auto* target = choose_active(active, rng);
    if (target == nullptr) {
      return amend_order(order_id(1 + bounded(rng, 64)), instrument,
                         price(100), quantity(1));
    }
    const auto mode = bounded(rng, 5);
    if (mode == 0) {
      const auto reduced = target->leaves_quantity.value() > 1
                               ? target->leaves_quantity.value() - 1
                               : target->leaves_quantity.value();
      return amend_order(target->order_id, instrument, target->price,
                         quantity(reduced));
    }
    if (mode == 1) {
      return amend_order(target->order_id, instrument, target->price,
                         target->leaves_quantity);
    }
    if (mode == 2) {
      return amend_order(target->order_id, instrument, target->price,
                         quantity(target->leaves_quantity.value() + 1));
    }
    return amend_order(
        target->order_id, instrument,
        price(94 + static_cast<std::int64_t>(bounded(rng, 13))),
        quantity(1 + bounded(rng, 24)));
  }
  if (roll < 79) {
    const auto* target = choose_active(active, rng);
    if (target != nullptr && bounded(rng, 4) != 0) {
      return cancel_order(target->order_id, instrument);
    }
    return cancel_order(order_id(65 + bounded(rng, 32)), instrument);
  }
  if (roll < 89) {
    const auto lifecycle_roll = bounded(rng, 4);
    return lifecycle(lifecycle_roll == 0 ? TraceCommandKind::Halt
                     : lifecycle_roll == 1 ? TraceCommandKind::Close
                     : lifecycle_roll == 2 ? TraceCommandKind::Resume
                                           : TraceCommandKind::Open,
                     instrument);
  }

  const auto invalid = bounded(rng, 7);
  auto command = new_order(order_id(100 + bounded(rng, 32)), instrument,
                           lob::Side::Buy, price(93), quantity(1));
  if (invalid == 0) {
    command.instrument_id = instrument_id(2);
  } else if (invalid == 1) {
    command.order_id = {};
  } else if (invalid == 2) {
    command.side = lob::Side::Invalid;
  } else if (invalid == 3) {
    command.price = {};
  } else if (invalid == 4) {
    command.quantity = {};
  } else if (invalid == 5) {
    return amend_order(order_id(65 + bounded(rng, 32)), instrument,
                       price(100), quantity(1));
  } else {
    return cancel_order(lob::OrderId{}, instrument);
  }
  return command;
}

[[nodiscard]] TraceCommand generate_command(const ReferenceModel& model,
                                            lob::InstrumentId instrument,
                                            std::mt19937_64& rng) {
  const auto state = model.observable_state().instrument_state;
  const auto active = model.active_orders();
  if (state == lob::InstrumentState::Active) {
    return generate_active_command(model, instrument, rng);
  }
  if (state == lob::InstrumentState::Halted) {
    const auto roll = bounded(rng, 100);
    if (roll < 30 && !active.empty()) {
      return cancel_order(choose_active(active, rng)->order_id, instrument);
    }
    if (roll < 55) {
      return lifecycle(TraceCommandKind::Resume, instrument);
    }
    if (roll < 70) {
      return lifecycle(TraceCommandKind::Close, instrument);
    }
    if (roll < 85) {
      return new_order(order_id(100 + bounded(rng, 32)), instrument,
                       lob::Side::Buy, price(100), quantity(1));
    }
    if (!active.empty()) {
      const auto* target = choose_active(active, rng);
      return amend_order(target->order_id, instrument, target->price,
                         target->leaves_quantity);
    }
    return lifecycle(TraceCommandKind::Halt, instrument);
  }

  const auto roll = bounded(rng, 100);
  if (roll < 60) {
    return lifecycle(TraceCommandKind::Open, instrument);
  }
  if (roll < 75) {
    return lifecycle(TraceCommandKind::Halt, instrument);
  }
  if (roll < 90) {
    return new_order(order_id(100 + bounded(rng, 32)), instrument,
                     lob::Side::Sell, price(100), quantity(1));
  }
  return lifecycle(TraceCommandKind::Open, instrument_id(2));
}

[[nodiscard]] bool run_trace(const std::vector<TraceCommand>& trace,
                             std::uint64_t seed, std::string* transcript) {
  const auto instrument = instrument_id(1);
  DifferentialHarness harness(instrument, lob::StorageLimits{48, 6},
                              lob::LosslessOutboxLimits{512, 16});
  for (std::size_t index = 0; index < trace.size(); ++index) {
    if (!harness.step(trace[index], seed, index)) {
      return false;
    }
  }
  if (transcript != nullptr) {
    *transcript = harness.transcript();
  }
  return true;
}

[[nodiscard]] bool run_generated(std::uint64_t seed,
                                 std::size_t command_count) {
  const auto instrument = instrument_id(1);
  DifferentialHarness harness(instrument, lob::StorageLimits{48, 6},
                              lob::LosslessOutboxLimits{512, 16});
  std::mt19937_64 rng(seed);
  std::vector<TraceCommand> trace;
  trace.reserve(command_count);
  for (std::size_t index = 0; index < command_count; ++index) {
    const auto command = generate_command(harness.model(), instrument, rng);
    trace.push_back(command);
    if (!harness.step(command, seed, index)) {
      return false;
    }
  }

  const auto serialized = phase1_test::serialize_trace(trace);
  const auto parsed = phase1_test::parse_trace(serialized);
  if (!parsed || *parsed != trace) {
    std::cerr << "trace round-trip failure for seed=" << seed << '\n';
    return false;
  }
  std::string replay_transcript;
  if (!run_trace(*parsed, seed, &replay_transcript) ||
      replay_transcript != harness.transcript()) {
    std::cerr << "deterministic replay failure for seed=" << seed << '\n';
    return false;
  }
  return true;
}

[[nodiscard]] bool test_serialization_and_fixture() {
  const std::vector<TraceCommand> commands{
      new_order(order_id(1), instrument_id(1), lob::Side::Buy, price(100),
                quantity(7)),
      cancel_order(order_id(1), instrument_id(1)),
      amend_order(order_id(2), instrument_id(1), price(101), quantity(3)),
      lifecycle(TraceCommandKind::Halt, instrument_id(1)),
      lifecycle(TraceCommandKind::Resume, instrument_id(1)),
      lifecycle(TraceCommandKind::Close, instrument_id(1)),
      lifecycle(TraceCommandKind::Open, instrument_id(1)),
      new_order({}, instrument_id(1), lob::Side::Invalid, {}, {}),
  };
  const auto serialized = phase1_test::serialize_trace(commands);
  const auto parsed = phase1_test::parse_trace(serialized);
  if (!parsed || *parsed != commands) {
    std::cerr << "fixed trace serialization round-trip failed\n";
    return false;
  }

  const auto fixture = phase1_test::load_trace(LOB_PHASE1_TRACE_FIXTURE);
  if (!fixture || fixture->empty()) {
    std::cerr << "could not load permanent Phase 1 replay fixture: "
              << LOB_PHASE1_TRACE_FIXTURE << '\n';
    return false;
  }
  std::string first;
  std::string second;
  return run_trace(*fixture, 0, &first) && run_trace(*fixture, 0, &second) &&
         first == second;
}

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(
    std::string_view value) noexcept {
  std::uint64_t parsed = 0;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return parsed;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string_view{argv[1]} == "--replay") {
    const auto trace = phase1_test::load_trace(argv[2]);
    if (!trace) {
      std::cerr << "failed to parse replay trace: " << argv[2] << '\n';
      return 1;
    }
    return run_trace(*trace, 0, nullptr) ? 0 : 1;
  }
  if (argc == 5 && std::string_view{argv[1]} == "--seed" &&
      std::string_view{argv[3]} == "--commands") {
    const auto seed = parse_unsigned(argv[2]);
    const auto commands = parse_unsigned(argv[4]);
    if (!seed || !commands ||
        *commands > std::numeric_limits<std::size_t>::max()) {
      std::cerr << "invalid seed or command count\n";
      return 1;
    }
    return run_generated(*seed, static_cast<std::size_t>(*commands)) ? 0 : 1;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--soak") {
    constexpr std::uint64_t seeds[]{0x5eedULL, 0xc0ffeeULL, 0x12345678ULL,
                                    0xd1ff3eULL};
    for (const auto seed : seeds) {
      if (!run_generated(seed, 25'000)) {
        return 1;
      }
    }
    return 0;
  }
  if (argc != 1) {
    std::cerr << "usage: lob_phase1_reference_model_test [--replay PATH | "
                 "--seed N --commands N | --soak]\n";
    return 1;
  }

  if (!test_serialization_and_fixture()) {
    return 1;
  }
  constexpr std::uint64_t default_seeds[]{0x5eedULL, 0xc0ffeeULL,
                                          0x12345678ULL, 0xd1ff3eULL};
  for (const auto seed : default_seeds) {
    if (!run_generated(seed, 2'500)) {
      return 1;
    }
  }
  return 0;
}
