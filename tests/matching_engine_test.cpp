#include "lob/matching/matching_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

using lob::CommandSequence;
using lob::CancelOrder;
using lob::CancelOrderResult;
using lob::DepthEntry;
using lob::EngineSequence;
using lob::ExecutionReport;
using lob::InstrumentId;
using lob::InstrumentState;
using lob::LosslessOutboxLimits;
using lob::MatchId;
using lob::MatchingEngine;
using lob::AmendOrder;
using lob::AmendOrderResult;
using lob::NewOrder;
using lob::NewOrderResult;
using lob::OrderBookResult;
using lob::OrderId;
using lob::PriceTicks;
using lob::Quantity;
using lob::RestingOrderView;
using lob::Side;
using lob::StatusEventKind;
using lob::StatusReason;
using lob::StatusScope;
using lob::StorageLimits;
using lob::SystemStatus;

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

CancelOrderResult cancel(Checks& checks, MatchingEngine& engine, OrderId id) {
  const auto result =
      engine.process(CancelOrder{id, engine.instrument_id()});
  checks.require(engine.validate_invariants());
  return result;
}

AmendOrderResult amend(Checks& checks, MatchingEngine& engine, OrderId id,
                       PriceTicks new_price, Quantity new_leaves_quantity) {
  const auto result = engine.process(AmendOrder{
      id, engine.instrument_id(), new_price, new_leaves_quantity});
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

std::vector<ExecutionReport> drain_reports(MatchingEngine& engine) {
  std::vector<ExecutionReport> reports;
  reports.reserve(engine.pending_execution_report_count());
  ExecutionReport report;
  while (engine.try_consume_execution_report(report)) {
    reports.push_back(report);
  }
  return reports;
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

void test_cancel_positions_unknown_and_reuse(Checks& checks) {
  const auto instrument = instrument_id(checks, 17);
  const auto main_price = price(checks, 100);
  const auto lower_price = price(checks, 99);
  const auto ask_price = price(checks, 110);
  const auto head = order_id(checks, 1);
  const auto sole = order_id(checks, 2);
  const auto middle = order_id(checks, 3);
  const auto tail = order_id(checks, 4);
  const auto lower = order_id(checks, 5);
  const auto ask = order_id(checks, 6);
  MatchingEngine engine(instrument);

  for (const auto id : {head, sole, middle, tail}) {
    checks.require(submit(checks, engine, id, Side::Buy, main_price,
                          quantity(checks, id.value()))
                       .result == OrderBookResult::Accepted);
  }
  checks.require(submit(checks, engine, lower, Side::Buy, lower_price,
                        quantity(checks, 10))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask, Side::Sell, ask_price,
                        quantity(checks, 20))
                     .result == OrderBookResult::Accepted);

  const auto cancel_head = cancel(checks, engine, head);
  checks.require(cancel_head.result == OrderBookResult::Accepted &&
                 cancel_head.reports().empty());
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, main_price)) ==
                 std::vector<OrderId>{sole, middle, tail});
  checks.require(engine.level(Side::Buy, main_price)
                     ->aggregate_leaves_quantity == quantity(checks, 9));

  const auto cancel_middle = cancel(checks, engine, middle);
  checks.require(cancel_middle.result == OrderBookResult::Accepted &&
                 cancel_middle.reports().empty());
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, main_price)) ==
                 std::vector<OrderId>{sole, tail});
  checks.require(engine.level(Side::Buy, main_price)
                     ->aggregate_leaves_quantity == quantity(checks, 6));

  const auto cancel_tail = cancel(checks, engine, tail);
  checks.require(cancel_tail.result == OrderBookResult::Accepted &&
                 cancel_tail.reports().empty());
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, main_price)) ==
                 std::vector<OrderId>{sole});
  checks.require(engine.level(Side::Buy, main_price)
                     ->aggregate_leaves_quantity == quantity(checks, 2));

  const auto cancel_sole = cancel(checks, engine, sole);
  checks.require(cancel_sole.result == OrderBookResult::Accepted &&
                 cancel_sole.reports().empty());
  checks.require(!engine.level(Side::Buy, main_price));
  checks.require(engine.best_bid() == std::optional<PriceTicks>{lower_price});
  checks.require(engine.best_ask() == std::optional<PriceTicks>{ask_price});
  checks.require(engine.last_engine_sequence().value() == 0);
  checks.require(engine.last_match_id().value() == 0);

  for (const auto unknown : {order_id(checks, 999), head}) {
    const auto before = logical_state(engine);
    const auto command_before = engine.last_command_sequence();
    const auto engine_before = engine.last_engine_sequence();
    const auto rejected = cancel(checks, engine, unknown);
    checks.require(rejected.result == OrderBookResult::OrderNotFound);
    checks.require(!rejected.command_sequence.is_valid() &&
                   rejected.reports().empty());
    checks.require(logical_state(engine) == before);
    checks.require(engine.last_command_sequence() == command_before);
    checks.require(engine.last_engine_sequence() == engine_before);
  }

  const auto filled = order_id(checks, 20);
  checks.require(submit(checks, engine, filled, Side::Sell, ask_price,
                        quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, order_id(checks, 21), Side::Buy,
                        ask_price, quantity(checks, 21))
                     .reports()
                     .size() == 2);
  checks.require(!engine.find_order(filled));
  const auto before_filled_cancel = logical_state(engine);
  const auto command_before_filled_cancel = engine.last_command_sequence();
  const auto filled_cancel = cancel(checks, engine, filled);
  checks.require(filled_cancel.result == OrderBookResult::OrderNotFound);
  checks.require(logical_state(engine) == before_filled_cancel);
  checks.require(engine.last_command_sequence() ==
                 command_before_filled_cancel);

  const auto reused = submit(checks, engine, head, Side::Buy,
                             price(checks, 98), quantity(checks, 7));
  checks.require(reused.result == OrderBookResult::Accepted);
  checks.require(engine.find_order(head).has_value());
}

void test_same_price_amend_priority(Checks& checks) {
  const auto reduce_instrument = instrument_id(checks, 18);
  const auto level_price = price(checks, 100);
  const auto sole_price = price(checks, 99);
  const auto head = order_id(checks, 1);
  const auto middle = order_id(checks, 2);
  const auto tail = order_id(checks, 3);
  const auto sole = order_id(checks, 4);
  MatchingEngine reductions(reduce_instrument);
  checks.require(submit(checks, reductions, head, Side::Buy, level_price,
                        quantity(checks, 10))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, reductions, middle, Side::Buy, level_price,
                        quantity(checks, 20))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, reductions, tail, Side::Buy, level_price,
                        quantity(checks, 30))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, reductions, sole, Side::Buy, sole_price,
                        quantity(checks, 40))
                     .result == OrderBookResult::Accepted);

  for (const auto& [id, leaves] :
       {std::pair{head, std::uint64_t{9}},
        std::pair{middle, std::uint64_t{12}},
        std::pair{tail, std::uint64_t{25}},
        std::pair{sole, std::uint64_t{35}}}) {
    const auto outcome = amend(checks, reductions, id,
                               reductions.find_order(id)->price,
                               quantity(checks, leaves));
    checks.require(outcome.result == OrderBookResult::Accepted &&
                   outcome.reports().empty());
  }
  checks.require(fifo_ids(
                     reductions.orders_at_level(Side::Buy, level_price)) ==
                 std::vector<OrderId>{head, middle, tail});
  checks.require(reductions.level(Side::Buy, level_price)
                     ->aggregate_leaves_quantity == quantity(checks, 46));
  checks.require(fifo_ids(
                     reductions.orders_at_level(Side::Buy, sole_price)) ==
                 std::vector<OrderId>{sole});
  checks.require(reductions.level(Side::Buy, sole_price)
                     ->aggregate_leaves_quantity == quantity(checks, 35));

  const auto before_noop = logical_state(reductions);
  const auto engine_before_noop = reductions.last_engine_sequence();
  const auto noop = amend(checks, reductions, middle, level_price,
                          quantity(checks, 12));
  checks.require(noop.result == OrderBookResult::Accepted &&
                 noop.reports().empty());
  checks.require(logical_state(reductions) == before_noop);
  checks.require(reductions.last_engine_sequence() == engine_before_noop);
  checks.require(reductions.last_command_sequence() == noop.command_sequence);
  checks.require(reductions.last_match_id().value() == 0);

  const auto increase_instrument = instrument_id(checks, 19);
  MatchingEngine increases(increase_instrument);
  const auto first = order_id(checks, 10);
  const auto second = order_id(checks, 11);
  const auto third = order_id(checks, 12);
  const auto fourth = order_id(checks, 13);
  const auto lone = order_id(checks, 14);
  for (const auto id : {first, second, third, fourth}) {
    checks.require(submit(checks, increases, id, Side::Sell, level_price,
                          quantity(checks, id.value()))
                       .result == OrderBookResult::Accepted);
  }
  checks.require(submit(checks, increases, lone, Side::Sell,
                        price(checks, 101), quantity(checks, 5))
                     .result == OrderBookResult::Accepted);

  checks.require(amend(checks, increases, first, level_price,
                       quantity(checks, 20))
                     .result == OrderBookResult::Accepted);
  checks.require(fifo_ids(increases.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{second, third, fourth, first});

  checks.require(amend(checks, increases, third, level_price,
                       quantity(checks, 30))
                     .result == OrderBookResult::Accepted);
  checks.require(fifo_ids(increases.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{second, fourth, first, third});

  checks.require(amend(checks, increases, third, level_price,
                       quantity(checks, 31))
                     .result == OrderBookResult::Accepted);
  checks.require(fifo_ids(increases.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{second, fourth, first, third});

  checks.require(amend(checks, increases, lone, price(checks, 101),
                       quantity(checks, 8))
                     .result == OrderBookResult::Accepted);
  checks.require(fifo_ids(
                     increases.orders_at_level(Side::Sell, price(checks, 101))) ==
                 std::vector<OrderId>{lone});
  checks.require(increases.level(Side::Sell, level_price)
                     ->aggregate_leaves_quantity == quantity(checks, 75));
  checks.require(increases.last_engine_sequence().value() == 0);
  checks.require(increases.last_match_id().value() == 0);
}

void test_same_price_increase_overflow_is_atomic(Checks& checks) {
  const auto instrument = instrument_id(checks, 20);
  const auto level_price = price(checks, 100);
  const auto other = order_id(checks, 1);
  const auto target = order_id(checks, 2);
  MatchingEngine engine(instrument);
  checks.require(submit(checks, engine, other, Side::Buy, level_price,
                        quantity(checks,
                                 std::numeric_limits<std::uint64_t>::max() - 1))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, target, Side::Buy, level_price,
                        quantity(checks, 1))
                     .result == OrderBookResult::Accepted);

  const auto before = logical_state(engine);
  const auto command_before = engine.last_command_sequence();
  const auto engine_before = engine.last_engine_sequence();
  const auto match_before = engine.last_match_id();
  const auto failure = amend(checks, engine, target, level_price,
                             quantity(checks, 2));
  checks.require(failure.result == OrderBookResult::CapacityExhausted);
  checks.require(failure.command_sequence.value() ==
                 command_before.value() + 1);
  checks.require(failure.reports().empty());
  checks.require(logical_state(engine) == before);
  checks.require(engine.last_engine_sequence() == engine_before);
  checks.require(engine.last_match_id() == match_before);
}

void test_non_marketable_reprice_positions_and_bbo(Checks& checks) {
  const auto instrument = instrument_id(checks, 21);
  const auto old_price = price(checks, 100);
  const auto existing_price = price(checks, 98);
  const std::vector<std::size_t> positions{0, 1, 3};

  for (const auto position : positions) {
    MatchingEngine engine(instrument);
    const std::vector<OrderId> ids{order_id(checks, 1), order_id(checks, 2),
                                   order_id(checks, 3), order_id(checks, 4)};
    const auto existing = order_id(checks, 10);
    for (const auto id : ids) {
      checks.require(submit(checks, engine, id, Side::Buy, old_price,
                            quantity(checks, 10))
                         .result == OrderBookResult::Accepted);
    }
    checks.require(submit(checks, engine, existing, Side::Buy, existing_price,
                          quantity(checks, 7))
                       .result == OrderBookResult::Accepted);

    const auto outcome = amend(
        checks, engine, ids[position], existing_price,
        quantity(checks, static_cast<std::uint64_t>(20 + position)));
    checks.require(outcome.result == OrderBookResult::Accepted &&
                   outcome.reports().empty());

    auto expected_old = ids;
    expected_old.erase(expected_old.begin() +
                       static_cast<std::ptrdiff_t>(position));
    checks.require(fifo_ids(engine.orders_at_level(Side::Buy, old_price)) ==
                   expected_old);
    checks.require(fifo_ids(
                       engine.orders_at_level(Side::Buy, existing_price)) ==
                   std::vector<OrderId>{existing, ids[position]});
    checks.require(engine.find_order(ids[position])->price == existing_price);
    checks.require(engine.find_order(ids[position])->leaves_quantity ==
                   quantity(checks,
                            static_cast<std::uint64_t>(20 + position)));
    checks.require(engine.best_bid() == std::optional<PriceTicks>{old_price});
    checks.require(engine.last_engine_sequence().value() == 0);
  }

  MatchingEngine sole_engine(instrument);
  const auto target = order_id(checks, 20);
  const auto other = order_id(checks, 21);
  const auto ask = order_id(checks, 22);
  const auto other_price = price(checks, 99);
  const auto new_best = price(checks, 102);
  checks.require(submit(checks, sole_engine, target, Side::Buy, old_price,
                        quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, sole_engine, other, Side::Buy, other_price,
                        quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, sole_engine, ask, Side::Sell,
                        price(checks, 110), quantity(checks, 4))
                     .result == OrderBookResult::Accepted);

  checks.require(amend(checks, sole_engine, target, new_best,
                       quantity(checks, 6))
                     .result == OrderBookResult::Accepted);
  checks.require(!sole_engine.level(Side::Buy, old_price));
  checks.require(sole_engine.best_bid() ==
                 std::optional<PriceTicks>{new_best});
  checks.require(sole_engine.find_order(target)->leaves_quantity ==
                 quantity(checks, 6));

  checks.require(amend(checks, sole_engine, target, other_price,
                       quantity(checks, 8))
                     .result == OrderBookResult::Accepted);
  checks.require(!sole_engine.level(Side::Buy, new_best));
  checks.require(sole_engine.best_bid() ==
                 std::optional<PriceTicks>{other_price});
  checks.require(fifo_ids(
                     sole_engine.orders_at_level(Side::Buy, other_price)) ==
                 std::vector<OrderId>{other, target});
  checks.require(sole_engine.level(Side::Buy, other_price)
                     ->aggregate_leaves_quantity == quantity(checks, 11));
  checks.require(sole_engine.best_ask() ==
                 std::optional<PriceTicks>{price(checks, 110)});
}

void test_reprice_final_capacity_and_failure_atomicity(Checks& checks) {
  const auto instrument = instrument_id(checks, 22);

  MatchingEngine released_level(instrument, StorageLimits{2, 2});
  const auto target = order_id(checks, 1);
  const auto other = order_id(checks, 2);
  checks.require(submit(checks, released_level, target, Side::Buy,
                        price(checks, 90), quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, released_level, other, Side::Buy,
                        price(checks, 91), quantity(checks, 4))
                     .result == OrderBookResult::Accepted);
  checks.require(amend(checks, released_level, target, price(checks, 92),
                       quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  checks.require(released_level.price_level_count(Side::Buy) == 2);
  checks.require(released_level.best_bid() ==
                 std::optional<PriceTicks>{price(checks, 92)});

  MatchingEngine level_full(instrument, StorageLimits{4, 2});
  const auto sibling = order_id(checks, 10);
  const auto blocked = order_id(checks, 11);
  const auto second_level = order_id(checks, 12);
  checks.require(submit(checks, level_full, sibling, Side::Buy,
                        price(checks, 90), quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, level_full, blocked, Side::Buy,
                        price(checks, 90), quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, level_full, second_level, Side::Buy,
                        price(checks, 91), quantity(checks, 3))
                     .result == OrderBookResult::Accepted);
  const auto before_level_failure = logical_state(level_full);
  const auto command_before_level_failure =
      level_full.last_command_sequence();
  const auto level_failure = amend(checks, level_full, blocked,
                                   price(checks, 92), quantity(checks, 5));
  checks.require(level_failure.result == OrderBookResult::CapacityExhausted);
  checks.require(level_failure.command_sequence.value() ==
                 command_before_level_failure.value() + 1);
  checks.require(logical_state(level_full) == before_level_failure);
  checks.require(fifo_ids(
                     level_full.orders_at_level(Side::Buy, price(checks, 90))) ==
                 std::vector<OrderId>{sibling, blocked});
  checks.require(level_full.last_engine_sequence().value() == 0);
  checks.require(level_full.last_match_id().value() == 0);

  MatchingEngine aggregate_full(instrument, StorageLimits{3, 3});
  const auto maximum_order = order_id(checks, 20);
  const auto aggregate_target = order_id(checks, 21);
  checks.require(submit(checks, aggregate_full, maximum_order, Side::Buy,
                        price(checks, 100),
                        quantity(checks,
                                 std::numeric_limits<std::uint64_t>::max()))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, aggregate_full, aggregate_target, Side::Buy,
                        price(checks, 90), quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  const auto before_aggregate_failure = logical_state(aggregate_full);
  const auto aggregate_failure =
      amend(checks, aggregate_full, aggregate_target, price(checks, 100),
            quantity(checks, 1));
  checks.require(aggregate_failure.result ==
                 OrderBookResult::CapacityExhausted);
  checks.require(logical_state(aggregate_full) == before_aggregate_failure);
  checks.require(aggregate_full.last_engine_sequence().value() == 0);
  checks.require(aggregate_full.last_match_id().value() == 0);
}

void test_marketable_amendment_multi_level_and_remainder(Checks& checks) {
  const auto instrument = instrument_id(checks, 23);
  const auto target = order_id(checks, 1);
  const auto ask_100 = order_id(checks, 2);
  const auto ask_101 = order_id(checks, 3);
  const auto ask_103 = order_id(checks, 4);
  const auto price_100 = price(checks, 100);
  const auto price_101 = price(checks, 101);
  const auto price_103 = price(checks, 103);
  MatchingEngine engine(instrument);

  checks.require(submit(checks, engine, target, Side::Buy, price(checks, 90),
                        quantity(checks, 10))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask_101, Side::Sell, price_101,
                        quantity(checks, 10))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask_100, Side::Sell, price_100,
                        quantity(checks, 5))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, ask_103, Side::Sell, price_103,
                        quantity(checks, 9))
                     .result == OrderBookResult::Accepted);

  const auto result = amend(checks, engine, target, price_101,
                            quantity(checks, 20));
  checks.require(result.result == OrderBookResult::Accepted);
  checks.require(result.reports().size() == 2);
  checks.require(report_equals(
      result.reports()[0], make_domain<MatchId>(checks, std::uint64_t{1}),
      instrument, target, ask_100, price_100, quantity(checks, 5),
      make_domain<EngineSequence>(checks, std::uint64_t{1})));
  checks.require(report_equals(
      result.reports()[1], make_domain<MatchId>(checks, std::uint64_t{2}),
      instrument, target, ask_101, price_101, quantity(checks, 10),
      make_domain<EngineSequence>(checks, std::uint64_t{2})));
  const auto published = drain_reports(engine);
  checks.require(reports_equal(published, result.reports()));
  checks.require(!engine.find_order(ask_100));
  checks.require(!engine.find_order(ask_101));
  checks.require(engine.find_order(ask_103)->leaves_quantity ==
                 quantity(checks, 9));
  checks.require(engine.find_order(target) ==
                 std::optional<RestingOrderView>{{target, instrument, Side::Buy,
                                                  price_101,
                                                  quantity(checks, 5)}});
  checks.require(engine.best_bid() == std::optional<PriceTicks>{price_101});
  checks.require(engine.best_ask() == std::optional<PriceTicks>{price_103});
  checks.require(engine.last_command_sequence() == result.command_sequence);
  checks.require(engine.last_engine_sequence().value() == 2);
  checks.require(engine.last_match_id().value() == 2);
}

void test_marketable_amendment_sell_and_partial_resting_fill(Checks& checks) {
  const auto instrument = instrument_id(checks, 24);
  const auto target = order_id(checks, 1);
  const auto best_bid = order_id(checks, 2);
  const auto lower_bid = order_id(checks, 3);
  const auto price_100 = price(checks, 100);
  const auto price_99 = price(checks, 99);
  MatchingEngine engine(instrument);

  checks.require(submit(checks, engine, target, Side::Sell, price(checks, 110),
                        quantity(checks, 9))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, best_bid, Side::Buy, price_100,
                        quantity(checks, 10))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, lower_bid, Side::Buy, price_99,
                        quantity(checks, 8))
                     .result == OrderBookResult::Accepted);

  const auto result = amend(checks, engine, target, price_100,
                            quantity(checks, 4));
  checks.require(result.result == OrderBookResult::Accepted &&
                 result.reports().size() == 1);
  checks.require(result.reports()[0].aggressive_order_id == target);
  checks.require(result.reports()[0].resting_order_id == best_bid);
  checks.require(result.reports()[0].match_price == price_100);
  checks.require(result.reports()[0].match_quantity == quantity(checks, 4));
  const auto published = drain_reports(engine);
  checks.require(reports_equal(published, result.reports()));
  checks.require(!engine.find_order(target));
  checks.require(engine.find_order(best_bid)->leaves_quantity ==
                 quantity(checks, 6));
  checks.require(engine.find_order(lower_bid)->leaves_quantity ==
                 quantity(checks, 8));
  checks.require(engine.best_bid() == std::optional<PriceTicks>{price_100});
  checks.require(!engine.best_ask());

  const auto reused = submit(checks, engine, target, Side::Sell,
                             price(checks, 110), quantity(checks, 2));
  checks.require(reused.result == OrderBookResult::Accepted);
  checks.require(engine.find_order(target).has_value());
}

void test_marketable_amendment_fifo_sweep(Checks& checks) {
  const auto instrument = instrument_id(checks, 25);
  const auto target = order_id(checks, 1);
  const auto first = order_id(checks, 2);
  const auto second = order_id(checks, 3);
  const auto third = order_id(checks, 4);
  const auto fourth = order_id(checks, 5);
  const auto level_price = price(checks, 100);
  MatchingEngine engine(instrument);

  checks.require(submit(checks, engine, target, Side::Buy, price(checks, 90),
                        quantity(checks, 20))
                     .result == OrderBookResult::Accepted);
  for (const auto& [id, leaves] :
       {std::pair{first, std::uint64_t{2}},
        std::pair{second, std::uint64_t{3}},
        std::pair{third, std::uint64_t{7}},
        std::pair{fourth, std::uint64_t{4}}}) {
    checks.require(submit(checks, engine, id, Side::Sell, level_price,
                          quantity(checks, leaves))
                       .result == OrderBookResult::Accepted);
  }

  const auto result = amend(checks, engine, target, level_price,
                            quantity(checks, 6));
  checks.require(result.result == OrderBookResult::Accepted &&
                 result.reports().size() == 3);
  checks.require(result.reports()[0].resting_order_id == first &&
                 result.reports()[0].match_quantity == quantity(checks, 2));
  checks.require(result.reports()[1].resting_order_id == second &&
                 result.reports()[1].match_quantity == quantity(checks, 3));
  checks.require(result.reports()[2].resting_order_id == third &&
                 result.reports()[2].match_quantity == quantity(checks, 1));
  checks.require(!engine.find_order(target));
  checks.require(!engine.find_order(first) && !engine.find_order(second));
  checks.require(engine.find_order(third)->leaves_quantity ==
                 quantity(checks, 6));
  checks.require(fifo_ids(engine.orders_at_level(Side::Sell, level_price)) ==
                 std::vector<OrderId>{third, fourth});
  checks.require(engine.level(Side::Sell, level_price)
                     ->aggregate_leaves_quantity == quantity(checks, 10));
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

void test_cancel_and_amend_validation_is_atomic(Checks& checks) {
  const auto instrument = instrument_id(checks, 26);
  const auto other_instrument = instrument_id(checks, 27);
  const auto target = order_id(checks, 1);
  const auto level_price = price(checks, 100);
  const auto leaves = quantity(checks, 10);
  MatchingEngine engine(instrument);
  checks.require(submit(checks, engine, target, Side::Buy, level_price, leaves)
                     .result == OrderBookResult::Accepted);

  const auto before = logical_state(engine);
  const auto command_before = engine.last_command_sequence();
  const auto engine_before = engine.last_engine_sequence();
  const auto match_before = engine.last_match_id();

  const auto wrong_cancel =
      engine.process(CancelOrder{target, other_instrument});
  checks.require(engine.validate_invariants());
  checks.require(wrong_cancel.result == OrderBookResult::InvalidInstrument);

  const auto wrong_amend = engine.process(
      AmendOrder{target, other_instrument, level_price, leaves});
  checks.require(engine.validate_invariants());
  checks.require(wrong_amend.result == OrderBookResult::InvalidInstrument);

  const auto unknown_amend = engine.process(
      AmendOrder{order_id(checks, 999), instrument, level_price, leaves});
  checks.require(engine.validate_invariants());
  checks.require(unknown_amend.result == OrderBookResult::OrderNotFound);

  const auto invalid_price = engine.process(
      AmendOrder{target, instrument, PriceTicks{}, leaves});
  checks.require(engine.validate_invariants());
  checks.require(invalid_price.result == OrderBookResult::InvalidPrice);

  const auto zero_quantity = engine.process(
      AmendOrder{target, instrument, level_price, Quantity{}});
  checks.require(engine.validate_invariants());
  checks.require(zero_quantity.result == OrderBookResult::InvalidAmendment);

  const auto invalid_cancel =
      engine.process(CancelOrder{OrderId{}, instrument});
  checks.require(engine.validate_invariants());
  checks.require(invalid_cancel.result == OrderBookResult::OrderNotFound);

  checks.require(logical_state(engine) == before);
  checks.require(engine.last_command_sequence() == command_before);
  checks.require(engine.last_engine_sequence() == engine_before);
  checks.require(engine.last_match_id() == match_before);
  for (const auto* result : {&wrong_cancel, &wrong_amend, &unknown_amend,
                             &invalid_price, &zero_quantity,
                             &invalid_cancel}) {
    checks.require(!result->command_sequence.is_valid());
    checks.require(result->reports().empty());
  }
}

void test_execution_outbox_ordering_and_capacity_reuse(Checks& checks) {
  const auto instrument = instrument_id(checks, 30);
  const auto level_price = price(checks, 100);
  const auto one = quantity(checks, 1);
  MatchingEngine buy_engine(instrument, {}, LosslessOutboxLimits{4, 1});
  for (std::uint64_t value = 1; value <= 3; ++value) {
    checks.require(submit(checks, buy_engine, order_id(checks, value),
                          Side::Sell, level_price, one)
                       .result == OrderBookResult::Accepted);
  }

  const auto buy = submit(checks, buy_engine, order_id(checks, 10), Side::Buy,
                          level_price, quantity(checks, 3));
  checks.require(buy.result == OrderBookResult::Accepted);
  checks.require(buy_engine.pending_execution_report_count() == 3);
  checks.require(buy_engine.available_execution_outbox_capacity() == 1);
  const auto published_buy = drain_reports(buy_engine);
  checks.require(reports_equal(published_buy, buy.reports()));
  checks.require(buy_engine.pending_execution_report_count() == 0);
  checks.require(buy_engine.available_execution_outbox_capacity() == 4);

  checks.require(submit(checks, buy_engine, order_id(checks, 11), Side::Sell,
                        level_price, one)
                     .result == OrderBookResult::Accepted);
  const auto reused = submit(checks, buy_engine, order_id(checks, 12),
                             Side::Buy, level_price, one);
  checks.require(reused.result == OrderBookResult::Accepted);
  checks.require(reused.reports().size() == 1);
  const auto reused_published = drain_reports(buy_engine);
  checks.require(reports_equal(reused_published, reused.reports()));
  checks.require(reused_published[0].engine_sequence.value() == 4);
  checks.require(reused_published[0].match_id.value() == 4);

  MatchingEngine sell_engine(instrument, {}, LosslessOutboxLimits{4, 1});
  for (std::uint64_t value = 1; value <= 3; ++value) {
    checks.require(submit(checks, sell_engine, order_id(checks, 100 + value),
                          Side::Buy, level_price, one)
                       .result == OrderBookResult::Accepted);
  }
  const auto sell = submit(checks, sell_engine, order_id(checks, 200),
                           Side::Sell, level_price, quantity(checks, 3));
  const auto published_sell = drain_reports(sell_engine);
  checks.require(sell.result == OrderBookResult::Accepted);
  checks.require(reports_equal(published_sell, sell.reports()));
  for (std::size_t index = 0; index < published_sell.size(); ++index) {
    checks.require(published_sell[index].resting_order_id.value() ==
                   std::uint64_t{101} + static_cast<std::uint64_t>(index));
    checks.require(published_sell[index].engine_sequence.value() ==
                   std::uint64_t{1} + static_cast<std::uint64_t>(index));
    checks.require(published_sell[index].match_id.value() ==
                   std::uint64_t{1} + static_cast<std::uint64_t>(index));
  }
}

void test_zero_output_commands_ignore_full_execution_outbox(Checks& checks) {
  const auto instrument = instrument_id(checks, 31);
  const auto ask_price = price(checks, 100);
  const auto bid_price = price(checks, 90);
  const auto target = order_id(checks, 10);
  MatchingEngine engine(instrument, {}, LosslessOutboxLimits{1, 1});

  checks.require(submit(checks, engine, order_id(checks, 1), Side::Sell,
                        ask_price, quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, order_id(checks, 2), Side::Buy,
                        ask_price, quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  checks.require(engine.pending_execution_report_count() == 1);
  checks.require(engine.available_execution_outbox_capacity() == 0);

  const auto resting = submit(checks, engine, target, Side::Buy, bid_price,
                              quantity(checks, 5));
  const auto noop = amend(checks, engine, target, bid_price,
                          quantity(checks, 5));
  const auto reduction = amend(checks, engine, target, bid_price,
                               quantity(checks, 4));
  const auto increase = amend(checks, engine, target, bid_price,
                              quantity(checks, 6));
  const auto reprice = amend(checks, engine, target, price(checks, 91),
                             quantity(checks, 6));
  const auto cancelled = cancel(checks, engine, target);

  for (const auto* outcome :
       {&resting, &noop, &reduction, &increase, &reprice, &cancelled}) {
    checks.require(outcome->result == OrderBookResult::Accepted);
    checks.require(outcome->reports().empty());
  }
  checks.require(!engine.find_order(target));
  checks.require(engine.instrument_state() == InstrumentState::Active);
  checks.require(engine.pending_execution_report_count() == 1);
  checks.require(engine.pending_status_event_count() == 0);
  checks.require(engine.last_engine_sequence().value() == 1);
  checks.require(engine.last_match_id().value() == 1);
}

void test_new_order_outbox_failure_is_atomic_and_halts(Checks& checks) {
  const auto instrument = instrument_id(checks, 32);
  const auto level_price = price(checks, 100);
  const auto one = quantity(checks, 1);
  const auto failed_order = order_id(checks, 20);
  MatchingEngine engine(instrument, {}, LosslessOutboxLimits{2, 1});

  for (std::uint64_t value = 1; value <= 3; ++value) {
    checks.require(submit(checks, engine, order_id(checks, value), Side::Sell,
                          level_price, one)
                       .result == OrderBookResult::Accepted);
  }
  const auto warmup = submit(checks, engine, order_id(checks, 10), Side::Buy,
                             level_price, one);
  checks.require(warmup.result == OrderBookResult::Accepted);
  checks.require(warmup.reports().size() == 1);
  checks.require(engine.pending_execution_report_count() == 1);

  const auto before = logical_state(engine);
  const auto command_before = engine.last_command_sequence();
  const auto engine_before = engine.last_engine_sequence();
  const auto match_before = engine.last_match_id();
  const auto failure = submit(checks, engine, failed_order, Side::Buy,
                              level_price, quantity(checks, 2));

  checks.require(failure.result == OrderBookResult::LosslessOutboxFull);
  checks.require(failure.command_sequence.value() ==
                 command_before.value() + 1);
  checks.require(failure.reports().empty());
  checks.require(logical_state(engine) == before);
  checks.require(!engine.find_order(failed_order));
  checks.require(engine.last_match_id() == match_before);
  checks.require(engine.last_engine_sequence().value() ==
                 engine_before.value() + 1);
  checks.require(engine.instrument_state() == InstrumentState::Halted);
  checks.require(engine.pending_execution_report_count() == 1);
  checks.require(engine.pending_status_event_count() == 1);

  const auto unchanged_reports = drain_reports(engine);
  checks.require(reports_equal(unchanged_reports, warmup.reports()));

  SystemStatus status;
  checks.require(engine.try_consume_status(status));
  checks.require(status.scope == StatusScope::Instrument);
  checks.require(status.instrument_id == instrument);
  checks.require(status.previous_state == InstrumentState::Active);
  checks.require(status.resulting_state == InstrumentState::Halted);
  checks.require(status.kind == StatusEventKind::StateTransition);
  checks.require(status.reason == StatusReason::LosslessOutboxFull);
  checks.require(status.engine_sequence == engine.last_engine_sequence());
  checks.require(!engine.try_consume_status(status));

  const auto command_after_halt = engine.last_command_sequence();
  const auto rejected = engine.process(NewOrder{
      order_id(checks, 21), instrument, Side::Buy, level_price, one});
  checks.require(rejected.result == OrderBookResult::MarketHalted);
  checks.require(engine.last_command_sequence() == command_after_halt);

  const auto cancel_while_halted = cancel(checks, engine, order_id(checks, 2));
  checks.require(cancel_while_halted.result == OrderBookResult::Accepted);
  checks.require(engine.instrument_state() == InstrumentState::Halted);
}

void test_amend_outbox_failure_is_atomic_and_halts(Checks& checks) {
  const auto instrument = instrument_id(checks, 33);
  const auto target = order_id(checks, 10);
  const auto sibling = order_id(checks, 11);
  const auto resting_ask = order_id(checks, 12);
  const auto old_price = price(checks, 90);
  const auto crossing_price = price(checks, 100);
  MatchingEngine engine(instrument, {}, LosslessOutboxLimits{1, 1});

  checks.require(submit(checks, engine, order_id(checks, 1), Side::Sell,
                        crossing_price, quantity(checks, 1))
                     .result == OrderBookResult::Accepted);
  const auto warmup = submit(checks, engine, order_id(checks, 2), Side::Buy,
                             crossing_price, quantity(checks, 1));
  checks.require(warmup.result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, target, Side::Buy, old_price,
                        quantity(checks, 4))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, sibling, Side::Buy, old_price,
                        quantity(checks, 2))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, engine, resting_ask, Side::Sell,
                        crossing_price, quantity(checks, 4))
                     .result == OrderBookResult::Accepted);

  const auto before = logical_state(engine);
  const auto command_before = engine.last_command_sequence();
  const auto engine_before = engine.last_engine_sequence();
  const auto match_before = engine.last_match_id();
  const auto failure = amend(checks, engine, target, crossing_price,
                             quantity(checks, 4));

  checks.require(failure.result == OrderBookResult::LosslessOutboxFull);
  checks.require(failure.command_sequence.value() ==
                 command_before.value() + 1);
  checks.require(failure.reports().empty());
  checks.require(logical_state(engine) == before);
  checks.require(engine.find_order(target)->price == old_price);
  checks.require(engine.find_order(target)->leaves_quantity ==
                 quantity(checks, 4));
  checks.require(fifo_ids(engine.orders_at_level(Side::Buy, old_price)) ==
                 std::vector<OrderId>{target, sibling});
  checks.require(engine.find_order(resting_ask)->leaves_quantity ==
                 quantity(checks, 4));
  checks.require(engine.last_match_id() == match_before);
  checks.require(engine.last_engine_sequence().value() ==
                 engine_before.value() + 1);
  checks.require(engine.instrument_state() == InstrumentState::Halted);
  checks.require(engine.pending_execution_report_count() == 1);
  checks.require(engine.pending_status_event_count() == 1);

  const auto unchanged_reports = drain_reports(engine);
  checks.require(reports_equal(unchanged_reports, warmup.reports()));
  SystemStatus status;
  checks.require(engine.try_consume_status(status));
  checks.require(status.reason == StatusReason::LosslessOutboxFull);
  checks.require(status.engine_sequence == engine.last_engine_sequence());
}

void test_fill_capacity_boundary(Checks& checks) {
  const auto instrument = instrument_id(checks, 13);
  const auto ask_price = price(checks, 100);
  const auto one = quantity(checks, 1);
  MatchingEngine permitted(instrument, {}, LosslessOutboxLimits{256, 1});
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

void test_amend_fill_capacity_boundary(Checks& checks) {
  const auto instrument = instrument_id(checks, 28);
  const auto old_price = price(checks, 90);
  const auto crossing_price = price(checks, 100);
  const auto one = quantity(checks, 1);

  MatchingEngine permitted(instrument, {}, LosslessOutboxLimits{256, 1});
  const auto permitted_target = order_id(checks, 1);
  checks.require(submit(checks, permitted, permitted_target, Side::Buy,
                        old_price, quantity(checks, 256))
                     .result == OrderBookResult::Accepted);
  for (std::uint64_t value = 1; value <= 256; ++value) {
    checks.require(submit(checks, permitted,
                          order_id(checks, 1'000 + value), Side::Sell,
                          crossing_price, one)
                       .result == OrderBookResult::Accepted);
  }
  const auto success = amend(checks, permitted, permitted_target,
                             crossing_price, quantity(checks, 256));
  checks.require(success.result == OrderBookResult::Accepted);
  checks.require(success.reports().size() == 256);
  for (std::size_t index = 0; index < success.reports().size(); ++index) {
    checks.require(success.reports()[index].aggressive_order_id ==
                   permitted_target);
    checks.require(success.reports()[index].resting_order_id.value() ==
                   std::uint64_t{1'001} +
                       static_cast<std::uint64_t>(index));
    checks.require(success.reports()[index].match_quantity == one);
    checks.require(success.reports()[index].engine_sequence.value() ==
                   static_cast<std::uint64_t>(index) + 1);
    checks.require(success.reports()[index].match_id.value() ==
                   static_cast<std::uint64_t>(index) + 1);
  }
  checks.require(permitted.active_order_count() == 0);

  MatchingEngine rejected(instrument);
  const auto rejected_target = order_id(checks, 2);
  const auto sibling = order_id(checks, 3);
  checks.require(submit(checks, rejected, rejected_target, Side::Buy,
                        old_price, quantity(checks, 257))
                     .result == OrderBookResult::Accepted);
  checks.require(submit(checks, rejected, sibling, Side::Buy, old_price,
                        quantity(checks, 9))
                     .result == OrderBookResult::Accepted);
  for (std::uint64_t value = 1; value <= 257; ++value) {
    checks.require(submit(checks, rejected,
                          order_id(checks, 2'000 + value), Side::Sell,
                          crossing_price, one)
                       .result == OrderBookResult::Accepted);
  }

  const auto before = logical_state(rejected);
  const auto command_before = rejected.last_command_sequence();
  const auto engine_before = rejected.last_engine_sequence();
  const auto match_before = rejected.last_match_id();
  const auto failure = amend(checks, rejected, rejected_target,
                             crossing_price, quantity(checks, 257));
  checks.require(failure.result == OrderBookResult::CapacityExhausted);
  checks.require(failure.command_sequence.value() ==
                 command_before.value() + 1);
  checks.require(failure.reports().empty());
  checks.require(logical_state(rejected) == before);
  checks.require(rejected.find_order(rejected_target)->price == old_price);
  checks.require(rejected.find_order(rejected_target)->leaves_quantity ==
                 quantity(checks, 257));
  checks.require(fifo_ids(rejected.orders_at_level(Side::Buy, old_price)) ==
                 std::vector<OrderId>{rejected_target, sibling});
  checks.require(rejected.last_engine_sequence() == engine_before);
  checks.require(rejected.last_match_id() == match_before);
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
  test_cancel_positions_unknown_and_reuse(checks);
  test_same_price_amend_priority(checks);
  test_same_price_increase_overflow_is_atomic(checks);
  test_non_marketable_reprice_positions_and_bbo(checks);
  test_reprice_final_capacity_and_failure_atomicity(checks);
  test_marketable_amendment_multi_level_and_remainder(checks);
  test_marketable_amendment_sell_and_partial_resting_fill(checks);
  test_marketable_amendment_fifo_sweep(checks);
  test_invalid_commands_are_atomic(checks);
  test_cancel_and_amend_validation_is_atomic(checks);
  test_execution_outbox_ordering_and_capacity_reuse(checks);
  test_zero_output_commands_ignore_full_execution_outbox(checks);
  test_new_order_outbox_failure_is_atomic_and_halts(checks);
  test_amend_outbox_failure_is_atomic_and_halts(checks);
  test_fill_capacity_boundary(checks);
  test_amend_fill_capacity_boundary(checks);
  test_final_state_capacity_preflight(checks);
  test_determinism(checks);
  return checks.passed() ? 0 : 1;
}
