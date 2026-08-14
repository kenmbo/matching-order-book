#include "lob/storage/order_book_storage.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace lob {

namespace {

[[nodiscard]] RestingOrderView make_view(
    OrderId order_id, InstrumentId instrument_id, Side side, PriceTicks price,
    Quantity leaves_quantity) noexcept {
  return {order_id, instrument_id, side, price, leaves_quantity};
}

}  // namespace

OrderBookStorage::OrderBookStorage(InstrumentId instrument_id,
                                   StorageLimits limits)
    : instrument_id_(instrument_id),
      active_order_capacity_(
          std::min(limits.active_orders, kMaximumActiveOrders)),
      price_level_capacity_per_side_(std::min(
          limits.price_levels_per_side, kMaximumPriceLevelsPerSide)) {
  orders_.reserve(active_order_capacity_);
}

OrderBookResult OrderBookStorage::insert_resting(
    OrderId order_id, InstrumentId instrument_id, Side side, PriceTicks price,
    Quantity leaves_quantity) {
  if (!instrument_id_.is_valid() || instrument_id != instrument_id_) {
    return OrderBookResult::InvalidInstrument;
  }
  if (!order_id.is_valid()) {
    return OrderBookResult::DuplicateOrderId;
  }
  if (side != Side::Buy && side != Side::Sell) {
    return OrderBookResult::InvalidSide;
  }
  if (!price.is_valid()) {
    return OrderBookResult::InvalidPrice;
  }
  if (!leaves_quantity.is_valid()) {
    return OrderBookResult::InvalidQuantity;
  }
  if (orders_.contains(order_id)) {
    return OrderBookResult::DuplicateOrderId;
  }
  if (orders_.size() >= active_order_capacity_) {
    return OrderBookResult::CapacityExhausted;
  }

  const OrderRecord order{order_id, instrument_id, side, price, leaves_quantity};
  if (side == Side::Buy) {
    const auto existing_level = bids_.find(price);
    if (existing_level == bids_.end()) {
      if (bids_.size() >= price_level_capacity_per_side_) {
        return OrderBookResult::CapacityExhausted;
      }
      return insert_bid(order, leaves_quantity);
    }

    const auto aggregate =
        checked_add(existing_level->second.aggregate_leaves_quantity,
                    leaves_quantity);
    if (!aggregate.has_value()) {
      return OrderBookResult::CapacityExhausted;
    }
    return insert_bid(order, aggregate.value);
  }

  const auto existing_level = asks_.find(price);
  if (existing_level == asks_.end()) {
    if (asks_.size() >= price_level_capacity_per_side_) {
      return OrderBookResult::CapacityExhausted;
    }
    return insert_ask(order, leaves_quantity);
  }

  const auto aggregate =
      checked_add(existing_level->second.aggregate_leaves_quantity,
                  leaves_quantity);
  if (!aggregate.has_value()) {
    return OrderBookResult::CapacityExhausted;
  }
  return insert_ask(order, aggregate.value);
}

OrderBookResult OrderBookStorage::insert_bid(OrderRecord order,
                                             Quantity aggregate) {
  auto [level, level_created] = bids_.try_emplace(order.price);
  level->second.fifo.push_back(order);
  const auto inserted_order = std::prev(level->second.fifo.end());
  const auto [indexed_order, inserted] = orders_.emplace(
      order.order_id,
      OrderLocation{Side::Buy, order.price, inserted_order});
  if (!inserted) {
    level->second.fifo.erase(inserted_order);
    if (level_created) {
      bids_.erase(level);
    }
    return OrderBookResult::DuplicateOrderId;
  }
  static_cast<void>(indexed_order);
  level->second.aggregate_leaves_quantity = aggregate;
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::insert_ask(OrderRecord order,
                                             Quantity aggregate) {
  auto [level, level_created] = asks_.try_emplace(order.price);
  level->second.fifo.push_back(order);
  const auto inserted_order = std::prev(level->second.fifo.end());
  const auto [indexed_order, inserted] = orders_.emplace(
      order.order_id,
      OrderLocation{Side::Sell, order.price, inserted_order});
  if (!inserted) {
    level->second.fifo.erase(inserted_order);
    if (level_created) {
      asks_.erase(level);
    }
    return OrderBookResult::DuplicateOrderId;
  }
  static_cast<void>(indexed_order);
  level->second.aggregate_leaves_quantity = aggregate;
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::remove_resting(OrderId order_id) {
  const auto indexed_order = orders_.find(order_id);
  if (indexed_order == orders_.end()) {
    return OrderBookResult::OrderNotFound;
  }

  if (indexed_order->second.side == Side::Buy) {
    return remove_bid(indexed_order);
  }
  return remove_ask(indexed_order);
}

OrderBookResult OrderBookStorage::reduce_resting_by(
    OrderId order_id, Quantity reduction) noexcept {
  const auto indexed_order = orders_.find(order_id);
  if (indexed_order == orders_.end()) {
    return OrderBookResult::OrderNotFound;
  }
  if (!reduction.is_valid() ||
      reduction > indexed_order->second.order->leaves_quantity) {
    return OrderBookResult::InvalidQuantity;
  }
  if (reduction == indexed_order->second.order->leaves_quantity) {
    return remove_resting(order_id);
  }

  const auto new_leaves = checked_subtract(
      indexed_order->second.order->leaves_quantity, reduction);
  if (!new_leaves.has_value()) {
    return OrderBookResult::InvalidQuantity;
  }

  if (indexed_order->second.side == Side::Buy) {
    const auto level = bids_.find(indexed_order->second.price);
    if (level == bids_.end()) {
      return OrderBookResult::OrderNotFound;
    }
    const auto aggregate =
        checked_subtract(level->second.aggregate_leaves_quantity, reduction);
    if (!aggregate.has_value()) {
      return OrderBookResult::InvalidQuantity;
    }
    indexed_order->second.order->leaves_quantity = new_leaves.value;
    level->second.aggregate_leaves_quantity = aggregate.value;
    return OrderBookResult::Accepted;
  }

  const auto level = asks_.find(indexed_order->second.price);
  if (level == asks_.end()) {
    return OrderBookResult::OrderNotFound;
  }
  const auto aggregate =
      checked_subtract(level->second.aggregate_leaves_quantity, reduction);
  if (!aggregate.has_value()) {
    return OrderBookResult::InvalidQuantity;
  }
  indexed_order->second.order->leaves_quantity = new_leaves.value;
  level->second.aggregate_leaves_quantity = aggregate.value;
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::update_resting_leaves(
    OrderId order_id, Quantity new_leaves_quantity) noexcept {
  const auto indexed_order = orders_.find(order_id);
  if (indexed_order == orders_.end()) {
    return OrderBookResult::OrderNotFound;
  }
  if (!new_leaves_quantity.is_valid()) {
    return OrderBookResult::InvalidQuantity;
  }

  const auto old_leaves = indexed_order->second.order->leaves_quantity;
  if (new_leaves_quantity == old_leaves) {
    return OrderBookResult::Accepted;
  }

  const auto update_level = [&](auto& levels) noexcept {
    const auto level = levels.find(indexed_order->second.price);
    if (level == levels.end()) {
      return OrderBookResult::OrderNotFound;
    }

    CheckedQuantity new_aggregate;
    if (new_leaves_quantity < old_leaves) {
      const auto reduction = checked_subtract(old_leaves, new_leaves_quantity);
      if (!reduction.has_value()) {
        return OrderBookResult::InvalidQuantity;
      }
      new_aggregate = checked_subtract(
          level->second.aggregate_leaves_quantity, reduction.value);
      if (!new_aggregate.has_value()) {
        return OrderBookResult::InvalidQuantity;
      }
    } else {
      const auto increase = checked_subtract(new_leaves_quantity, old_leaves);
      if (!increase.has_value()) {
        return OrderBookResult::InvalidQuantity;
      }
      new_aggregate = checked_add(level->second.aggregate_leaves_quantity,
                                  increase.value);
      if (!new_aggregate.has_value()) {
        return OrderBookResult::CapacityExhausted;
      }
    }

    indexed_order->second.order->leaves_quantity = new_leaves_quantity;
    level->second.aggregate_leaves_quantity = new_aggregate.value;
    return OrderBookResult::Accepted;
  };

  if (indexed_order->second.side == Side::Buy) {
    return update_level(bids_);
  }
  if (indexed_order->second.side == Side::Sell) {
    return update_level(asks_);
  }
  return OrderBookResult::OrderNotFound;
}

OrderBookResult OrderBookStorage::move_resting_to_back(
    OrderId order_id) noexcept {
  const auto indexed_order = orders_.find(order_id);
  if (indexed_order == orders_.end()) {
    return OrderBookResult::OrderNotFound;
  }

  const auto move_in_level = [&](auto& levels) noexcept {
    const auto level = levels.find(indexed_order->second.price);
    if (level == levels.end()) {
      return OrderBookResult::OrderNotFound;
    }
    level->second.fifo.splice(level->second.fifo.end(), level->second.fifo,
                              indexed_order->second.order);
    return OrderBookResult::Accepted;
  };

  if (indexed_order->second.side == Side::Buy) {
    return move_in_level(bids_);
  }
  if (indexed_order->second.side == Side::Sell) {
    return move_in_level(asks_);
  }
  return OrderBookResult::OrderNotFound;
}

void OrderBookStorage::clear() noexcept {
  orders_.clear();
  bids_.clear();
  asks_.clear();
}

OrderBookResult OrderBookStorage::remove_bid(
    OrderIndex::iterator indexed_order) {
  const auto level = bids_.find(indexed_order->second.price);
  if (level == bids_.end()) {
    return OrderBookResult::OrderNotFound;
  }

  const auto leaves = indexed_order->second.order->leaves_quantity;
  const auto aggregate =
      checked_subtract(level->second.aggregate_leaves_quantity, leaves);
  if (aggregate.result != QuantityArithmeticResult::Success &&
      aggregate.result != QuantityArithmeticResult::Zero) {
    return OrderBookResult::InvalidQuantity;
  }

  level->second.fifo.erase(indexed_order->second.order);
  orders_.erase(indexed_order);
  if (aggregate.result == QuantityArithmeticResult::Zero) {
    bids_.erase(level);
  } else {
    level->second.aggregate_leaves_quantity = aggregate.value;
  }
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::remove_ask(
    OrderIndex::iterator indexed_order) {
  const auto level = asks_.find(indexed_order->second.price);
  if (level == asks_.end()) {
    return OrderBookResult::OrderNotFound;
  }

  const auto leaves = indexed_order->second.order->leaves_quantity;
  const auto aggregate =
      checked_subtract(level->second.aggregate_leaves_quantity, leaves);
  if (aggregate.result != QuantityArithmeticResult::Success &&
      aggregate.result != QuantityArithmeticResult::Zero) {
    return OrderBookResult::InvalidQuantity;
  }

  level->second.fifo.erase(indexed_order->second.order);
  orders_.erase(indexed_order);
  if (aggregate.result == QuantityArithmeticResult::Zero) {
    asks_.erase(level);
  } else {
    level->second.aggregate_leaves_quantity = aggregate.value;
  }
  return OrderBookResult::Accepted;
}

InstrumentId OrderBookStorage::instrument_id() const noexcept {
  return instrument_id_;
}

std::size_t OrderBookStorage::active_order_count() const noexcept {
  return orders_.size();
}

std::size_t OrderBookStorage::price_level_count(Side side) const noexcept {
  if (side == Side::Buy) {
    return bids_.size();
  }
  if (side == Side::Sell) {
    return asks_.size();
  }
  return 0;
}

std::size_t OrderBookStorage::active_order_capacity() const noexcept {
  return active_order_capacity_;
}

std::size_t OrderBookStorage::price_level_capacity_per_side() const noexcept {
  return price_level_capacity_per_side_;
}

std::optional<RestingOrderView> OrderBookStorage::find_order(
    OrderId order_id) const noexcept {
  const auto order = orders_.find(order_id);
  if (order == orders_.end()) {
    return std::nullopt;
  }

  const auto& record = *order->second.order;
  return make_view(record.order_id, record.instrument_id, record.side,
                   record.price, record.leaves_quantity);
}

std::optional<PriceTicks> OrderBookStorage::best_bid() const noexcept {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.begin()->first;
}

std::optional<PriceTicks> OrderBookStorage::best_ask() const noexcept {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.begin()->first;
}

std::optional<DepthEntry> OrderBookStorage::level(
    Side side, PriceTicks price) const noexcept {
  if (side == Side::Buy) {
    return bid_level(price);
  }
  if (side == Side::Sell) {
    return ask_level(price);
  }
  return std::nullopt;
}

std::optional<DepthEntry> OrderBookStorage::bid_level(
    PriceTicks price) const noexcept {
  const auto level = bids_.find(price);
  if (level == bids_.end()) {
    return std::nullopt;
  }
  return DepthEntry{level->first, level->second.aggregate_leaves_quantity,
                    level->second.fifo.size()};
}

std::optional<DepthEntry> OrderBookStorage::ask_level(
    PriceTicks price) const noexcept {
  const auto level = asks_.find(price);
  if (level == asks_.end()) {
    return std::nullopt;
  }
  return DepthEntry{level->first, level->second.aggregate_leaves_quantity,
                    level->second.fifo.size()};
}

std::vector<DepthEntry> OrderBookStorage::depth(Side side) const {
  std::vector<DepthEntry> result;
  result.reserve(price_level_count(side));
  if (side == Side::Buy) {
    for (const auto& [price, level_data] : bids_) {
      result.push_back(
          {price, level_data.aggregate_leaves_quantity, level_data.fifo.size()});
    }
  } else if (side == Side::Sell) {
    for (const auto& [price, level_data] : asks_) {
      result.push_back(
          {price, level_data.aggregate_leaves_quantity, level_data.fifo.size()});
    }
  }
  return result;
}

std::vector<RestingOrderView> OrderBookStorage::orders_at_level(
    Side side, PriceTicks price) const {
  if (side == Side::Buy) {
    return bid_orders(price);
  }
  if (side == Side::Sell) {
    return ask_orders(price);
  }
  return {};
}

std::vector<RestingOrderView> OrderBookStorage::bid_orders(
    PriceTicks price) const {
  std::vector<RestingOrderView> result;
  const auto level = bids_.find(price);
  if (level == bids_.end()) {
    return result;
  }

  result.reserve(level->second.fifo.size());
  for (const auto& order : level->second.fifo) {
    result.push_back(make_view(order.order_id, order.instrument_id, order.side,
                               order.price, order.leaves_quantity));
  }
  return result;
}

std::vector<RestingOrderView> OrderBookStorage::ask_orders(
    PriceTicks price) const {
  std::vector<RestingOrderView> result;
  const auto level = asks_.find(price);
  if (level == asks_.end()) {
    return result;
  }

  result.reserve(level->second.fifo.size());
  for (const auto& order : level->second.fifo) {
    result.push_back(make_view(order.order_id, order.instrument_id, order.side,
                               order.price, order.leaves_quantity));
  }
  return result;
}

#ifndef NDEBUG
bool OrderBookStorage::validate_invariants() const noexcept {
  if (orders_.size() > active_order_capacity_ ||
      bids_.size() > price_level_capacity_per_side_ ||
      asks_.size() > price_level_capacity_per_side_) {
    return false;
  }

  std::size_t reachable_orders = 0;
  auto validate_side = [this, &reachable_orders](const auto& levels,
                                                 Side expected_side) noexcept {
    bool first_level = true;
    PriceTicks previous_price{};
    for (const auto& [price, level_data] : levels) {
      if (!price.is_valid() || level_data.fifo.empty() ||
          !level_data.aggregate_leaves_quantity.is_valid()) {
        return false;
      }
      if (!first_level) {
        if (expected_side == Side::Buy && !(previous_price > price)) {
          return false;
        }
        if (expected_side == Side::Sell && !(previous_price < price)) {
          return false;
        }
      }
      first_level = false;
      previous_price = price;

      std::uint64_t aggregate = 0;
      for (auto order = level_data.fifo.begin(); order != level_data.fifo.end();
           ++order) {
        if (!order->order_id.is_valid() || order->instrument_id != instrument_id_ ||
            order->side != expected_side || order->price != price ||
            !order->leaves_quantity.is_valid()) {
          return false;
        }
        if (order->leaves_quantity.value() >
            Quantity::maximum_value - aggregate) {
          return false;
        }
        aggregate += order->leaves_quantity.value();

        const auto indexed_order = orders_.find(order->order_id);
        if (indexed_order == orders_.end() ||
            indexed_order->second.side != expected_side ||
            indexed_order->second.price != price ||
            std::addressof(*indexed_order->second.order) !=
                std::addressof(*order)) {
          return false;
        }
        ++reachable_orders;
      }
      if (aggregate != level_data.aggregate_leaves_quantity.value()) {
        return false;
      }
    }
    return true;
  };

  if (!validate_side(bids_, Side::Buy) ||
      !validate_side(asks_, Side::Sell) ||
      reachable_orders != orders_.size()) {
    return false;
  }

  for (const auto& [order_id, location] : orders_) {
    if (location.order->order_id != order_id ||
        location.order->side != location.side ||
        location.order->price != location.price) {
      return false;
    }
  }

  if (bids_.empty() != !best_bid().has_value() ||
      asks_.empty() != !best_ask().has_value()) {
    return false;
  }
  if (!bids_.empty() && best_bid() != std::optional<PriceTicks>{bids_.begin()->first}) {
    return false;
  }
  if (!asks_.empty() && best_ask() != std::optional<PriceTicks>{asks_.begin()->first}) {
    return false;
  }

  return true;
}
#endif

}  // namespace lob
