#include "lob/storage/order_book_storage.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <limits>

namespace lob {

namespace {

[[noreturn]] void fail_storage_invariant() noexcept { std::abort(); }

[[nodiscard]] RestingOrderView make_view(
    OrderId order_id, InstrumentId instrument_id, Side side, PriceTicks price,
    Quantity leaves_quantity) noexcept {
  return {order_id, instrument_id, side, price, leaves_quantity};
}

[[nodiscard]] std::size_t physical_capacity(std::size_t logical) noexcept {
  return std::max(logical, std::size_t{1});
}

}  // namespace

OrderBookStorage::ActiveOrderIndex::ActiveOrderIndex(
    std::size_t maximum_entries) {
  const auto target = std::max(std::size_t{2}, maximum_entries * 2);
  capacity_ = 2;
  while (capacity_ < target) {
    capacity_ *= 2;
  }
  mask_ = capacity_ - 1;
  controls_ = std::make_unique<Control[]>(capacity_);
  payloads_ = std::make_unique<Payload[]>(capacity_);
}

std::size_t OrderBookStorage::ActiveOrderIndex::bucket(
    OrderId order_id) const noexcept {
  std::uint64_t value = order_id.value();
  value ^= value >> 33;
  value *= UINT64_C(0xff51afd7ed558ccd);
  value ^= value >> 33;
  value *= UINT64_C(0xc4ceb9fe1a85ec53);
  value ^= value >> 33;
  return static_cast<std::size_t>(value) & mask_;
}

std::size_t OrderBookStorage::ActiveOrderIndex::find_position(
    OrderId order_id) const noexcept {
  auto position = bucket(order_id);
  for (std::size_t probe = 0; probe < capacity_; ++probe) {
    if (controls_[position] == kEmpty) {
      return capacity_;
    }
    if (payloads_[position].order_id == order_id) {
      return position;
    }
    position = (position + 1) & mask_;
  }
  return capacity_;
}

const OrderBookStorage::NodeLink*
OrderBookStorage::ActiveOrderIndex::find(OrderId order_id) const noexcept {
  const auto position = find_position(order_id);
  return position == capacity_ ? nullptr : &payloads_[position].link;
}

OrderBookStorage::NodeLink* OrderBookStorage::ActiveOrderIndex::find(
    OrderId order_id) noexcept {
  const auto position = find_position(order_id);
  return position == capacity_ ? nullptr : &payloads_[position].link;
}

std::optional<OrderBookStorage::PreparedRemoval>
OrderBookStorage::ActiveOrderIndex::prepare_removal(
    OrderId order_id) const noexcept {
  const auto position = find_position(order_id);
  if (position == capacity_) {
    return std::nullopt;
  }
  const Payload& payload = payloads_[position];
  return PreparedRemoval{position, payload.order_id, payload.link};
}

bool OrderBookStorage::ActiveOrderIndex::matches_at(
    std::size_t position, OrderId expected_order_id,
    NodeLink expected_link) const noexcept {
  return position < capacity_ && controls_[position] == kOccupied &&
         payloads_[position].order_id == expected_order_id &&
         payloads_[position].link == expected_link;
}

bool OrderBookStorage::ActiveOrderIndex::insert(OrderId order_id,
                                                NodeLink link) noexcept {
  if (!order_id.is_valid() || link.is_invalid() || size_ * 2 >= capacity_) {
    return false;
  }
  auto position = bucket(order_id);
  for (std::size_t probe = 0; probe < capacity_; ++probe) {
    if (controls_[position] == kEmpty) {
      payloads_[position] = {order_id, link};
      controls_[position] = kOccupied;
      ++size_;
      return true;
    }
    if (payloads_[position].order_id == order_id) {
      return false;
    }
    position = (position + 1) & mask_;
  }
  return false;
}

bool OrderBookStorage::ActiveOrderIndex::erase(OrderId order_id) noexcept {
  auto position = find_position(order_id);
  if (position == capacity_) {
    return false;
  }

  controls_[position] = kEmpty;
  --size_;
  position = (position + 1) & mask_;
  while (controls_[position] == kOccupied) {
    const Payload displaced = payloads_[position];
    controls_[position] = kEmpty;
    --size_;
    if (!insert(displaced.order_id, displaced.link)) {
      fail_storage_invariant();
    }
    position = (position + 1) & mask_;
  }
  return true;
}

bool OrderBookStorage::ActiveOrderIndex::erase_at(
    std::size_t position, OrderId expected_order_id,
    NodeLink expected_link) noexcept {
  if (!matches_at(position, expected_order_id, expected_link)) {
    return false;
  }
  controls_[position] = kEmpty;
  --size_;
  position = (position + 1) & mask_;
  while (controls_[position] == kOccupied) {
    const Payload displaced = payloads_[position];
    controls_[position] = kEmpty;
    --size_;
    if (!insert(displaced.order_id, displaced.link)) {
      fail_storage_invariant();
    }
    position = (position + 1) & mask_;
  }
  return true;
}

void OrderBookStorage::ActiveOrderIndex::clear() noexcept {
  std::fill_n(controls_.get(), capacity_, kEmpty);
  size_ = 0;
}

std::size_t OrderBookStorage::ActiveOrderIndex::size() const noexcept {
  return size_;
}

std::size_t OrderBookStorage::ActiveOrderIndex::capacity() const noexcept {
  return capacity_;
}

std::size_t OrderBookStorage::ActiveOrderIndex::backing_memory_bytes()
    const noexcept {
  return capacity_ * (sizeof(Control) + sizeof(Payload));
}

#ifndef NDEBUG
bool OrderBookStorage::ActiveOrderIndex::validate_invariants() const noexcept {
  if (capacity_ < 2 || (capacity_ & (capacity_ - 1)) != 0 ||
      mask_ != capacity_ - 1 || size_ * 2 > capacity_) {
    return false;
  }
  std::size_t occupied = 0;
  for (std::size_t index = 0; index < capacity_; ++index) {
    if (controls_[index] != kEmpty && controls_[index] != kOccupied) {
      return false;
    }
    if (controls_[index] == kEmpty) {
      continue;
    }
    const Payload& payload = payloads_[index];
    if (!payload.order_id.is_valid() || payload.link.is_invalid() ||
        find_position(payload.order_id) != index) {
      return false;
    }
    ++occupied;
  }
  return occupied == size_;
}
#endif

OrderBookStorage::OrderBookStorage(InstrumentId instrument_id,
                                   StorageLimits limits)
    : instrument_id_(instrument_id),
      active_order_capacity_(
          std::min(limits.active_orders, kMaximumActiveOrders)),
      price_level_capacity_per_side_(std::min(
          limits.price_levels_per_side, kMaximumPriceLevelsPerSide)),
      order_pool_(physical_capacity(active_order_capacity_)),
      orders_(active_order_capacity_),
      bids_(std::make_unique<PriceLevel[]>(
          physical_capacity(price_level_capacity_per_side_))),
      asks_(std::make_unique<PriceLevel[]>(
          physical_capacity(price_level_capacity_per_side_))) {}

OrderBookStorage::NodeLink OrderBookStorage::to_link(
    OrderHandle handle) noexcept {
  return {handle.index(), handle.generation(), handle.epoch()};
}

OrderBookStorage::OrderHandle OrderBookStorage::to_handle(
    NodeLink link) const noexcept {
  return OrderHandle::from_raw_parts(&order_pool_, link.index, link.generation,
                                     link.epoch);
}

OrderBookStorage::OrderRecord* OrderBookStorage::get(NodeLink link) noexcept {
  return link.is_invalid() ? nullptr : order_pool_.get(to_handle(link));
}

const OrderBookStorage::OrderRecord* OrderBookStorage::get(
    NodeLink link) const noexcept {
  return link.is_invalid() ? nullptr : order_pool_.get(to_handle(link));
}

std::size_t OrderBookStorage::find_level_position(
    const PriceLevel* levels, std::size_t count, Side side,
    PriceTicks price) const noexcept {
  std::size_t first = 0;
  std::size_t last = count;
  while (first < last) {
    const auto middle = first + (last - first) / 2;
    const bool before = side == Side::Buy ? levels[middle].price > price
                                          : levels[middle].price < price;
    if (before) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  return first;
}

OrderBookStorage::PriceLevel* OrderBookStorage::find_level(
    Side side, PriceTicks price) noexcept {
  PriceLevel* levels = side == Side::Buy ? bids_.get() : asks_.get();
  const auto count = side == Side::Buy ? bid_level_count_ : ask_level_count_;
  if (side != Side::Buy && side != Side::Sell) {
    return nullptr;
  }
  const auto position = find_level_position(levels, count, side, price);
  return position < count && levels[position].price == price
             ? &levels[position]
             : nullptr;
}

const OrderBookStorage::PriceLevel* OrderBookStorage::find_level(
    Side side, PriceTicks price) const noexcept {
  const PriceLevel* levels = side == Side::Buy ? bids_.get() : asks_.get();
  const auto count = side == Side::Buy ? bid_level_count_ : ask_level_count_;
  if (side != Side::Buy && side != Side::Sell) {
    return nullptr;
  }
  const auto position = find_level_position(levels, count, side, price);
  return position < count && levels[position].price == price
             ? &levels[position]
             : nullptr;
}

OrderBookStorage::PriceLevel& OrderBookStorage::create_level(
    Side side, PriceTicks price) noexcept {
  PriceLevel* levels = side == Side::Buy ? bids_.get() : asks_.get();
  std::size_t& count = side == Side::Buy ? bid_level_count_ : ask_level_count_;
  std::size_t& high_water = side == Side::Buy
                                ? bid_level_high_water_count_
                                : ask_level_high_water_count_;
  if (count >= price_level_capacity_per_side_) {
    fail_storage_invariant();
  }
  const auto position = find_level_position(levels, count, side, price);
  for (std::size_t index = count; index > position; --index) {
    levels[index] = levels[index - 1];
  }
  levels[position] = PriceLevel{price};
  ++count;
  high_water = std::max(high_water, count);
  return levels[position];
}

void OrderBookStorage::erase_level(Side side, std::size_t position) noexcept {
  PriceLevel* levels = side == Side::Buy ? bids_.get() : asks_.get();
  std::size_t& count = side == Side::Buy ? bid_level_count_ : ask_level_count_;
  if (position >= count) {
    fail_storage_invariant();
  }
  for (std::size_t index = position + 1; index < count; ++index) {
    levels[index - 1] = levels[index];
  }
  --count;
  levels[count] = {};
}

void OrderBookStorage::append_to_level(PriceLevel& level,
                                       NodeLink link) noexcept {
  OrderRecord* order = get(link);
  if (order == nullptr) {
    fail_storage_invariant();
  }
  order->previous = level.tail;
  order->next = {};
  if (level.tail.is_invalid()) {
    level.head = link;
  } else {
    OrderRecord* tail = get(level.tail);
    if (tail == nullptr) {
      fail_storage_invariant();
    }
    tail->next = link;
  }
  level.tail = link;
  ++level.order_count;
}

void OrderBookStorage::unlink_from_level(PriceLevel& level,
                                          NodeLink link) noexcept {
  OrderRecord* order = get(link);
  if (order == nullptr || level.order_count == 0) {
    fail_storage_invariant();
  }
  if (order->previous.is_invalid()) {
    level.head = order->next;
  } else {
    OrderRecord* previous = get(order->previous);
    if (previous == nullptr) {
      fail_storage_invariant();
    }
    previous->next = order->next;
  }
  if (order->next.is_invalid()) {
    level.tail = order->previous;
  } else {
    OrderRecord* next = get(order->next);
    if (next == nullptr) {
      fail_storage_invariant();
    }
    next->previous = order->previous;
  }
  order->previous = {};
  order->next = {};
  --level.order_count;
}

OrderBookResult OrderBookStorage::insert_resting(
    OrderId order_id, InstrumentId instrument_id, Side side, PriceTicks price,
    Quantity leaves_quantity) noexcept {
  if (!instrument_id_.is_valid() || instrument_id != instrument_id_) {
    return OrderBookResult::InvalidInstrument;
  }
  if (!order_id.is_valid() || orders_.find(order_id) != nullptr) {
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
  if (orders_.size() >= active_order_capacity_ ||
      order_pool_.next_acquire_status() != PoolAcquireStatus::Acquired) {
    return OrderBookResult::CapacityExhausted;
  }

  PriceLevel* destination = find_level(side, price);
  Quantity aggregate = leaves_quantity;
  if (destination == nullptr) {
    if (price_level_count(side) >= price_level_capacity_per_side_) {
      return OrderBookResult::CapacityExhausted;
    }
  } else {
    const auto sum = checked_add(destination->aggregate_leaves_quantity,
                                 leaves_quantity);
    if (!sum.has_value()) {
      return OrderBookResult::CapacityExhausted;
    }
    aggregate = sum.value;
  }

  const auto acquired = order_pool_.acquire(OrderRecord{
      order_id, instrument_id, side, price, leaves_quantity, {}, {}});
  if (!acquired.acquired()) {
    fail_storage_invariant();
  }
  const NodeLink link = to_link(acquired.handle);
  if (!orders_.insert(order_id, link)) {
    fail_storage_invariant();
  }
  if (destination == nullptr) {
    destination = &create_level(side, price);
  }
  append_to_level(*destination, link);
  destination->aggregate_leaves_quantity = aggregate;
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::remove_link(OrderId order_id,
                                               NodeLink link) noexcept {
  OrderRecord* order = get(link);
  if (order == nullptr || order->order_id != order_id) {
    return OrderBookResult::OrderNotFound;
  }
  PriceLevel* price_level = find_level(order->side, order->price);
  if (price_level == nullptr) {
    return OrderBookResult::OrderNotFound;
  }
  PriceLevel* levels = order->side == Side::Buy ? bids_.get() : asks_.get();
  const Side side = order->side;
  const auto level_position = static_cast<std::size_t>(price_level - levels);
  const auto aggregate = checked_subtract(
      price_level->aggregate_leaves_quantity, order->leaves_quantity);
  if (aggregate.result != QuantityArithmeticResult::Success &&
      aggregate.result != QuantityArithmeticResult::Zero) {
    return OrderBookResult::InvalidQuantity;
  }

  unlink_from_level(*price_level, link);
  if (!orders_.erase(order_id) ||
      order_pool_.release(to_handle(link)) != PoolReleaseStatus::Released) {
    fail_storage_invariant();
  }
  if (aggregate.result == QuantityArithmeticResult::Zero) {
    if (price_level->order_count != 0) {
      fail_storage_invariant();
    }
    erase_level(side, level_position);
  } else {
    price_level->aggregate_leaves_quantity = aggregate.value;
  }
  return OrderBookResult::Accepted;
}

std::optional<OrderBookStorage::PreparedRemoval>
OrderBookStorage::prepare_removal(OrderId order_id) const noexcept {
  if (!order_id.is_valid()) {
    return std::nullopt;
  }
  const auto prepared = orders_.prepare_removal(order_id);
  if (!prepared || prepared->order_id != order_id) {
    return std::nullopt;
  }
  const OrderRecord* order = get(prepared->link);
  return order != nullptr && order->order_id == order_id
             ? prepared
             : std::nullopt;
}

OrderBookResult OrderBookStorage::remove_prepared(
    const PreparedRemoval& prepared) noexcept {
  if (!orders_.matches_at(prepared.active_index_position, prepared.order_id,
                          prepared.link)) {
    return OrderBookResult::OrderNotFound;
  }
  OrderRecord* order = get(prepared.link);
  if (order == nullptr || order->order_id != prepared.order_id) {
    return OrderBookResult::OrderNotFound;
  }
  PriceLevel* price_level = find_level(order->side, order->price);
  if (price_level == nullptr) {
    return OrderBookResult::OrderNotFound;
  }
  PriceLevel* levels = order->side == Side::Buy ? bids_.get() : asks_.get();
  const Side side = order->side;
  const auto level_position = static_cast<std::size_t>(price_level - levels);
  const auto aggregate = checked_subtract(
      price_level->aggregate_leaves_quantity, order->leaves_quantity);
  if (aggregate.result != QuantityArithmeticResult::Success &&
      aggregate.result != QuantityArithmeticResult::Zero) {
    return OrderBookResult::InvalidQuantity;
  }

  unlink_from_level(*price_level, prepared.link);
  if (!orders_.erase_at(prepared.active_index_position, prepared.order_id,
                        prepared.link) ||
      order_pool_.release(to_handle(prepared.link)) !=
          PoolReleaseStatus::Released) {
    fail_storage_invariant();
  }
  if (aggregate.result == QuantityArithmeticResult::Zero) {
    if (price_level->order_count != 0) {
      fail_storage_invariant();
    }
    erase_level(side, level_position);
  } else {
    price_level->aggregate_leaves_quantity = aggregate.value;
  }
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::remove_resting(OrderId order_id) noexcept {
  const NodeLink* link = orders_.find(order_id);
  return link == nullptr ? OrderBookResult::OrderNotFound
                         : remove_link(order_id, *link);
}

OrderBookResult OrderBookStorage::reduce_resting_by(
    OrderId order_id, Quantity reduction) noexcept {
  const NodeLink* link = orders_.find(order_id);
  OrderRecord* order = link == nullptr ? nullptr : get(*link);
  if (order == nullptr) {
    return OrderBookResult::OrderNotFound;
  }
  if (!reduction.is_valid() || reduction > order->leaves_quantity) {
    return OrderBookResult::InvalidQuantity;
  }
  if (reduction == order->leaves_quantity) {
    return remove_link(order_id, *link);
  }
  const auto new_leaves = checked_subtract(order->leaves_quantity, reduction);
  PriceLevel* price_level = find_level(order->side, order->price);
  if (!new_leaves.has_value() || price_level == nullptr) {
    return OrderBookResult::InvalidQuantity;
  }
  const auto aggregate = checked_subtract(
      price_level->aggregate_leaves_quantity, reduction);
  if (!aggregate.has_value()) {
    return OrderBookResult::InvalidQuantity;
  }
  order->leaves_quantity = new_leaves.value;
  price_level->aggregate_leaves_quantity = aggregate.value;
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::update_resting_leaves(
    OrderId order_id, Quantity new_leaves_quantity) noexcept {
  const NodeLink* link = orders_.find(order_id);
  OrderRecord* order = link == nullptr ? nullptr : get(*link);
  if (order == nullptr) {
    return OrderBookResult::OrderNotFound;
  }
  if (!new_leaves_quantity.is_valid()) {
    return OrderBookResult::InvalidQuantity;
  }
  if (new_leaves_quantity == order->leaves_quantity) {
    return OrderBookResult::Accepted;
  }
  PriceLevel* price_level = find_level(order->side, order->price);
  if (price_level == nullptr) {
    return OrderBookResult::OrderNotFound;
  }

  CheckedQuantity aggregate;
  if (new_leaves_quantity < order->leaves_quantity) {
    const auto reduction = checked_subtract(order->leaves_quantity,
                                            new_leaves_quantity);
    if (!reduction.has_value()) {
      return OrderBookResult::InvalidQuantity;
    }
    aggregate = checked_subtract(price_level->aggregate_leaves_quantity,
                                 reduction.value);
    if (!aggregate.has_value()) {
      return OrderBookResult::InvalidQuantity;
    }
  } else {
    const auto increase = checked_subtract(new_leaves_quantity,
                                           order->leaves_quantity);
    if (!increase.has_value()) {
      return OrderBookResult::InvalidQuantity;
    }
    aggregate = checked_add(price_level->aggregate_leaves_quantity,
                            increase.value);
    if (!aggregate.has_value()) {
      return OrderBookResult::CapacityExhausted;
    }
  }
  order->leaves_quantity = new_leaves_quantity;
  price_level->aggregate_leaves_quantity = aggregate.value;
  return OrderBookResult::Accepted;
}

OrderBookResult OrderBookStorage::move_resting_to_back(
    OrderId order_id) noexcept {
  const NodeLink* indexed = orders_.find(order_id);
  if (indexed == nullptr) {
    return OrderBookResult::OrderNotFound;
  }
  const NodeLink link = *indexed;
  OrderRecord* order = get(link);
  if (order == nullptr) {
    return OrderBookResult::OrderNotFound;
  }
  PriceLevel* price_level = find_level(order->side, order->price);
  if (price_level == nullptr) {
    return OrderBookResult::OrderNotFound;
  }
  if (price_level->tail == link) {
    return OrderBookResult::Accepted;
  }
  unlink_from_level(*price_level, link);
  append_to_level(*price_level, link);
  return OrderBookResult::Accepted;
}

bool OrderBookStorage::can_insert_after_removing(
    std::optional<OrderId> first_removed_order) const noexcept {
  if (order_pool_.free_count() != 0) {
    return order_pool_.next_acquire_status() == PoolAcquireStatus::Acquired;
  }
  if (!first_removed_order) {
    return false;
  }
  const NodeLink* link = orders_.find(*first_removed_order);
  return link != nullptr &&
         order_pool_.acquire_status_after_release(to_handle(*link)) ==
             PoolAcquireStatus::Acquired;
}

bool OrderBookStorage::can_clear() const noexcept {
  return order_pool_.can_reset();
}

void OrderBookStorage::clear() noexcept {
  if (!can_clear()) {
    fail_storage_invariant();
  }
  orders_.clear();
  bid_level_count_ = 0;
  ask_level_count_ = 0;
  const auto reset = order_pool_.reset();
  if (reset != PoolResetStatus::Reset) {
    fail_storage_invariant();
  }
}

InstrumentId OrderBookStorage::instrument_id() const noexcept {
  return instrument_id_;
}

std::size_t OrderBookStorage::active_order_count() const noexcept {
  return orders_.size();
}

std::size_t OrderBookStorage::price_level_count(Side side) const noexcept {
  if (side == Side::Buy) {
    return bid_level_count_;
  }
  return side == Side::Sell ? ask_level_count_ : 0;
}

std::size_t OrderBookStorage::active_order_capacity() const noexcept {
  return active_order_capacity_;
}

std::size_t OrderBookStorage::price_level_capacity_per_side() const noexcept {
  return price_level_capacity_per_side_;
}

StorageDiagnostics OrderBookStorage::diagnostics() const noexcept {
  const auto level_bytes =
      2 * physical_capacity(price_level_capacity_per_side_) *
      sizeof(PriceLevel);
  const auto pool_bytes = order_pool_.backing_memory_bytes();
  const auto index_bytes = orders_.backing_memory_bytes();
  return {active_order_capacity_,
          order_pool_.used_count(),
          order_pool_.free_count(),
          order_pool_.high_water_count(),
          bid_level_high_water_count_,
          ask_level_high_water_count_,
          orders_.capacity(),
          sizeof(OrderRecord),
          alignof(OrderRecord),
          OrderPool::slot_size_bytes(),
          OrderPool::slot_alignment_bytes(),
          order_pool_.slot_backing_memory_bytes(),
          order_pool_.free_index_backing_memory_bytes(),
          pool_bytes,
          index_bytes,
          level_bytes,
          pool_bytes + index_bytes + level_bytes};
}

std::optional<RestingOrderView> OrderBookStorage::find_order(
    OrderId order_id) const noexcept {
  const NodeLink* link = orders_.find(order_id);
  const OrderRecord* order = link == nullptr ? nullptr : get(*link);
  if (order == nullptr) {
    return std::nullopt;
  }
  return make_view(order->order_id, order->instrument_id, order->side,
                   order->price, order->leaves_quantity);
}

std::optional<PriceTicks> OrderBookStorage::best_bid() const noexcept {
  return bid_level_count_ == 0 ? std::nullopt
                               : std::optional<PriceTicks>{bids_[0].price};
}

std::optional<PriceTicks> OrderBookStorage::best_ask() const noexcept {
  return ask_level_count_ == 0 ? std::nullopt
                               : std::optional<PriceTicks>{asks_[0].price};
}

std::optional<DepthEntry> OrderBookStorage::level(
    Side side, PriceTicks price) const noexcept {
  const PriceLevel* found = find_level(side, price);
  return found == nullptr
             ? std::nullopt
             : std::optional<DepthEntry>{{found->price,
                                          found->aggregate_leaves_quantity,
                                          found->order_count}};
}

std::vector<DepthEntry> OrderBookStorage::depth(Side side) const {
  std::vector<DepthEntry> result;
  const PriceLevel* levels = side == Side::Buy ? bids_.get() : asks_.get();
  const auto count = price_level_count(side);
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back({levels[index].price,
                      levels[index].aggregate_leaves_quantity,
                      levels[index].order_count});
  }
  return result;
}

std::vector<RestingOrderView> OrderBookStorage::level_orders(
    Side side, PriceTicks price) const {
  std::vector<RestingOrderView> result;
  const PriceLevel* price_level = find_level(side, price);
  if (price_level == nullptr) {
    return result;
  }
  result.reserve(price_level->order_count);
  NodeLink link = price_level->head;
  while (!link.is_invalid()) {
    const OrderRecord* order = get(link);
    if (order == nullptr) {
      fail_storage_invariant();
    }
    result.push_back(make_view(order->order_id, order->instrument_id,
                               order->side, order->price,
                               order->leaves_quantity));
    link = order->next;
  }
  return result;
}

std::vector<RestingOrderView> OrderBookStorage::orders_at_level(
    Side side, PriceTicks price) const {
  return side == Side::Buy || side == Side::Sell
             ? level_orders(side, price)
             : std::vector<RestingOrderView>{};
}

#ifndef NDEBUG
bool OrderBookStorage::validate_invariants() const noexcept {
  if (orders_.size() > active_order_capacity_ ||
      bid_level_count_ > price_level_capacity_per_side_ ||
      ask_level_count_ > price_level_capacity_per_side_ ||
      order_pool_.used_count() != orders_.size() ||
      order_pool_.used_count() + order_pool_.free_count() !=
          order_pool_.capacity() ||
      order_pool_.high_water_count() < order_pool_.used_count() ||
      bid_level_high_water_count_ < bid_level_count_ ||
      ask_level_high_water_count_ < ask_level_count_ ||
      !orders_.validate_invariants() || !order_pool_.validate_invariants()) {
    return false;
  }

  std::size_t reachable_orders = 0;
  const auto validate_side = [&](const PriceLevel* levels, std::size_t count,
                                 Side expected_side) noexcept {
    for (std::size_t level_index = 0; level_index < count; ++level_index) {
      const PriceLevel& price_level = levels[level_index];
      if (!price_level.price.is_valid() ||
          !price_level.aggregate_leaves_quantity.is_valid() ||
          price_level.order_count == 0 || price_level.head.is_invalid() ||
          price_level.tail.is_invalid()) {
        return false;
      }
      if (level_index != 0) {
        const bool ordered = expected_side == Side::Buy
                                 ? levels[level_index - 1].price >
                                       price_level.price
                                 : levels[level_index - 1].price <
                                       price_level.price;
        if (!ordered) {
          return false;
        }
      }

      std::uint64_t aggregate = 0;
      std::size_t order_count = 0;
      NodeLink previous{};
      NodeLink link = price_level.head;
      while (!link.is_invalid()) {
        const OrderRecord* order = get(link);
        if (order == nullptr || order->previous != previous ||
            !order->order_id.is_valid() ||
            order->instrument_id != instrument_id_ ||
            order->side != expected_side || order->price != price_level.price ||
            !order->leaves_quantity.is_valid() ||
            order->leaves_quantity.value() >
                Quantity::maximum_value - aggregate) {
          return false;
        }
        const NodeLink* indexed = orders_.find(order->order_id);
        if (indexed == nullptr || *indexed != link) {
          return false;
        }
        aggregate += order->leaves_quantity.value();
        ++order_count;
        ++reachable_orders;
        if (order_count > orders_.size()) {
          return false;
        }
        previous = link;
        link = order->next;
      }
      if (previous != price_level.tail || order_count != price_level.order_count ||
          aggregate != price_level.aggregate_leaves_quantity.value()) {
        return false;
      }
    }
    return true;
  };

  if (!validate_side(bids_.get(), bid_level_count_, Side::Buy) ||
      !validate_side(asks_.get(), ask_level_count_, Side::Sell) ||
      reachable_orders != orders_.size()) {
    return false;
  }
  if ((bid_level_count_ == 0) != !best_bid().has_value() ||
      (ask_level_count_ == 0) != !best_ask().has_value()) {
    return false;
  }
  return true;
}
#endif

}  // namespace lob
