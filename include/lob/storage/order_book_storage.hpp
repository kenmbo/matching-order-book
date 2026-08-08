#pragma once

#include "lob/domain/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob {

inline constexpr std::size_t kMaximumActiveOrders = std::size_t{1} << 17;
inline constexpr std::size_t kMaximumPriceLevelsPerSide = std::size_t{1} << 12;

struct StorageLimits final {
  std::size_t active_orders{kMaximumActiveOrders};
  std::size_t price_levels_per_side{kMaximumPriceLevelsPerSide};
};

struct RestingOrderView final {
  OrderId order_id{};
  InstrumentId instrument_id{};
  Side side{Side::Invalid};
  PriceTicks price{};
  Quantity leaves_quantity{};

  constexpr bool operator==(const RestingOrderView&) const noexcept = default;
};

struct DepthEntry final {
  PriceTicks price{};
  Quantity aggregate_leaves_quantity{};
  std::size_t order_count{};

  constexpr bool operator==(const DepthEntry&) const noexcept = default;
};

class OrderBookStorage final {
 public:
  explicit OrderBookStorage(InstrumentId instrument_id,
                            StorageLimits limits = {});

  OrderBookStorage(const OrderBookStorage&) = delete;
  OrderBookStorage& operator=(const OrderBookStorage&) = delete;
  OrderBookStorage(OrderBookStorage&&) = delete;
  OrderBookStorage& operator=(OrderBookStorage&&) = delete;
  ~OrderBookStorage() = default;

  [[nodiscard]] OrderBookResult insert_resting(
      OrderId order_id, InstrumentId instrument_id, Side side, PriceTicks price,
      Quantity leaves_quantity);
  [[nodiscard]] OrderBookResult remove_resting(OrderId order_id);

  [[nodiscard]] InstrumentId instrument_id() const noexcept;
  [[nodiscard]] std::size_t active_order_count() const noexcept;
  [[nodiscard]] std::size_t price_level_count(Side side) const noexcept;
  [[nodiscard]] std::size_t active_order_capacity() const noexcept;
  [[nodiscard]] std::size_t price_level_capacity_per_side() const noexcept;

  [[nodiscard]] std::optional<RestingOrderView> find_order(
      OrderId order_id) const noexcept;
  [[nodiscard]] std::optional<PriceTicks> best_bid() const noexcept;
  [[nodiscard]] std::optional<PriceTicks> best_ask() const noexcept;
  [[nodiscard]] std::optional<DepthEntry> level(
      Side side, PriceTicks price) const noexcept;
  [[nodiscard]] std::vector<DepthEntry> depth(Side side) const;
  [[nodiscard]] std::vector<RestingOrderView> orders_at_level(
      Side side, PriceTicks price) const;

#ifndef NDEBUG
  [[nodiscard]] bool validate_invariants() const noexcept;
#else
  [[nodiscard]] constexpr bool validate_invariants() const noexcept {
    return true;
  }
#endif

 private:
  struct OrderRecord final {
    OrderId order_id{};
    InstrumentId instrument_id{};
    Side side{Side::Invalid};
    PriceTicks price{};
    Quantity leaves_quantity{};
  };

  struct PriceLevel final {
    Quantity aggregate_leaves_quantity{};
    std::list<OrderRecord> fifo{};
  };

  using OrderIterator = std::list<OrderRecord>::iterator;

  struct OrderLocation final {
    Side side{Side::Invalid};
    PriceTicks price{};
    OrderIterator order{};
  };

  struct OrderIdHash final {
    [[nodiscard]] std::size_t operator()(OrderId order_id) const noexcept {
      return std::hash<std::uint64_t>{}(order_id.value());
    }
  };

  using BidLevels = std::map<PriceTicks, PriceLevel, std::greater<PriceTicks>>;
  using AskLevels = std::map<PriceTicks, PriceLevel, std::less<PriceTicks>>;
  using OrderIndex = std::unordered_map<OrderId, OrderLocation, OrderIdHash>;

  [[nodiscard]] OrderBookResult insert_bid(OrderRecord order,
                                           Quantity aggregate);
  [[nodiscard]] OrderBookResult insert_ask(OrderRecord order,
                                           Quantity aggregate);
  [[nodiscard]] OrderBookResult remove_bid(OrderIndex::iterator indexed_order);
  [[nodiscard]] OrderBookResult remove_ask(OrderIndex::iterator indexed_order);

  [[nodiscard]] std::optional<DepthEntry> bid_level(
      PriceTicks price) const noexcept;
  [[nodiscard]] std::optional<DepthEntry> ask_level(
      PriceTicks price) const noexcept;
  [[nodiscard]] std::vector<RestingOrderView> bid_orders(
      PriceTicks price) const;
  [[nodiscard]] std::vector<RestingOrderView> ask_orders(
      PriceTicks price) const;

  InstrumentId instrument_id_{};
  std::size_t active_order_capacity_{};
  std::size_t price_level_capacity_per_side_{};
  BidLevels bids_{};
  AskLevels asks_{};
  OrderIndex orders_{};
};

static_assert(kMaximumActiveOrders == 131'072);
static_assert(kMaximumPriceLevelsPerSide == 4'096);

}  // namespace lob
