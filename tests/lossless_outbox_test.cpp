#include "lob/egress/lossless_outbox.hpp"
#include "lob/domain/contracts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

struct Record final {
  std::uint64_t value{};

  constexpr bool operator==(const Record&) const noexcept = default;
};

static_assert(noexcept(std::declval<lob::LosslessOutbox<Record>&>().reserve(1)));
static_assert(noexcept(std::declval<lob::LosslessOutbox<Record>&>().try_pop(
    std::declval<Record&>())));

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

void test_empty_and_zero_reservation(Checks& checks) {
  lob::LosslessOutbox<Record> outbox(8);
  Record record{};

  checks.require(outbox.capacity() == 8);
  checks.require(outbox.available_capacity() == 8);
  checks.require(outbox.empty() && outbox.size() == 0);
  checks.require(!outbox.try_peek(record));
  checks.require(!outbox.try_pop(record));

  auto reservation = outbox.reserve(0);
  checks.require(reservation.valid() && reservation.size() == 0);
  checks.require(!reservation.write(Record{1}));
  checks.require(outbox.empty());
  checks.require(reservation.commit());
  checks.require(!reservation.commit());
  checks.require(outbox.empty() && outbox.available_capacity() == 8);
  checks.require(outbox.validate_invariants());
}

void test_visibility_and_single_commit(Checks& checks) {
  lob::LosslessOutbox<Record> outbox(4);
  Record record{};
  auto reservation = outbox.reserve(1);

  checks.require(reservation.valid());
  checks.require(reservation.write(Record{11}));
  checks.require(!reservation.write(Record{12}));
  checks.require(outbox.empty());
  checks.require(!outbox.try_peek(record));
  checks.require(!outbox.try_pop(record));
  checks.require(reservation.commit());
  checks.require(!reservation.commit());

  checks.require(outbox.size() == 1);
  checks.require(outbox.try_peek(record) && record == Record{11});
  checks.require(outbox.size() == 1);
  checks.require(outbox.try_pop(record) && record == Record{11});
  checks.require(!outbox.try_pop(record));
  checks.require(outbox.available_capacity() == 4);
  checks.require(outbox.validate_invariants());
}

void test_exact_capacity_and_insufficient_by_one(Checks& checks) {
  lob::LosslessOutbox<Record> outbox(8);
  auto exact = outbox.reserve(8);
  checks.require(exact.valid() && exact.size() == 8);
  for (std::uint64_t value = 1; value <= 8; ++value) {
    checks.require(exact.write(Record{value}));
  }
  checks.require(exact.commit());
  checks.require(outbox.size() == 8 && outbox.available_capacity() == 0);
  checks.require(!outbox.reserve(1).valid());

  Record record{};
  for (std::uint64_t value = 1; value <= 8; ++value) {
    checks.require(outbox.try_pop(record) && record == Record{value});
  }
  checks.require(outbox.empty());

  auto too_large = outbox.reserve(9);
  checks.require(!too_large.valid());
  checks.require(outbox.empty() && outbox.available_capacity() == 8);
  checks.require(outbox.validate_invariants());
}

void test_partial_occupancy_wraparound_and_reuse(Checks& checks) {
  lob::LosslessOutbox<Record> outbox(4);
  auto first = outbox.reserve(3);
  for (std::uint64_t value = 1; value <= 3; ++value) {
    checks.require(first.write(Record{value}));
  }
  checks.require(first.commit());

  Record record{};
  checks.require(outbox.try_pop(record) && record == Record{1});
  checks.require(outbox.try_pop(record) && record == Record{2});
  checks.require(outbox.size() == 1 && outbox.available_capacity() == 3);

  auto wrapped = outbox.reserve(3);
  checks.require(wrapped.valid());
  for (std::uint64_t value = 4; value <= 6; ++value) {
    checks.require(wrapped.write(Record{value}));
  }
  checks.require(wrapped.commit());
  checks.require(outbox.size() == 4);

  constexpr std::array expected{std::uint64_t{3}, std::uint64_t{4},
                                std::uint64_t{5}, std::uint64_t{6}};
  for (const auto value : expected) {
    checks.require(outbox.try_pop(record) && record == Record{value});
  }
  checks.require(outbox.empty() && outbox.available_capacity() == 4);

  auto reused = outbox.reserve(2);
  checks.require(reused.valid());
  checks.require(reused.write(Record{7}));
  checks.require(reused.write(Record{8}));
  checks.require(reused.commit());
  checks.require(outbox.try_pop(record) && record == Record{7});
  checks.require(outbox.try_pop(record) && record == Record{8});
  checks.require(outbox.validate_invariants());
}

void test_abort_and_abandon_restore_capacity(Checks& checks) {
  lob::LosslessOutbox<Record> outbox(4);
  {
    auto reservation = outbox.reserve(3);
    checks.require(reservation.valid());
    checks.require(reservation.write(Record{1}));
    checks.require(outbox.available_capacity() == 1);
    checks.require(reservation.abort());
    checks.require(!reservation.abort());
  }
  checks.require(outbox.empty() && outbox.available_capacity() == 4);

  {
    auto abandoned = outbox.reserve(4);
    checks.require(abandoned.valid());
    checks.require(abandoned.write(Record{9}));
    checks.require(outbox.available_capacity() == 0);
  }
  checks.require(outbox.empty() && outbox.available_capacity() == 4);

  auto incomplete = outbox.reserve(2);
  checks.require(incomplete.write(Record{20}));
  checks.require(!incomplete.commit());
  checks.require(outbox.empty() && outbox.available_capacity() == 2);
  checks.require(incomplete.abort());
  checks.require(outbox.available_capacity() == 4);
  checks.require(outbox.validate_invariants());
}

void test_system_status_invisible_until_commit(Checks& checks) {
  lob::LosslessOutbox<lob::SystemStatus> outbox(2);
  const auto instrument =
      lob::checked_domain_cast<lob::InstrumentId>(std::uint32_t{7});
  const auto sequence =
      lob::checked_domain_cast<lob::EngineSequence>(std::uint64_t{1});
  checks.require(instrument.has_value() && sequence.has_value());
  const lob::SystemStatus expected{lob::StatusScope::Instrument,
                                   instrument.value,
                                   lob::InstrumentState::Active,
                                   lob::InstrumentState::Halted,
                                   lob::StatusEventKind::StateTransition,
                                   lob::StatusReason::TradingHalt,
                                   sequence.value};
  auto reservation = outbox.reserve(1);
  checks.require(reservation.valid() && reservation.write(expected));

  lob::SystemStatus observed;
  checks.require(!outbox.try_peek(observed));
  checks.require(!outbox.try_pop(observed));
  checks.require(reservation.commit());
  checks.require(outbox.try_pop(observed));
  checks.require(observed.scope == expected.scope &&
                 observed.instrument_id == expected.instrument_id &&
                 observed.previous_state == expected.previous_state &&
                 observed.resulting_state == expected.resulting_state &&
                 observed.kind == expected.kind &&
                 observed.reason == expected.reason &&
                 observed.engine_sequence == expected.engine_sequence);
}

}  // namespace

int main() {
  Checks checks;
  test_empty_and_zero_reservation(checks);
  test_visibility_and_single_commit(checks);
  test_exact_capacity_and_insufficient_by_one(checks);
  test_partial_occupancy_wraparound_and_reuse(checks);
  test_abort_and_abandon_restore_capacity(checks);
  test_system_status_invisible_until_commit(checks);
  return checks.passed() ? 0 : 1;
}
