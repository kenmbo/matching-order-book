#include "lob/domain/contracts.hpp"
#include "lob/domain/sequencing.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using lob::AmendOrder;
using lob::CancelOrder;
using lob::CommandKind;
using lob::CommandSequence;
using lob::EngineSequence;
using lob::ExecutionReport;
using lob::InstrumentId;
using lob::InstrumentState;
using lob::MatchId;
using lob::NewOrder;
using lob::OrderBookResult;
using lob::OrderId;
using lob::PriceTicks;
using lob::Quantity;
using lob::SequenceAllocationResult;
using lob::SequenceState;
using lob::Side;
using lob::StatusEventKind;
using lob::StatusReason;
using lob::StatusScope;
using lob::SystemStatus;

static_assert(sizeof(OrderId::rep_type) == 8);
static_assert(sizeof(InstrumentId::rep_type) == 4);
static_assert(sizeof(MatchId::rep_type) == 8);
static_assert(sizeof(CommandSequence::rep_type) == 8);
static_assert(sizeof(EngineSequence::rep_type) == 8);
static_assert(sizeof(PriceTicks::rep_type) == 8);
static_assert(sizeof(Quantity::rep_type) == 8);
static_assert(std::is_signed_v<PriceTicks::rep_type>);
static_assert(std::is_integral_v<Quantity::rep_type>);
static_assert(!std::is_floating_point_v<PriceTicks::rep_type>);
static_assert(!std::is_floating_point_v<Quantity::rep_type>);
static_assert(!std::is_convertible_v<OrderId, InstrumentId>);
static_assert(!std::is_convertible_v<OrderId, MatchId>);
static_assert(!std::is_constructible_v<OrderId, std::uint64_t>);
static_assert(std::is_trivially_copyable_v<NewOrder>);
static_assert(std::is_standard_layout_v<NewOrder>);
static_assert(std::is_trivially_copyable_v<CancelOrder>);
static_assert(std::is_standard_layout_v<CancelOrder>);
static_assert(std::is_trivially_copyable_v<AmendOrder>);
static_assert(std::is_standard_layout_v<AmendOrder>);
static_assert(std::is_trivially_copyable_v<ExecutionReport>);
static_assert(std::is_standard_layout_v<ExecutionReport>);
static_assert(std::is_trivially_copyable_v<SystemStatus>);
static_assert(std::is_standard_layout_v<SystemStatus>);

static_assert(noexcept(lob::checked_domain_cast<OrderId>(std::uint64_t{1})));
static_assert(noexcept(lob::checked_add(Quantity{}, Quantity{})));
static_assert(noexcept(lob::checked_subtract(Quantity{}, Quantity{})));
static_assert(noexcept(SequenceState{}.accept_command()));
static_assert(noexcept(SequenceState{}.commit_event_batch(1)));

class Checks final {
 public:
  void require(bool condition) noexcept {
    if (!condition) {
      failed_ = true;
    }
  }

  [[nodiscard]] bool passed() const noexcept { return !failed_; }

 private:
  bool failed_{false};
};

template <typename Domain, typename Source>
Domain make_domain(Checks& checks, Source source) noexcept {
  const auto converted = lob::checked_domain_cast<Domain>(source);
  checks.require(converted.has_value());
  return converted.value;
}

void test_checked_conversions(Checks& checks) noexcept {
  const auto order_min = lob::checked_domain_cast<OrderId>(std::uint64_t{1});
  const auto order_max = lob::checked_domain_cast<OrderId>(
      std::numeric_limits<std::uint64_t>::max());
  const auto order_zero = lob::checked_domain_cast<OrderId>(std::uint64_t{0});
  const auto order_negative = lob::checked_domain_cast<OrderId>(std::int64_t{-1});
  checks.require(order_min.has_value() && order_min.value.value() == 1);
  checks.require(order_max.has_value());
  checks.require(order_zero.result == lob::DomainConversionResult::ReservedValue);
  checks.require(order_negative.result == lob::DomainConversionResult::OutOfRange);

  const auto instrument_max = lob::checked_domain_cast<InstrumentId>(
      std::numeric_limits<std::uint32_t>::max());
  const auto instrument_overflow = lob::checked_domain_cast<InstrumentId>(
      std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1U);
  checks.require(instrument_max.has_value());
  checks.require(instrument_overflow.result ==
                 lob::DomainConversionResult::OutOfRange);

  const auto price_min = lob::checked_domain_cast<PriceTicks>(std::int64_t{1});
  const auto price_max = lob::checked_domain_cast<PriceTicks>(
      std::numeric_limits<std::int64_t>::max());
  const auto price_zero = lob::checked_domain_cast<PriceTicks>(std::int64_t{0});
  const auto price_negative =
      lob::checked_domain_cast<PriceTicks>(std::int64_t{-1});
  const auto price_unsigned_overflow = lob::checked_domain_cast<PriceTicks>(
      std::numeric_limits<std::uint64_t>::max());
  checks.require(price_min.has_value() && price_max.has_value());
  checks.require(price_zero.result == lob::DomainConversionResult::ReservedValue);
  checks.require(price_negative.result ==
                 lob::DomainConversionResult::OutOfRange);
  checks.require(price_unsigned_overflow.result ==
                 lob::DomainConversionResult::OutOfRange);

  const auto quantity_max = lob::checked_domain_cast<Quantity>(
      std::numeric_limits<std::uint64_t>::max());
  const auto quantity_zero =
      lob::checked_domain_cast<Quantity>(std::uint64_t{0});
  checks.require(quantity_max.has_value());
  checks.require(quantity_zero.result ==
                 lob::DomainConversionResult::ReservedValue);
}

void test_quantity_arithmetic(Checks& checks) noexcept {
  const auto one = make_domain<Quantity>(checks, std::uint64_t{1});
  const auto two = make_domain<Quantity>(checks, std::uint64_t{2});
  const auto maximum = make_domain<Quantity>(
      checks, std::numeric_limits<std::uint64_t>::max());

  const auto sum = lob::checked_add(one, two);
  checks.require(sum.has_value() && sum.value.value() == 3);
  checks.require(lob::checked_add(maximum, one).result ==
                 lob::QuantityArithmeticResult::Overflow);
  checks.require(lob::checked_add(Quantity{}, one).result ==
                 lob::QuantityArithmeticResult::InvalidOperand);

  const auto difference = lob::checked_subtract(two, one);
  checks.require(difference.has_value() && difference.value.value() == 1);
  checks.require(lob::checked_subtract(one, one).result ==
                 lob::QuantityArithmeticResult::Zero);
  checks.require(lob::checked_subtract(one, two).result ==
                 lob::QuantityArithmeticResult::Underflow);
}

void test_enums(Checks& checks) noexcept {
  constexpr std::array required_results{
      OrderBookResult::Accepted,
      OrderBookResult::OrderNotFound,
      OrderBookResult::DuplicateOrderId,
      OrderBookResult::InvalidInstrument,
      OrderBookResult::MarketHalted,
      OrderBookResult::InstrumentUnavailable,
      OrderBookResult::InvalidSide,
      OrderBookResult::InvalidPrice,
      OrderBookResult::InvalidQuantity,
      OrderBookResult::InvalidAmendment,
      OrderBookResult::CapacityExhausted,
      OrderBookResult::LosslessOutboxFull,
      OrderBookResult::ChannelUnavailable,
      OrderBookResult::SnapshotRequired,
  };

  for (std::size_t left = 0; left < required_results.size(); ++left) {
    for (std::size_t right = left + 1; right < required_results.size(); ++right) {
      checks.require(required_results[left] != required_results[right]);
    }
  }

  checks.require(static_cast<std::uint8_t>(Side::Buy) == 1);
  checks.require(static_cast<std::uint8_t>(Side::Sell) == 2);
  checks.require(static_cast<std::uint8_t>(CommandKind::New) == 1);
  checks.require(static_cast<std::uint8_t>(CommandKind::Cancel) == 2);
  checks.require(static_cast<std::uint8_t>(CommandKind::Amend) == 3);
  checks.require(static_cast<std::uint8_t>(InstrumentState::Active) == 1);
  checks.require(static_cast<std::uint8_t>(InstrumentState::Halted) == 2);
  checks.require(static_cast<std::uint8_t>(InstrumentState::Closed) == 3);
}

void test_commands_and_events(Checks& checks) noexcept {
  const auto order = make_domain<OrderId>(checks, std::uint64_t{11});
  const auto resting = make_domain<OrderId>(checks, std::uint64_t{12});
  const auto instrument = make_domain<InstrumentId>(checks, std::uint32_t{7});
  const auto match = make_domain<MatchId>(checks, std::uint64_t{13});
  const auto price = make_domain<PriceTicks>(checks, std::int64_t{101});
  const auto quantity = make_domain<Quantity>(checks, std::uint64_t{25});
  const auto engine = make_domain<EngineSequence>(checks, std::uint64_t{19});

  const NewOrder new_order{order, instrument, Side::Buy, price, quantity};
  const CancelOrder cancel{order, instrument};
  const AmendOrder amend{order, instrument, price, quantity};
  checks.require(new_order.order_id == order &&
                 new_order.instrument_id == instrument &&
                 new_order.side == Side::Buy &&
                 new_order.limit_price == price &&
                 new_order.quantity == quantity);
  checks.require(cancel.order_id == order && cancel.instrument_id == instrument);
  checks.require(amend.order_id == order && amend.instrument_id == instrument &&
                 amend.new_price == price &&
                 amend.new_leaves_quantity == quantity);

  const ExecutionReport report{match, instrument, order, resting, price, quantity,
                               engine};
  checks.require(report.match_id == match && report.instrument_id == instrument &&
                 report.aggressive_order_id == order &&
                 report.resting_order_id == resting &&
                 report.match_price == price &&
                 report.match_quantity == quantity &&
                 report.engine_sequence == engine);

  const SystemStatus status{StatusScope::Instrument,
                            instrument,
                            InstrumentState::Active,
                            InstrumentState::Halted,
                            StatusEventKind::StateTransition,
                            StatusReason::TradingHalt,
                            engine};
  checks.require(status.scope == StatusScope::Instrument &&
                 status.instrument_id == instrument &&
                 status.previous_state == InstrumentState::Active &&
                 status.resulting_state == InstrumentState::Halted &&
                 status.kind == StatusEventKind::StateTransition &&
                 status.reason == StatusReason::TradingHalt &&
                 status.engine_sequence == engine);
}

void test_sequences(Checks& checks) noexcept {
  SequenceState rejected;
  rejected.reject_before_acceptance();
  checks.require(rejected.last_command().value() == 0 &&
                 rejected.last_engine().value() == 0);

  SequenceState zero_event;
  const auto zero_command = zero_event.accept_command();
  const auto zero_batch = zero_event.commit_event_batch(0);
  checks.require(zero_command.assigned() && zero_command.sequence.value() == 1);
  checks.require(zero_batch.empty() && zero_event.last_engine().value() == 0);

  SequenceState batches;
  const auto one_command = batches.accept_command();
  const auto one_event = batches.commit_event_batch(1);
  checks.require(one_command.assigned() && one_event.assigned());
  checks.require(one_event.first.value() == 1 && one_event.last.value() == 1);

  const auto multi_command = batches.accept_command();
  const auto multi_event = batches.commit_event_batch(3);
  checks.require(multi_command.sequence.value() == 2 && multi_event.assigned());
  checks.require(multi_event.first.value() == 2 && multi_event.last.value() == 4 &&
                 multi_event.event_count == 3);

  const auto next_command = batches.accept_command();
  const auto next_event = batches.commit_event_batch(2);
  checks.require(next_command.sequence.value() == 3 && next_event.assigned());
  checks.require(next_event.first.value() == 5 && next_event.last.value() == 6);

  const auto previous_engine = batches.last_engine();
  const auto aborted_command = batches.accept_command();
  batches.abort_event_batch();
  checks.require(aborted_command.sequence.value() == 4 &&
                 batches.last_engine() == previous_engine);

  const auto max_command = make_domain<CommandSequence>(
      checks, std::numeric_limits<std::uint64_t>::max());
  const auto max_engine = make_domain<EngineSequence>(
      checks, std::numeric_limits<std::uint64_t>::max());
  SequenceState exhausted(max_command, max_engine);
  checks.require(exhausted.accept_command().result ==
                 SequenceAllocationResult::Exhausted);
  checks.require(exhausted.commit_event_batch(1).result ==
                 SequenceAllocationResult::Exhausted);
  checks.require(exhausted.commit_event_batch(0).empty());
  checks.require(exhausted.last_command() == max_command &&
                 exhausted.last_engine() == max_engine);

  const auto near_max_engine = make_domain<EngineSequence>(
      checks, std::numeric_limits<std::uint64_t>::max() - 2U);
  SequenceState no_gap(CommandSequence{}, near_max_engine);
  const auto failed = no_gap.commit_event_batch(3);
  checks.require(failed.result == SequenceAllocationResult::Exhausted &&
                 no_gap.last_engine() == near_max_engine);
  no_gap.abort_event_batch();
  const auto final_batch = no_gap.commit_event_batch(2);
  checks.require(final_batch.assigned());
  checks.require(final_batch.first.value() ==
                     std::numeric_limits<std::uint64_t>::max() - 1U &&
                 final_batch.last.value() ==
                     std::numeric_limits<std::uint64_t>::max());
}

}  // namespace

int main() {
  Checks checks;
  test_checked_conversions(checks);
  test_quantity_arithmetic(checks);
  test_enums(checks);
  test_commands_and_events(checks);
  test_sequences(checks);
  return checks.passed() ? 0 : 1;
}
