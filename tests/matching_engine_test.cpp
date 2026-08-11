#include "lob/matching/matching_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace {

using lob::CommandSequence;
using lob::DepthEntry;
using lob::EngineSequence;
using lob::ExecutionReport;
using lob::InstrumentId;
using lob::MatchId;
using lob::MatchingEngine;
using lob::NewOrder;
using lob::NewOrderResult;
using lob::OrderBookResult;
using lob::OrderId;
using lob::PriceTicks;
using lob::Quantity;
using lob::RestingOrderView;
using lob::Side;
using lob::StorageLimits;

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

NewOrderResult submit(Checks& checks, MatchingEngine& engine, OrderId id,
                      Side side, PriceTicks limit, Quantity order_quantity) {
  const auto result = engine.process(
      NewOrder{id, engine.instrument_id(), side, limit, order_quantity});
  checks.require(engine.validate_invariants());
  return result;
}

bool report_equals(const ExecutionReport& report, MatchId match_id,
                   InstrumentId instrument, OrderId aggressive,
                   OrderId resting, PriceTicks match_price,
                   Quantity match_quantity,
                   EngineSequence engine_sequence) noexcept {
  return report.match_id == match_id && report.instrument_id == instrument &&
         report.aggressive_order_id == aggressive &&
         report.resting_order_id == resting &&
         report.match_price == match_price &&
         report.match_quantity == match_quantity &&
         report.engine_sequence == engine_sequence;
}

bool reports_equal(std::span<const ExecutionReport> left,
                   std::span<const ExecutionReport> right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (!report_equals(left[index], right[index].match_id,
                       right[index].instrument_id,
                       right[index].aggressive_order_id,
                       right[index].resting_order_id,
                       right[index].match_price,
                       right[index].match_quantity,
                       right[index].engine_sequence)) {
      return false;
    }
  }
  return true;
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

void test_non_marketable_resting(Checks& checks) {
  const auto instrument = instrument_id(checks, 1);
  const auto bid_price = price(checks, 104);
  const auto ask_price = price(checks, 105);
  const auto bid_one = order_id(checks, 1);
  const auto ask_one = order_id(checks, 2);
  const auto bid_two = order_id(checks, 3);
  const auto ask_two = order_id(checks, 4);
  MatchingEngine engine(instrument);

  const auto first_bid = submit(checks, engine, bid_one, Side::Buy, bid_price,
                                quantity(checks, 3));
  checks.require(first_bid.result == OrderBookResult::Accepted &&
                 first_bid.reports().empty());
  const auto first_ask = submit(checks, engine, ask_one, Side::Sell, ask_price,
                                quantity(checks, 5));
  checks.require(first_ask.result == OrderBookResult::Accepted &&
                 first_ask.reports().empty());
  const auto second_bid = submit(checks, engine, bid_two, Side::Buy, bid_price,
                                 quantity(checks, 7));
  const auto second_ask = submit(checks, engine, ask_two, Side::Sell, ask_price,
                                 quantity(checks, 11));

  checks.require(second_bid.reports().empty() && second_ask.reports().empty());
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, bid_price)) ==
                 std::vector<OrderId>{bid_one, bid_two});
  checks.require(fifo_ids(engine.orders_at_level(Side::Sell, ask_price)) ==
                 std::vector<OrderId>{ask_one, ask_two});
  checks.require(engine.level(Side::Buy, bid_price) ==
                 std::optional<DepthEntry>{
                     {bid_price, quantity(checks, 10), 2}});
  checks.require(engine.level(Side::Sell, ask_price) ==
                 std::optional<DepthEntry>{
                     {ask_price, quantity(checks, 16), 2}});
  checks.require(engine.best_bid() == std::optional<PriceTicks>{bid_price});
  checks.require(engine.best_ask() == std::optional<PriceTicks>{ask_price});
  checks.require(engine.last_command_sequence().value() == 4);
  checks.require(engine.last_engine_sequence().value() == 0);
}

void test_exact_cross_and_resting_price(Checks& checks) {
  const auto instrument = instrument_id(checks, 2);
  const auto resting_price = price(checks, 100);
  const auto better_buy_price = price(checks, 110);
  const auto resting = order_id(checks, 1);
  const auto aggressive = order_id(checks, 2);
  MatchingEngine buy_engine(instrument);

  checks.require(submit(checks, buy_engine, resting, Side::Sell, resting_price,
                        quantity(checks, 10))
                     .result == OrderBookResult::Accepted);
  const auto buy = submit(checks, buy_engine, aggressive, Side::Buy,
                          better_buy_price, quantity(checks, 10));
  checks.require(buy.result == OrderBookResult::Accepted &&
                 buy.reports().size() == 1);
  checks.require(report_equals(
      buy.reports()[0], make_domain<MatchId>(checks, std::uint64_t{1}),
      instrument, aggressive, resting, resting_price, quantity(checks, 10),
      make_domain<EngineSequence>(checks, std::uint64_t{1})));
  checks.require(buy_engine.active_order_count() == 0);

  const auto sell_instrument = instrument_id(checks, 3);
  const auto resting_bid = order_id(checks, 10);
  const auto aggressive_sell = order_id(checks, 11);
  const auto better_sell_price = price(checks, 90);
  MatchingEngine sell_engine(sell_instrument);
  checks.require(submit(checks, sell_engine, resting_bid, Side::Buy,
                        resting_price, quantity(checks, 8))
                     .result == OrderBookResult::Accepted);
  const auto sell = submit(checks, sell_engine, aggressive_sell, Side::Sell,
                           better_sell_price, quantity(checks, 8));
  checks.require(sell.reports().size() == 1);
  checks.require(sell.reports()[0].match_price == resting_price);
  checks.require(sell.reports()[0].resting_order_id == resting_bid);
  checks.require(sell_engine.active_order_count() == 0);
}

void test_one_tick_non_crossing(Checks& checks) {
  const auto instrument = instrument_id(checks, 4);
  MatchingEngine engine(instrument);
  const auto bid = order_id(checks, 1);
  const auto ask = order_id(checks, 2);
  const auto second_ask = order_id(checks, 3);
  const auto best_bid = price(checks, 99);
  const auto best_ask = price(checks, 100);

  checks.require(submit(checks, engine, ask, Side::Sell, best_ask,
                        quantity(checks, 1))
                     .reports()
                     .empty());
  checks.require(submit(checks, engine, bid, Side::Buy, best_bid,
                        quantity(checks, 1))
                     .reports()
                     .empty());
  checks.require(submit(checks, engine, second_ask, Side::Sell, best_ask,
                        quantity(checks, 1))
                     .reports()
                     .empty());
  checks.require(engine.find_order(bid).has_value());
  checks.require(engine.find_order(ask).has_value());
  checks.require(engine.find_order(second_ask).has_value());
}

void test_partial_and_full_resting_fills(Checks& checks) {
  const auto instrument = instrument_id(checks, 5);
  const auto level_price = price(checks, 100);
  const auto first = order_id(checks, 1);
  const auto second = order_id(checks, 2);
  MatchingEngine engine(instrument);

  checks.require(submit(checks, engine, first, Side::Sell, level_price,
                        quantity(checks, 20))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, second, Side::Sell, level_price,
                        quantity(checks, 7))
                     .result == OrderBookResult::Accepted);
  const auto partial = submit(checks, engine, order_id(checks, 10), Side::Buy,
                              price(checks, 101), quantity(checks, 5));
  checks.require(partial.reports().size() == 1);
  checks.require(partial.reports()[0].resting_order_id == first);
  checks.require(engine.find_order(first)->leaves_quantity ==
                 quantity(checks, 15));
  checks.require(fifo_ids(engine.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{first, second});
  checks.require(engine.level(Side::Sell, level_price)
                     ->aggregate_leaves_quantity == quantity(checks, 22));

  const auto full = submit(checks, engine, order_id(checks, 11), Side::Buy,
                           level_price, quantity(checks, 15));
  checks.require(full.reports().size() == 1 &&
                 full.reports()[0].resting_order_id == first);
  checks.require(!engine.find_order(first));
  checks.require(engine.find_order(second).has_value());
  checks.require(fifo_ids(engine.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{second});
}

void test_aggressive_remainder(Checks& checks) {
  const auto instrument = instrument_id(checks, 6);
  const auto resting = order_id(checks, 1);
  const auto aggressive = order_id(checks, 2);
  const auto ask_price = price(checks, 100);
  const auto incoming_price = price(checks, 101);
  MatchingEngine engine(instrument);

  checks.require(submit(checks, engine, resting, Side::Sell, ask_price,
                        quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  const auto result = submit(checks, engine, aggressive, Side::Buy,
                             incoming_price, quantity(checks, 20));
  checks.require(result.reports().size() == 1);
  checks.require(result.reports()[0].match_quantity == quantity(checks, 5));
  checks.require(!engine.find_order(resting));
  checks.require(engine.find_order(aggressive) ==
                 std::optional<RestingOrderView>{{aggressive, instrument,
                                                  Side::Buy, incoming_price,
                                                  quantity(checks, 15)}});
  checks.require(engine.best_bid() ==
                 std::optional<PriceTicks>{incoming_price});
  checks.require(!engine.best_ask());

  const auto sell_instrument = instrument_id(checks, 16);
  MatchingEngine sell_engine(sell_instrument);
  const auto bid = order_id(checks, 10);
  const auto sell = order_id(checks, 11);
  checks.require(submit(checks, sell_engine, bid, Side::Buy, ask_price,
                        quantity(checks, 4))
                     .result == OrderBookResult::Accepted);
  const auto sell_result = submit(checks, sell_engine, sell, Side::Sell,
                                  price(checks, 99), quantity(checks, 9));
  checks.require(sell_result.reports().size() == 1);
  checks.require(sell_result.reports()[0].match_price == ask_price);
  checks.require(sell_engine.find_order(sell)->leaves_quantity ==
                 quantity(checks, 5));
  checks.require(sell_engine.find_order(sell)->price == price(checks, 99));
}

void test_fifo_sweep(Checks& checks) {
  const auto instrument = instrument_id(checks, 7);
  const auto level_price = price(checks, 100);
  const auto first = order_id(checks, 1);
  const auto second = order_id(checks, 2);
  const auto third = order_id(checks, 3);
  MatchingEngine engine(instrument);

  checks.require(submit(checks, engine, first, Side::Sell, level_price,
                        quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, second, Side::Sell, level_price,
                        quantity(checks, 7))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, third, Side::Sell, level_price,
                        quantity(checks, 10))
                     .result == OrderBookResult::Accepted);
  const auto result = submit(checks, engine, order_id(checks, 10), Side::Buy,
                             level_price, quantity(checks, 15));

  checks.require(result.reports().size() == 3);
  checks.require(result.reports()[0].resting_order_id == first &&
                 result.reports()[0].match_quantity == quantity(checks, 5));
  checks.require(result.reports()[1].resting_order_id == second &&
                 result.reports()[1].match_quantity == quantity(checks, 7));
  checks.require(result.reports()[2].resting_order_id == third &&
                 result.reports()[2].match_quantity == quantity(checks, 3));
  checks.require(!engine.find_order(first) && !engine.find_order(second));
  checks.require(engine.find_order(third)->leaves_quantity ==
                 quantity(checks, 7));
  checks.require(fifo_ids(engine.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{third});
  checks.require(engine.level(Side::Sell, level_price) ==
                 std::optional<DepthEntry>{
                     {level_price, quantity(checks, 7), 1}});
}

void test_buy_multi_level_limit_stop(Checks& checks) {
  const auto instrument = instrument_id(checks, 8);
  MatchingEngine engine(instrument);
  const auto ask_100_a = order_id(checks, 1);
  const auto ask_100_b = order_id(checks, 2);
  const auto ask_101 = order_id(checks, 3);
  const auto ask_103 = order_id(checks, 4);
  const auto price_100 = price(checks, 100);
  const auto price_101 = price(checks, 101);
  const auto price_103 = price(checks, 103);

  checks.require(submit(checks, engine, ask_101, Side::Sell, price_101,
                        quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask_100_a, Side::Sell, price_100,
                        quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask_103, Side::Sell, price_103,
                        quantity(checks, 9))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask_100_b, Side::Sell, price_100,
                        quantity(checks, 1))
                     .result == OrderBookResult::Accepted);

  const auto aggressive = order_id(checks, 10);
  const auto result = submit(checks, engine, aggressive, Side::Buy, price_101,
                             quantity(checks, 10));
  checks.require(result.reports().size() == 3);
  checks.require(result.reports()[0].resting_order_id == ask_100_a);
  checks.require(result.reports()[1].resting_order_id == ask_100_b);
  checks.require(result.reports()[2].resting_order_id == ask_101);
  checks.require(result.reports()[0].match_price == price_100 &&
                 result.reports()[1].match_price == price_100 &&
                 result.reports()[2].match_price == price_101);
  checks.require(engine.find_order(ask_103)->leaves_quantity ==
                 quantity(checks, 9));
  checks.require(engine.find_order(aggressive)->leaves_quantity ==
                 quantity(checks, 4));
  checks.require(engine.find_order(aggressive)->price == price_101);
  checks.require(engine.best_bid() == std::optional<PriceTicks>{price_101});
  checks.require(engine.best_ask() == std::optional<PriceTicks>{price_103});
}

void test_sell_multi_level_and_exact_consumption(Checks& checks) {
  const auto instrument = instrument_id(checks, 9);
  MatchingEngine engine(instrument);
  const auto price_100 = price(checks, 100);
  const auto price_99 = price(checks, 99);
  const auto price_97 = price(checks, 97);
  const auto bid_100 = order_id(checks, 1);
  const auto bid_99_a = order_id(checks, 2);
  const auto bid_99_b = order_id(checks, 3);
  const auto bid_97 = order_id(checks, 4);

  checks.require(submit(checks, engine, bid_99_a, Side::Buy, price_99,
                        quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, bid_100, Side::Buy, price_100,
                        quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, bid_97, Side::Buy, price_97,
                        quantity(checks, 8))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, bid_99_b, Side::Buy, price_99,
                        quantity(checks, 3))
                     .result == OrderBookResult::Accepted);

  const auto aggressive = order_id(checks, 10);
  const auto result = submit(checks, engine, aggressive, Side::Sell, price_99,
                             quantity(checks, 6));
  checks.require(result.reports().size() == 3);
  checks.require(result.reports()[0].resting_order_id == bid_100);
  checks.require(result.reports()[1].resting_order_id == bid_99_a);
  checks.require(result.reports()[2].resting_order_id == bid_99_b);
  checks.require(!engine.find_order(aggressive));
  checks.require(engine.find_order(bid_97).has_value());
  checks.require(engine.best_bid() == std::optional<PriceTicks>{price_97});
  checks.require(!engine.best_ask());
}

void test_duplicate_and_id_reuse(Checks& checks) {
  const auto instrument = instrument_id(checks, 10);
  MatchingEngine engine(instrument);
  const auto reusable = order_id(checks, 1);
  const auto ask_price = price(checks, 100);
  checks.require(submit(checks, engine, reusable, Side::Sell, ask_price,
                        quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  const auto before_duplicate = logical_state(engine);
  const auto command_before_duplicate = engine.last_command_sequence();
  const auto duplicate = submit(checks, engine, reusable, Side::Buy,
                                price(checks, 99), quantity(checks, 2));
  checks.require(duplicate.result == OrderBookResult::DuplicateOrderId);
  checks.require(!duplicate.command_sequence.is_valid());
  checks.require(logical_state(engine) == before_duplicate);
  checks.require(engine.last_command_sequence() == command_before_duplicate);

  checks.require(submit(checks, engine, order_id(checks, 2), Side::Buy,
                        ask_price, quantity(checks, 5))
                     .reports()
                     .size() == 1);
  checks.require(!engine.find_order(reusable));
  const auto reused_aggressive =
      submit(checks, engine, order_id(checks, 2), Side::Buy, price(checks, 90),
             quantity(checks, 2));
  checks.require(reused_aggressive.result == OrderBookResult::Accepted);
  checks.require(engine.find_order(order_id(checks, 2)).has_value());
  const auto reused = submit(checks, engine, reusable, Side::Buy,
                             price(checks, 90), quantity(checks, 4));
  checks.require(reused.result == OrderBookResult::Accepted);
  checks.require(engine.find_order(reusable).has_value());
}

void test_invalid_commands_are_atomic(Checks& checks) {
  const auto instrument = instrument_id(checks, 11);
  MatchingEngine engine(instrument);
  const auto before = logical_state(engine);
  const auto valid_id = order_id(checks, 1);
  const auto valid_price = price(checks, 100);
  const auto valid_quantity = quantity(checks, 1);

  const auto wrong_instrument = engine.process(
      NewOrder{valid_id, instrument_id(checks, 12), Side::Buy, valid_price,
               valid_quantity});
  checks.require(engine.validate_invariants());
  checks.require(wrong_instrument.result == OrderBookResult::InvalidInstrument);
  const auto invalid_side = engine.process(NewOrder{
      valid_id, instrument, Side::Invalid, valid_price, valid_quantity});
  checks.require(engine.validate_invariants());
  checks.require(invalid_side.result == OrderBookResult::InvalidSide);
  const auto invalid_price = engine.process(
      NewOrder{valid_id, instrument, Side::Buy, PriceTicks{}, valid_quantity});
  checks.require(engine.validate_invariants());
  checks.require(invalid_price.result == OrderBookResult::InvalidPrice);
  const auto invalid_quantity = engine.process(
      NewOrder{valid_id, instrument, Side::Buy, valid_price, Quantity{}});
  checks.require(engine.validate_invariants());
  checks.require(invalid_quantity.result == OrderBookResult::InvalidQuantity);

  checks.require(logical_state(engine) == before);
  checks.require(engine.last_command_sequence().value() == 0);
  checks.require(engine.last_engine_sequence().value() == 0);
}

void test_fill_capacity_boundary(Checks& checks) {
  const auto instrument = instrument_id(checks, 13);
  const auto ask_price = price(checks, 100);
  const auto one = quantity(checks, 1);
  MatchingEngine permitted(instrument);
  for (std::uint64_t value = 1; value <= 256; ++value) {
    checks.require(submit(checks, permitted, order_id(checks, value),
                          Side::Sell, ask_price, one)
                       .result == OrderBookResult::Accepted);
  }
  const auto success = submit(checks, permitted, order_id(checks, 1'000),
                              Side::Buy, ask_price, quantity(checks, 256));
  checks.require(success.result == OrderBookResult::Accepted);
  checks.require(success.reports().size() == 256);
  for (std::size_t index = 0; index < success.reports().size(); ++index) {
    checks.require(success.reports()[index].resting_order_id.value() ==
                   static_cast<std::uint64_t>(index) + 1);
    checks.require(success.reports()[index].match_quantity == one);
    checks.require(success.reports()[index].engine_sequence.value() ==
                   static_cast<std::uint64_t>(index) + 1);
  }
  checks.require(permitted.active_order_count() == 0);

  MatchingEngine rejected(instrument);
  for (std::uint64_t value = 1; value <= 257; ++value) {
    checks.require(submit(checks, rejected, order_id(checks, value), Side::Sell,
                          ask_price, one)
                       .result == OrderBookResult::Accepted);
  }
  const auto before = logical_state(rejected);
  const auto command_before = rejected.last_command_sequence();
  const auto engine_before = rejected.last_engine_sequence();
  const auto match_before = rejected.last_match_id();
  const auto failure = submit(checks, rejected, order_id(checks, 1'001),
                              Side::Buy, ask_price, quantity(checks, 257));
  checks.require(failure.result == OrderBookResult::CapacityExhausted);
  checks.require(failure.reports().empty());
  checks.require(failure.command_sequence.value() ==
                 command_before.value() + 1);
  checks.require(logical_state(rejected) == before);
  checks.require(rejected.last_engine_sequence() == engine_before);
  checks.require(rejected.last_match_id() == match_before);
  const auto after_failure = submit(
      checks, rejected, order_id(checks, 1'002), Side::Buy, ask_price, one);
  checks.require(after_failure.result == OrderBookResult::Accepted);
  checks.require(after_failure.reports().size() == 1);
  checks.require(after_failure.reports()[0].engine_sequence.value() == 1);
  checks.require(after_failure.reports()[0].match_id.value() == 1);
}

void test_final_state_capacity_preflight(Checks& checks) {
  const auto instrument = instrument_id(checks, 14);
  const auto ask_price = price(checks, 100);
  MatchingEngine frees_orders(instrument, StorageLimits{2, 2});
  checks.require(submit(checks, frees_orders, order_id(checks, 1), Side::Sell,
                        ask_price, quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, frees_orders, order_id(checks, 2), Side::Sell,
                        ask_price, quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  const auto frees_then_rests =
      submit(checks, frees_orders, order_id(checks, 3), Side::Buy, ask_price,
             quantity(checks, 3));
  checks.require(frees_then_rests.result == OrderBookResult::Accepted);
  checks.require(frees_then_rests.reports().size() == 2);
  checks.require(frees_orders.active_order_count() == 1);
  checks.require(frees_orders.find_order(order_id(checks, 3))->leaves_quantity ==
                 quantity(checks, 1));

  MatchingEngine level_full(instrument, StorageLimits{4, 1});
  checks.require(submit(checks, level_full, order_id(checks, 10), Side::Buy,
                        price(checks, 90), quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, level_full, order_id(checks, 11), Side::Sell,
                        ask_price, quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  const auto before_level_failure = logical_state(level_full);
  const auto failure = submit(checks, level_full, order_id(checks, 12),
                              Side::Buy, price(checks, 105),
                              quantity(checks, 2));
  checks.require(failure.result == OrderBookResult::CapacityExhausted);
  checks.require(failure.reports().empty());
  checks.require(logical_state(level_full) == before_level_failure);
  checks.require(level_full.last_engine_sequence().value() == 0);

  MatchingEngine active_full(instrument, StorageLimits{1, 2});
  checks.require(submit(checks, active_full, order_id(checks, 20), Side::Sell,
                        ask_price, quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  const auto before_active_failure = logical_state(active_full);
  const auto active_failure = submit(
      checks, active_full, order_id(checks, 21), Side::Buy, price(checks, 99),
      quantity(checks, 1));
  checks.require(active_failure.result == OrderBookResult::CapacityExhausted);
  checks.require(logical_state(active_full) == before_active_failure);

  MatchingEngine aggregate_full(instrument, StorageLimits{2, 2});
  const auto maximum =
      quantity(checks, std::numeric_limits<std::uint64_t>::max());
  checks.require(submit(checks, aggregate_full, order_id(checks, 30), Side::Buy,
                        price(checks, 90), maximum)
                     .result == OrderBookResult::Accepted);
  const auto before_aggregate_failure = logical_state(aggregate_full);
  const auto aggregate_failure = submit(
      checks, aggregate_full, order_id(checks, 31), Side::Buy,
      price(checks, 90), quantity(checks, 1));
  checks.require(aggregate_failure.result == OrderBookResult::CapacityExhausted);
  checks.require(logical_state(aggregate_full) == before_aggregate_failure);
}

struct DeterministicRun final {
  NewOrderResult outcome{};
  LogicalState state{};
  CommandSequence last_command{};
  EngineSequence last_engine{};
  MatchId last_match{};
};

DeterministicRun run_deterministic_case(Checks& checks,
                                        InstrumentId instrument) {
  MatchingEngine engine(instrument);
  const auto price_100 = price(checks, 100);
  const auto price_101 = price(checks, 101);
  const auto price_103 = price(checks, 103);
  checks.require(submit(checks, engine, order_id(checks, 1), Side::Sell,
                        price_100, quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, order_id(checks, 2), Side::Sell,
                        price_100, quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, order_id(checks, 3), Side::Sell,
                        price_101, quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, order_id(checks, 4), Side::Sell,
                        price_103, quantity(checks, 7))
                     .result == OrderBookResult::Accepted);
  auto outcome = submit(checks, engine, order_id(checks, 10), Side::Buy,
                        price_101, quantity(checks, 12));
  return {outcome,
          logical_state(engine),
          engine.last_command_sequence(),
          engine.last_engine_sequence(),
          engine.last_match_id()};
}

void test_determinism(Checks& checks) {
  const auto instrument = instrument_id(checks, 15);
  const auto first = run_deterministic_case(checks, instrument);
  const auto second = run_deterministic_case(checks, instrument);

  checks.require(first.outcome.result == second.outcome.result);
  checks.require(first.outcome.command_sequence ==
                 second.outcome.command_sequence);
  checks.require(reports_equal(first.outcome.reports(),
                               second.outcome.reports()));
  checks.require(first.state == second.state);
  checks.require(first.last_command == second.last_command);
  checks.require(first.last_engine == second.last_engine);
  checks.require(first.last_match == second.last_match);
}

}  // namespace

int main() {
  Checks checks;
  test_non_marketable_resting(checks);
  test_exact_cross_and_resting_price(checks);
  test_one_tick_non_crossing(checks);
  test_partial_and_full_resting_fills(checks);
  test_aggressive_remainder(checks);
  test_fifo_sweep(checks);
  test_buy_multi_level_limit_stop(checks);
  test_sell_multi_level_and_exact_consumption(checks);
  test_duplicate_and_id_reuse(checks);
  test_invalid_commands_are_atomic(checks);
  test_fill_capacity_boundary(checks);
  test_final_state_capacity_preflight(checks);
  test_determinism(checks);
  return checks.passed() ? 0 : 1;
}
