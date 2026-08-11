#include "lob/storage/order_book_storage.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using lob::DepthEntry;
using lob::InstrumentId;
using lob::OrderBookResult;
using lob::OrderBookStorage;
using lob::OrderId;
using lob::PriceTicks;
using lob::Quantity;
using lob::RestingOrderView;
using lob::Side;
using lob::StorageLimits;

static_assert(!std::is_reference_v<decltype(
              std::declval<const OrderBookStorage&>().find_order(OrderId{}))>);
static_assert(!std::is_reference_v<decltype(
              std::declval<const OrderBookStorage&>().depth(Side::Buy))>);
static_assert(!std::is_reference_v<decltype(
              std::declval<const OrderBookStorage&>().orders_at_level(
                  Side::Buy, PriceTicks{}))>);
static_assert(std::is_same_v<decltype(
                  std::declval<const OrderBookStorage&>().find_order(OrderId{})),
              std::optional<RestingOrderView>>);
static_assert(lob::kMaximumActiveOrders == 131'072);
static_assert(lob::kMaximumPriceLevelsPerSide == 4'096);

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

void require_invariants(Checks& checks, const OrderBookStorage& book) noexcept {
  checks.require(book.validate_invariants());
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

LogicalState logical_state(const OrderBookStorage& book) {
  LogicalState state;
  state.active_orders = book.active_order_count();
  state.bids = book.depth(Side::Buy);
  state.asks = book.depth(Side::Sell);
  for (const auto& level : state.bids) {
    const auto orders = book.orders_at_level(Side::Buy, level.price);
    state.bid_orders.insert(state.bid_orders.end(), orders.begin(), orders.end());
  }
  for (const auto& level : state.asks) {
    const auto orders = book.orders_at_level(Side::Sell, level.price);
    state.ask_orders.insert(state.ask_orders.end(), orders.begin(), orders.end());
  }
  return state;
}

void test_empty_book(Checks& checks) {
  const auto instrument = instrument_id(checks, 1);
  const OrderBookStorage book(instrument);

  checks.require(book.instrument_id() == instrument);
  checks.require(book.active_order_count() == 0);
  checks.require(book.price_level_count(Side::Buy) == 0);
  checks.require(book.price_level_count(Side::Sell) == 0);
  checks.require(!book.best_bid().has_value());
  checks.require(!book.best_ask().has_value());
  checks.require(book.depth(Side::Buy).empty());
  checks.require(book.depth(Side::Sell).empty());
  checks.require(book.orders_at_level(Side::Buy, PriceTicks{}).empty());
  require_invariants(checks, book);
}

void test_single_orders(Checks& checks) {
  const auto instrument = instrument_id(checks, 2);
  const auto bid_id = order_id(checks, 10);
  const auto ask_id = order_id(checks, 11);
  const auto bid_price = price(checks, 100);
  const auto ask_price = price(checks, 105);
  const auto bid_quantity = quantity(checks, 7);
  const auto ask_quantity = quantity(checks, 9);
  OrderBookStorage book(instrument);

  checks.require(book.insert_resting(bid_id, instrument, Side::Buy, bid_price,
                                     bid_quantity) == OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(ask_id, instrument, Side::Sell, ask_price,
                                     ask_quantity) == OrderBookResult::Accepted);
  require_invariants(checks, book);

  checks.require(book.active_order_count() == 2);
  checks.require(book.best_bid() == std::optional<PriceTicks>{bid_price});
  checks.require(book.best_ask() == std::optional<PriceTicks>{ask_price});
  checks.require(book.find_order(bid_id) == std::optional<RestingOrderView>{
                                                {bid_id, instrument, Side::Buy,
                                                 bid_price, bid_quantity}});
  checks.require(book.find_order(ask_id) == std::optional<RestingOrderView>{
                                                {ask_id, instrument, Side::Sell,
                                                 ask_price, ask_quantity}});
  checks.require(book.level(Side::Buy, bid_price) ==
                 std::optional<DepthEntry>{{bid_price, bid_quantity, 1}});
  checks.require(book.level(Side::Sell, ask_price) ==
                 std::optional<DepthEntry>{{ask_price, ask_quantity, 1}});
}

void test_fifo_and_aggregate(Checks& checks) {
  const auto instrument = instrument_id(checks, 3);
  const auto level_price = price(checks, 101);
  OrderBookStorage book(instrument);
  std::vector<OrderId> expected;
  std::uint64_t expected_aggregate = 0;

  for (std::uint64_t value = 1; value <= 4; ++value) {
    const auto id = order_id(checks, value);
    const auto leaves = quantity(checks, value * 10);
    checks.require(book.insert_resting(id, instrument, Side::Buy, level_price,
                                       leaves) == OrderBookResult::Accepted);
    expected.push_back(id);
    expected_aggregate += leaves.value();
    require_invariants(checks, book);
  }

  checks.require(book.price_level_count(Side::Buy) == 1);
  checks.require(fifo_ids(book.orders_at_level(Side::Buy, level_price)) ==
                 expected);
  const auto level = book.level(Side::Buy, level_price);
  checks.require(level.has_value());
  checks.require(level->aggregate_leaves_quantity.value() == expected_aggregate);
  checks.require(level->order_count == expected.size());
  for (const auto id : expected) {
    checks.require(book.find_order(id).has_value());
  }
}

void test_price_ordering_and_mixed_book(Checks& checks) {
  const auto instrument = instrument_id(checks, 4);
  OrderBookStorage book(instrument);

  const std::vector<std::int64_t> bid_prices{100, 103, 101};
  const std::vector<std::int64_t> ask_prices{108, 105, 107};
  std::uint64_t id_value = 1;
  for (const auto value : bid_prices) {
    const auto id = order_id(checks, id_value);
    const auto leaves = quantity(checks, id_value + 1);
    ++id_value;
    checks.require(book.insert_resting(id, instrument, Side::Buy,
                                       price(checks, value), leaves) ==
                   OrderBookResult::Accepted);
    require_invariants(checks, book);
  }
  for (const auto value : ask_prices) {
    const auto id = order_id(checks, id_value);
    const auto leaves = quantity(checks, id_value + 1);
    ++id_value;
    checks.require(book.insert_resting(id, instrument, Side::Sell,
                                       price(checks, value), leaves) ==
                   OrderBookResult::Accepted);
    require_invariants(checks, book);
  }

  const auto bids = book.depth(Side::Buy);
  const auto asks = book.depth(Side::Sell);
  checks.require(bids.size() == 3 && bids[0].price.value() == 103 &&
                 bids[1].price.value() == 101 && bids[2].price.value() == 100);
  checks.require(asks.size() == 3 && asks[0].price.value() == 105 &&
                 asks[1].price.value() == 107 && asks[2].price.value() == 108);
  checks.require(book.best_bid()->value() == 103);
  checks.require(book.best_ask()->value() == 105);
  checks.require(book.price_level_count(Side::Buy) == 3);
  checks.require(book.price_level_count(Side::Sell) == 3);
  checks.require(book.active_order_count() == 6);
}

void test_removal_positions(Checks& checks) {
  const auto instrument = instrument_id(checks, 5);
  const auto main_price = price(checks, 100);
  const auto other_bid_price = price(checks, 99);
  const auto ask_price = price(checks, 110);
  OrderBookStorage book(instrument);

  const auto one = order_id(checks, 1);
  const auto two = order_id(checks, 2);
  const auto three = order_id(checks, 3);
  const auto four = order_id(checks, 4);
  const auto other = order_id(checks, 5);
  const auto ask = order_id(checks, 6);
  for (const auto id : {one, two, three, four}) {
    checks.require(book.insert_resting(id, instrument, Side::Buy, main_price,
                                       quantity(checks, id.value())) ==
                   OrderBookResult::Accepted);
    require_invariants(checks, book);
  }
  checks.require(book.insert_resting(other, instrument, Side::Buy,
                                     other_bid_price, quantity(checks, 10)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(ask, instrument, Side::Sell, ask_price,
                                     quantity(checks, 20)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);

  checks.require(book.remove_resting(one) == OrderBookResult::Accepted);
  checks.require(fifo_ids(book.orders_at_level(Side::Buy, main_price)) ==
                 std::vector<OrderId>{two, three, four});
  checks.require(book.level(Side::Buy, main_price)
                     ->aggregate_leaves_quantity.value() == 9);
  checks.require(book.best_bid() == std::optional<PriceTicks>{main_price});
  checks.require(!book.find_order(one).has_value());
  require_invariants(checks, book);

  checks.require(book.remove_resting(three) == OrderBookResult::Accepted);
  checks.require(fifo_ids(book.orders_at_level(Side::Buy, main_price)) ==
                 std::vector<OrderId>{two, four});
  checks.require(book.level(Side::Buy, main_price)
                     ->aggregate_leaves_quantity.value() == 6);
  checks.require(book.best_bid() == std::optional<PriceTicks>{main_price});
  checks.require(!book.find_order(three).has_value());
  require_invariants(checks, book);

  checks.require(book.remove_resting(four) == OrderBookResult::Accepted);
  checks.require(fifo_ids(book.orders_at_level(Side::Buy, main_price)) ==
                 std::vector<OrderId>{two});
  checks.require(book.level(Side::Buy, main_price)
                     ->aggregate_leaves_quantity.value() == 2);
  checks.require(book.best_bid() == std::optional<PriceTicks>{main_price});
  checks.require(!book.find_order(four).has_value());
  require_invariants(checks, book);

  checks.require(book.remove_resting(two) == OrderBookResult::Accepted);
  checks.require(!book.level(Side::Buy, main_price).has_value());
  checks.require(book.best_bid() == std::optional<PriceTicks>{other_bid_price});
  checks.require(book.find_order(other).has_value());
  checks.require(book.find_order(ask).has_value());
  checks.require(book.best_ask() == std::optional<PriceTicks>{ask_price});
  require_invariants(checks, book);
}

void test_fill_reduction(Checks& checks) {
  const auto instrument = instrument_id(checks, 14);
  const auto level_price = price(checks, 100);
  const auto first = order_id(checks, 1);
  const auto second = order_id(checks, 2);
  OrderBookStorage book(instrument);

  checks.require(book.insert_resting(first, instrument, Side::Sell, level_price,
                                     quantity(checks, 10)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(second, instrument, Side::Sell, level_price,
                                     quantity(checks, 5)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);

  checks.require(book.reduce_resting_by(first, quantity(checks, 4)) ==
                 OrderBookResult::Accepted);
  checks.require(book.find_order(first)->leaves_quantity == quantity(checks, 6));
  checks.require(fifo_ids(book.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{first, second});
  checks.require(book.level(Side::Sell, level_price)
                     ->aggregate_leaves_quantity == quantity(checks, 11));
  require_invariants(checks, book);

  const auto before_invalid = logical_state(book);
  checks.require(book.reduce_resting_by(first, quantity(checks, 7)) ==
                 OrderBookResult::InvalidQuantity);
  checks.require(logical_state(book) == before_invalid);
  require_invariants(checks, book);

  checks.require(book.reduce_resting_by(first, quantity(checks, 6)) ==
                 OrderBookResult::Accepted);
  checks.require(!book.find_order(first));
  checks.require(fifo_ids(book.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{second});
  checks.require(book.level(Side::Sell, level_price) ==
                 std::optional<DepthEntry>{
                     {level_price, quantity(checks, 5), 1}});
  require_invariants(checks, book);
}

void test_duplicate_and_reuse(Checks& checks) {
  const auto instrument = instrument_id(checks, 6);
  const auto id = order_id(checks, 77);
  const auto first_price = price(checks, 100);
  const auto second_price = price(checks, 200);
  OrderBookStorage book(instrument);

  checks.require(book.insert_resting(id, instrument, Side::Buy, first_price,
                                     quantity(checks, 10)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  const auto before_duplicate = logical_state(book);
  checks.require(book.insert_resting(id, instrument, Side::Sell, second_price,
                                     quantity(checks, 30)) ==
                 OrderBookResult::DuplicateOrderId);
  checks.require(logical_state(book) == before_duplicate);
  require_invariants(checks, book);

  checks.require(book.remove_resting(id) == OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(id, instrument, Side::Sell, second_price,
                                     quantity(checks, 30)) ==
                 OrderBookResult::Accepted);
  checks.require(book.find_order(id)->side == Side::Sell);
  checks.require(book.find_order(id)->price == second_price);
  require_invariants(checks, book);
}

void test_active_order_capacity(Checks& checks) {
  const auto instrument = instrument_id(checks, 7);
  const auto level_price = price(checks, 100);
  OrderBookStorage book(instrument, StorageLimits{2, 2});
  const auto one = order_id(checks, 1);
  const auto two = order_id(checks, 2);
  const auto three = order_id(checks, 3);

  checks.require(book.insert_resting(one, instrument, Side::Buy, level_price,
                                     quantity(checks, 1)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(two, instrument, Side::Buy, level_price,
                                     quantity(checks, 2)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  const auto full_state = logical_state(book);
  checks.require(book.insert_resting(three, instrument, Side::Buy, level_price,
                                     quantity(checks, 3)) ==
                 OrderBookResult::CapacityExhausted);
  checks.require(logical_state(book) == full_state);
  require_invariants(checks, book);

  checks.require(book.remove_resting(one) == OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(three, instrument, Side::Buy, level_price,
                                     quantity(checks, 3)) ==
                 OrderBookResult::Accepted);
  checks.require(book.active_order_count() == 2);
  require_invariants(checks, book);
}

void test_level_capacity(Checks& checks, Side side) {
  const auto instrument = instrument_id(checks, side == Side::Buy ? 8 : 9);
  OrderBookStorage book(instrument, StorageLimits{8, 2});
  const auto first_price = price(checks, 100);
  const auto second_price = price(checks, 101);
  const auto third_price = price(checks, 102);
  const auto first = order_id(checks, 1);
  const auto second = order_id(checks, 2);
  const auto existing = order_id(checks, 3);
  const auto rejected = order_id(checks, 4);

  checks.require(book.insert_resting(first, instrument, side, first_price,
                                     quantity(checks, 1)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(second, instrument, side, second_price,
                                     quantity(checks, 2)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(existing, instrument, side, first_price,
                                     quantity(checks, 3)) ==
                 OrderBookResult::Accepted);
  require_invariants(checks, book);
  const auto full_state = logical_state(book);
  checks.require(book.insert_resting(rejected, instrument, side, third_price,
                                     quantity(checks, 4)) ==
                 OrderBookResult::CapacityExhausted);
  checks.require(logical_state(book) == full_state);
  require_invariants(checks, book);

  checks.require(book.remove_resting(second) == OrderBookResult::Accepted);
  require_invariants(checks, book);
  checks.require(book.insert_resting(rejected, instrument, side, third_price,
                                     quantity(checks, 4)) ==
                 OrderBookResult::Accepted);
  checks.require(book.price_level_count(side) == 2);
  require_invariants(checks, book);
}

void test_aggregate_overflow(Checks& checks) {
  const auto instrument = instrument_id(checks, 10);
  const auto level_price = price(checks, 100);
  OrderBookStorage book(instrument, StorageLimits{3, 2});
  const auto first = order_id(checks, 1);
  const auto second = order_id(checks, 2);
  const auto maximum = quantity(checks, std::numeric_limits<std::uint64_t>::max());

  checks.require(book.insert_resting(first, instrument, Side::Buy, level_price,
                                     maximum) == OrderBookResult::Accepted);
  require_invariants(checks, book);
  const auto before_overflow = logical_state(book);
  checks.require(book.insert_resting(second, instrument, Side::Buy, level_price,
                                     quantity(checks, 1)) ==
                 OrderBookResult::CapacityExhausted);
  checks.require(logical_state(book) == before_overflow);
  require_invariants(checks, book);
}

void test_invalid_and_unknown_inputs(Checks& checks) {
  const auto instrument = instrument_id(checks, 11);
  const auto other_instrument = instrument_id(checks, 12);
  const auto valid_id = order_id(checks, 1);
  const auto valid_price = price(checks, 100);
  const auto valid_quantity = quantity(checks, 1);
  OrderBookStorage book(instrument);

  checks.require(book.insert_resting(valid_id, other_instrument, Side::Buy,
                                     valid_price, valid_quantity) ==
                 OrderBookResult::InvalidInstrument);
  require_invariants(checks, book);
  checks.require(book.insert_resting(valid_id, instrument, Side::Invalid,
                                     valid_price, valid_quantity) ==
                 OrderBookResult::InvalidSide);
  require_invariants(checks, book);
  checks.require(book.insert_resting(valid_id, instrument, Side::Buy,
                                     PriceTicks{}, valid_quantity) ==
                 OrderBookResult::InvalidPrice);
  require_invariants(checks, book);
  checks.require(book.insert_resting(valid_id, instrument, Side::Buy,
                                     valid_price, Quantity{}) ==
                 OrderBookResult::InvalidQuantity);
  require_invariants(checks, book);
  checks.require(book.remove_resting(valid_id) ==
                 OrderBookResult::OrderNotFound);
  checks.require(book.active_order_count() == 0);
  require_invariants(checks, book);
}

void test_query_values_are_copies(Checks& checks) {
  const auto instrument = instrument_id(checks, 13);
  const auto id = order_id(checks, 1);
  const auto level_price = price(checks, 100);
  const auto leaves = quantity(checks, 10);
  OrderBookStorage book(instrument);
  checks.require(book.insert_resting(id, instrument, Side::Buy, level_price,
                                     leaves) == OrderBookResult::Accepted);
  require_invariants(checks, book);

  auto order = book.find_order(id);
  auto depth = book.depth(Side::Buy);
  order->side = Side::Sell;
  depth[0].order_count = 99;

  checks.require(book.find_order(id)->side == Side::Buy);
  checks.require(book.level(Side::Buy, level_price)->order_count == 1);
  require_invariants(checks, book);
}

}  // namespace

int main() {
  Checks checks;
  test_empty_book(checks);
  test_single_orders(checks);
  test_fifo_and_aggregate(checks);
  test_price_ordering_and_mixed_book(checks);
  test_removal_positions(checks);
  test_fill_reduction(checks);
  test_duplicate_and_reuse(checks);
  test_active_order_capacity(checks);
  test_level_capacity(checks, Side::Buy);
  test_level_capacity(checks, Side::Sell);
  test_aggregate_overflow(checks);
  test_invalid_and_unknown_inputs(checks);
  test_query_values_are_copies(checks);
  return checks.passed() ? 0 : 1;
}
