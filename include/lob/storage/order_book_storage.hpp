#pragma once

#include "lob/capacity.hpp"
#include "lob/domain/contracts.hpp"
#include "lob/memory/fixed_object_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace lob {

struct OrderBookStorageTestAccess;

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

struct StorageDiagnostics final {
  std::size_t configured_active_order_capacity{};
  std::size_t pool_used_count{};
  std::size_t pool_free_count{};
  std::size_t pool_high_water_count{};
  std::size_t bid_level_high_water_count{};
  std::size_t ask_level_high_water_count{};
  std::size_t active_index_capacity{};
  std::size_t order_node_size{};
  std::size_t order_node_alignment{};
  std::size_t pool_slot_size{};
  std::size_t pool_slot_alignment{};
  std::size_t pool_slot_backing_bytes{};
  std::size_t free_index_backing_bytes{};
  std::size_t pool_backing_bytes{};
  std::size_t active_index_backing_bytes{};
  std::size_t price_level_backing_bytes{};
  std::size_t total_configured_storage_bytes{};

  constexpr bool operator==(const StorageDiagnostics&) const noexcept = default;
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
      Quantity leaves_quantity) noexcept;
  [[nodiscard]] OrderBookResult remove_resting(OrderId order_id) noexcept;
  [[nodiscard]] OrderBookResult reduce_resting_by(
      OrderId order_id, Quantity reduction) noexcept;
  [[nodiscard]] OrderBookResult update_resting_leaves(
      OrderId order_id, Quantity new_leaves_quantity) noexcept;
  [[nodiscard]] OrderBookResult move_resting_to_back(
      OrderId order_id) noexcept;
  [[nodiscard]] bool can_insert_after_removing(
      std::optional<OrderId> first_removed_order) const noexcept;
  [[nodiscard]] bool can_clear() const noexcept;
  void clear() noexcept;

  [[nodiscard]] InstrumentId instrument_id() const noexcept;
  [[nodiscard]] std::size_t active_order_count() const noexcept;
  [[nodiscard]] std::size_t price_level_count(Side side) const noexcept;
  [[nodiscard]] std::size_t active_order_capacity() const noexcept;
  [[nodiscard]] std::size_t price_level_capacity_per_side() const noexcept;
  [[nodiscard]] StorageDiagnostics diagnostics() const noexcept;

  [[nodiscard]] std::optional<RestingOrderView> find_order(
      OrderId order_id) const noexcept;
  [[nodiscard]] std::optional<PriceTicks> best_bid() const noexcept;
  [[nodiscard]] std::optional<PriceTicks> best_ask() const noexcept;
  [[nodiscard]] std::optional<DepthEntry> level(
      Side side, PriceTicks price) const noexcept;
  [[nodiscard]] std::vector<DepthEntry> depth(Side side) const;
  [[nodiscard]] std::vector<RestingOrderView> orders_at_level(
      Side side, PriceTicks price) const;

  template <typename Visitor>
  void visit_orders_by_priority(Side side, Visitor&& visitor) const noexcept {
    const PriceLevel* levels = nullptr;
    std::size_t count = 0;
    if (side == Side::Buy) {
      levels = bids_.get();
      count = bid_level_count_;
    } else if (side == Side::Sell) {
      levels = asks_.get();
      count = ask_level_count_;
    }

    for (std::size_t level_index = 0; level_index < count; ++level_index) {
      NodeLink link = levels[level_index].head;
      while (!link.is_invalid()) {
        const OrderRecord* order = get(link);
        if (order == nullptr) {
          return;
        }
        const RestingOrderView view{order->order_id, order->instrument_id,
                                    order->side, order->price,
                                    order->leaves_quantity};
        link = order->next;
        if (!std::invoke(visitor, view)) {
          return;
        }
      }
    }
  }

#ifndef NDEBUG
  [[nodiscard]] bool validate_invariants() const noexcept;
#else
  [[nodiscard]] constexpr bool validate_invariants() const noexcept {
    return true;
  }
#endif

 private:
  friend struct OrderBookStorageTestAccess;

  struct NodeLink final {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint64_t generation{};
    std::uint64_t epoch{};

    [[nodiscard]] constexpr bool is_invalid() const noexcept {
      return index == std::numeric_limits<std::uint32_t>::max() ||
             generation == 0 || epoch == 0;
    }

    constexpr bool operator==(const NodeLink&) const noexcept = default;
  };

  struct OrderRecord final {
    OrderId order_id{};
    InstrumentId instrument_id{};
    Side side{Side::Invalid};
    PriceTicks price{};
    Quantity leaves_quantity{};
    NodeLink previous{};
    NodeLink next{};
  };

  using OrderPool = FixedObjectPool<OrderRecord>;
  using OrderHandle = OrderPool::Handle;

  struct PriceLevel final {
    PriceTicks price{};
    Quantity aggregate_leaves_quantity{};
    std::size_t order_count{};
    NodeLink head{};
    NodeLink tail{};
  };

  class ActiveOrderIndex final {
   public:
    explicit ActiveOrderIndex(std::size_t maximum_entries);

    ActiveOrderIndex(const ActiveOrderIndex&) = delete;
    ActiveOrderIndex& operator=(const ActiveOrderIndex&) = delete;
    ActiveOrderIndex(ActiveOrderIndex&&) = delete;
    ActiveOrderIndex& operator=(ActiveOrderIndex&&) = delete;
    ~ActiveOrderIndex() = default;

    [[nodiscard]] const NodeLink* find(OrderId order_id) const noexcept;
    [[nodiscard]] NodeLink* find(OrderId order_id) noexcept;
    [[nodiscard]] bool insert(OrderId order_id, NodeLink link) noexcept;
    [[nodiscard]] bool erase(OrderId order_id) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t backing_memory_bytes() const noexcept;
#ifndef NDEBUG
    [[nodiscard]] bool validate_invariants() const noexcept;
#endif

   private:
    friend struct OrderBookStorageTestAccess;

    using Control = std::uint8_t;
    static constexpr Control kEmpty{0};
    static constexpr Control kOccupied{1};

    struct Payload final {
      OrderId order_id{};
      NodeLink link{};
    };

    [[nodiscard]] std::size_t bucket(OrderId order_id) const noexcept;
    [[nodiscard]] std::size_t find_position(OrderId order_id) const noexcept;

    std::size_t capacity_{};
    std::size_t mask_{};
    std::size_t size_{};
    std::unique_ptr<Control[]> controls_{};
    std::unique_ptr<Payload[]> payloads_{};
  };

  [[nodiscard]] static NodeLink to_link(OrderHandle handle) noexcept;
  [[nodiscard]] OrderHandle to_handle(NodeLink link) const noexcept;
  [[nodiscard]] OrderRecord* get(NodeLink link) noexcept;
  [[nodiscard]] const OrderRecord* get(NodeLink link) const noexcept;
  [[nodiscard]] PriceLevel* find_level(Side side, PriceTicks price) noexcept;
  [[nodiscard]] const PriceLevel* find_level(Side side,
                                             PriceTicks price) const noexcept;
  [[nodiscard]] std::size_t find_level_position(
      const PriceLevel* levels, std::size_t count, Side side,
      PriceTicks price) const noexcept;
  [[nodiscard]] PriceLevel& create_level(Side side, PriceTicks price) noexcept;
  void erase_level(Side side, std::size_t position) noexcept;
  [[nodiscard]] OrderBookResult remove_link(OrderId order_id,
                                            NodeLink link) noexcept;
  void append_to_level(PriceLevel& level, NodeLink link) noexcept;
  void unlink_from_level(PriceLevel& level, NodeLink link) noexcept;
  [[nodiscard]] std::vector<RestingOrderView> level_orders(
      Side side, PriceTicks price) const;

  InstrumentId instrument_id_{};
  std::size_t active_order_capacity_{};
  std::size_t price_level_capacity_per_side_{};
  OrderPool order_pool_;
  ActiveOrderIndex orders_;
  std::unique_ptr<PriceLevel[]> bids_{};
  std::unique_ptr<PriceLevel[]> asks_{};
  std::size_t bid_level_count_{};
  std::size_t ask_level_count_{};
  std::size_t bid_level_high_water_count_{};
  std::size_t ask_level_high_water_count_{};
};

}  // namespace lob
