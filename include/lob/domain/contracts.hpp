#pragma once

#include "lob/domain/types.hpp"

#include <cstdint>
#include <type_traits>

namespace lob {

enum class Side : std::uint8_t {
  Invalid = 0,
  Buy = 1,
  Sell = 2,
};

enum class CommandKind : std::uint8_t {
  Invalid = 0,
  New = 1,
  Cancel = 2,
  Amend = 3,
};

enum class InstrumentState : std::uint8_t {
  Invalid = 0,
  Active = 1,
  Halted = 2,
  Closed = 3,
};

enum class OrderBookResult : std::uint8_t {
  Accepted = 0,
  OrderNotFound = 1,
  DuplicateOrderId = 2,
  InvalidInstrument = 3,
  MarketHalted = 4,
  InstrumentUnavailable = 5,
  InvalidSide = 6,
  InvalidPrice = 7,
  InvalidQuantity = 8,
  InvalidAmendment = 9,
  CapacityExhausted = 10,
  LosslessOutboxFull = 11,
  ChannelUnavailable = 12,
  SnapshotRequired = 13,
};

enum class StatusScope : std::uint8_t {
  Invalid = 0,
  Instrument = 1,
  MatchingEngine = 2,
};

enum class StatusEventKind : std::uint8_t {
  Invalid = 0,
  StateTransition = 1,
  Reset = 2,
};

enum class StatusReason : std::uint8_t {
  Invalid = 0,
  TradingHalt = 1,
  TradingResume = 2,
  EndOfDay = 3,
  LosslessOutboxFull = 4,
};

struct NewOrder final {
  OrderId order_id{};
  InstrumentId instrument_id{};
  Side side{Side::Invalid};
  PriceTicks limit_price{};
  Quantity quantity{};
};

struct CancelOrder final {
  OrderId order_id{};
  InstrumentId instrument_id{};
};

struct AmendOrder final {
  OrderId order_id{};
  InstrumentId instrument_id{};
  PriceTicks new_price{};
  Quantity new_leaves_quantity{};
};

struct ExecutionReport final {
  MatchId match_id{};
  InstrumentId instrument_id{};
  OrderId aggressive_order_id{};
  OrderId resting_order_id{};
  PriceTicks match_price{};
  Quantity match_quantity{};
  EngineSequence engine_sequence{};
};

struct SystemStatus final {
  StatusScope scope{StatusScope::Invalid};
  InstrumentId instrument_id{};
  InstrumentState previous_state{InstrumentState::Invalid};
  InstrumentState resulting_state{InstrumentState::Invalid};
  StatusEventKind kind{StatusEventKind::Invalid};
  StatusReason reason{StatusReason::Invalid};
  EngineSequence engine_sequence{};
};

static_assert(sizeof(std::underlying_type_t<Side>) == sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<CommandKind>) ==
              sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<InstrumentState>) ==
              sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<OrderBookResult>) ==
              sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<StatusScope>) ==
              sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<StatusEventKind>) ==
              sizeof(std::uint8_t));
static_assert(sizeof(std::underlying_type_t<StatusReason>) ==
              sizeof(std::uint8_t));

static_assert(std::is_trivially_copyable_v<NewOrder>);
static_assert(std::is_trivially_copyable_v<CancelOrder>);
static_assert(std::is_trivially_copyable_v<AmendOrder>);
static_assert(std::is_trivially_copyable_v<ExecutionReport>);
static_assert(std::is_trivially_copyable_v<SystemStatus>);
static_assert(std::is_standard_layout_v<NewOrder>);
static_assert(std::is_standard_layout_v<CancelOrder>);
static_assert(std::is_standard_layout_v<AmendOrder>);
static_assert(std::is_standard_layout_v<ExecutionReport>);
static_assert(std::is_standard_layout_v<SystemStatus>);

}  // namespace lob
