#include "phase1_reference_model.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace phase1_test {

namespace {

template <typename Domain, typename Source>
[[nodiscard]] Domain domain_or_zero(Source value) noexcept {
  if (value == Source{0}) {
    return {};
  }
  const auto converted = lob::checked_domain_cast<Domain>(value);
  return converted.has_value() ? converted.value : Domain{};
}

template <typename Domain, typename Source>
[[nodiscard]] Domain required_domain(Source value) noexcept {
  const auto converted = lob::checked_domain_cast<Domain>(value);
  if (!converted.has_value()) {
    std::abort();
  }
  return converted.value;
}

[[nodiscard]] char side_code(lob::Side side) noexcept {
  if (side == lob::Side::Buy) {
    return 'B';
  }
  if (side == lob::Side::Sell) {
    return 'S';
  }
  return 'I';
}

[[nodiscard]] lob::Side parse_side(char value) noexcept {
  if (value == 'B') {
    return lob::Side::Buy;
  }
  if (value == 'S') {
    return lob::Side::Sell;
  }
  return lob::Side::Invalid;
}

[[nodiscard]] bool crosses(lob::Side side, std::int64_t aggressive,
                           std::int64_t resting) noexcept {
  return side == lob::Side::Buy ? aggressive >= resting
                                : aggressive <= resting;
}

[[nodiscard]] std::uint64_t aggregate(const ReferenceModel::Level& level,
                                      bool& valid) noexcept {
  std::uint64_t total = 0;
  for (const auto& order : level) {
    if (order.leaves.value() >
        std::numeric_limits<std::uint64_t>::max() - total) {
      valid = false;
      return 0;
    }
    total += order.leaves.value();
  }
  valid = true;
  return total;
}

}  // namespace

std::string serialize_command(const TraceCommand& command) {
  std::ostringstream output;
  switch (command.kind) {
    case TraceCommandKind::New:
      output << "N " << command.order_id.value() << ' '
             << command.instrument_id.value() << ' ' << side_code(command.side)
             << ' ' << command.price.value() << ' ' << command.quantity.value();
      break;
    case TraceCommandKind::Cancel:
      output << "C " << command.order_id.value() << ' '
             << command.instrument_id.value();
      break;
    case TraceCommandKind::Amend:
      output << "A " << command.order_id.value() << ' '
             << command.instrument_id.value() << ' ' << command.price.value()
             << ' ' << command.quantity.value();
      break;
    case TraceCommandKind::Halt:
      output << "H " << command.instrument_id.value();
      break;
    case TraceCommandKind::Resume:
      output << "R " << command.instrument_id.value();
      break;
    case TraceCommandKind::Close:
      output << "X " << command.instrument_id.value();
      break;
    case TraceCommandKind::Open:
      output << "O " << command.instrument_id.value();
      break;
  }
  return output.str();
}

std::optional<TraceCommand> parse_command(std::string_view line) {
  std::istringstream input{std::string{line}};
  char kind = 0;
  if (!(input >> kind)) {
    return std::nullopt;
  }

  TraceCommand command;
  std::uint64_t order = 0;
  std::uint64_t instrument = 0;
  std::int64_t price = 0;
  std::uint64_t quantity = 0;
  char side = 0;
  switch (kind) {
    case 'N':
      command.kind = TraceCommandKind::New;
      if (!(input >> order >> instrument >> side >> price >> quantity)) {
        return std::nullopt;
      }
      command.order_id = domain_or_zero<lob::OrderId>(order);
      command.instrument_id = domain_or_zero<lob::InstrumentId>(instrument);
      command.side = parse_side(side);
      command.price = domain_or_zero<lob::PriceTicks>(price);
      command.quantity = domain_or_zero<lob::Quantity>(quantity);
      break;
    case 'C':
      command.kind = TraceCommandKind::Cancel;
      if (!(input >> order >> instrument)) {
        return std::nullopt;
      }
      command.order_id = domain_or_zero<lob::OrderId>(order);
      command.instrument_id = domain_or_zero<lob::InstrumentId>(instrument);
      break;
    case 'A':
      command.kind = TraceCommandKind::Amend;
      if (!(input >> order >> instrument >> price >> quantity)) {
        return std::nullopt;
      }
      command.order_id = domain_or_zero<lob::OrderId>(order);
      command.instrument_id = domain_or_zero<lob::InstrumentId>(instrument);
      command.price = domain_or_zero<lob::PriceTicks>(price);
      command.quantity = domain_or_zero<lob::Quantity>(quantity);
      break;
    case 'H':
    case 'R':
    case 'X':
    case 'O':
      if (!(input >> instrument)) {
        return std::nullopt;
      }
      command.kind = kind == 'H'   ? TraceCommandKind::Halt
                     : kind == 'R' ? TraceCommandKind::Resume
                     : kind == 'X' ? TraceCommandKind::Close
                                   : TraceCommandKind::Open;
      command.instrument_id = domain_or_zero<lob::InstrumentId>(instrument);
      break;
    default:
      return std::nullopt;
  }

  std::string extra;
  if (input >> extra) {
    return std::nullopt;
  }
  if (instrument > std::numeric_limits<std::uint32_t>::max() ||
      (price < 0 && (kind == 'N' || kind == 'A'))) {
    return std::nullopt;
  }
  if ((kind == 'N' && side != 'B' && side != 'S' && side != 'I') ||
      (order != 0 && !command.order_id.is_valid()) ||
      (instrument != 0 && !command.instrument_id.is_valid()) ||
      (price != 0 && !command.price.is_valid()) ||
      (quantity != 0 && !command.quantity.is_valid())) {
    return std::nullopt;
  }
  return command;
}

std::string serialize_trace(const std::vector<TraceCommand>& trace) {
  std::string result;
  for (const auto& command : trace) {
    result += serialize_command(command);
    result.push_back('\n');
  }
  return result;
}

std::optional<std::vector<TraceCommand>> parse_trace(std::string_view text) {
  std::vector<TraceCommand> trace;
  std::istringstream input{std::string{text}};
  std::string line;
  while (std::getline(input, line)) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    const auto parsed = parse_command(line);
    if (!parsed) {
      return std::nullopt;
    }
    trace.push_back(*parsed);
  }
  return trace;
}

std::optional<std::vector<TraceCommand>> load_trace(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  return parse_trace(contents.str());
}

ReferenceModel::ReferenceModel(lob::InstrumentId instrument_id,
                               lob::StorageLimits storage_limits,
                               lob::LosslessOutboxLimits outbox_limits)
    : instrument_id_(instrument_id),
      active_order_capacity_(std::min(storage_limits.active_orders,
                                      lob::kMaximumActiveOrders)),
      price_level_capacity_(std::min(storage_limits.price_levels_per_side,
                                     lob::kMaximumPriceLevelsPerSide)),
      execution_outbox_capacity_(outbox_limits.execution_reports),
      status_outbox_capacity_(outbox_limits.status_events) {}

StepResult ReferenceModel::apply(const TraceCommand& command) {
  switch (command.kind) {
    case TraceCommandKind::New:
      return apply_new(command);
    case TraceCommandKind::Cancel:
      return apply_cancel(command);
    case TraceCommandKind::Amend:
      return apply_amend(command);
    case TraceCommandKind::Halt:
    case TraceCommandKind::Resume:
    case TraceCommandKind::Close:
    case TraceCommandKind::Open:
      return apply_lifecycle(command);
  }
  std::abort();
}

StepResult ReferenceModel::apply_new(const TraceCommand& command) {
  StepResult result;
  if (!instrument_id_.is_valid() || command.instrument_id != instrument_id_) {
    result.result = lob::OrderBookResult::InvalidInstrument;
  } else if (instrument_state_ == lob::InstrumentState::Halted) {
    result.result = lob::OrderBookResult::MarketHalted;
  } else if (instrument_state_ != lob::InstrumentState::Active) {
    result.result = lob::OrderBookResult::InstrumentUnavailable;
  } else if (!command.order_id.is_valid() || find(command.order_id) != nullptr) {
    result.result = lob::OrderBookResult::DuplicateOrderId;
  } else if (command.side != lob::Side::Buy &&
             command.side != lob::Side::Sell) {
    result.result = lob::OrderBookResult::InvalidSide;
  } else if (!command.price.is_valid()) {
    result.result = lob::OrderBookResult::InvalidPrice;
  } else if (!command.quantity.is_valid()) {
    result.result = lob::OrderBookResult::InvalidQuantity;
  } else if (last_command_ == std::numeric_limits<std::uint64_t>::max()) {
    result.result = lob::OrderBookResult::CapacityExhausted;
  } else {
    result.command_sequence = accept_command();
    return execute_aggressive(command, command.side, std::nullopt);
  }
  return result;
}

StepResult ReferenceModel::apply_cancel(const TraceCommand& command) {
  StepResult result;
  if (!instrument_id_.is_valid() || command.instrument_id != instrument_id_) {
    result.result = lob::OrderBookResult::InvalidInstrument;
  } else if (instrument_state_ == lob::InstrumentState::Closed) {
    result.result = lob::OrderBookResult::InstrumentUnavailable;
  } else if (instrument_state_ != lob::InstrumentState::Active &&
             instrument_state_ != lob::InstrumentState::Halted) {
    result.result = lob::OrderBookResult::InstrumentUnavailable;
  } else if (!command.order_id.is_valid() || find(command.order_id) == nullptr) {
    result.result = lob::OrderBookResult::OrderNotFound;
  } else if (last_command_ == std::numeric_limits<std::uint64_t>::max()) {
    result.result = lob::OrderBookResult::CapacityExhausted;
  } else {
    result.command_sequence = accept_command();
    if (!erase(command.order_id)) {
      std::abort();
    }
    result.result = lob::OrderBookResult::Accepted;
  }
  return result;
}

StepResult ReferenceModel::apply_amend(const TraceCommand& command) {
  StepResult result;
  const auto* resting = find(command.order_id);
  if (!instrument_id_.is_valid() || command.instrument_id != instrument_id_) {
    result.result = lob::OrderBookResult::InvalidInstrument;
  } else if (instrument_state_ == lob::InstrumentState::Halted) {
    result.result = lob::OrderBookResult::MarketHalted;
  } else if (instrument_state_ != lob::InstrumentState::Active) {
    result.result = lob::OrderBookResult::InstrumentUnavailable;
  } else if (!command.order_id.is_valid() || resting == nullptr) {
    result.result = lob::OrderBookResult::OrderNotFound;
  } else if (!command.price.is_valid()) {
    result.result = lob::OrderBookResult::InvalidPrice;
  } else if (!command.quantity.is_valid()) {
    result.result = lob::OrderBookResult::InvalidAmendment;
  } else if (last_command_ == std::numeric_limits<std::uint64_t>::max()) {
    result.result = lob::OrderBookResult::CapacityExhausted;
  } else {
    const auto old_price = resting->price;
    const auto old_leaves = resting->leaves;
    const auto old_side = resting->side;
    result.command_sequence = accept_command();
    if (command.price != old_price) {
      return execute_aggressive(command, old_side, command.order_id);
    }

    if (command.quantity > old_leaves) {
      const auto location = locate(command.order_id);
      if (!location) {
        std::abort();
      }
      bool valid = false;
      const auto& level = old_side == lob::Side::Buy
                              ? bids_.find(location->price)->second
                              : asks_.find(location->price)->second;
      const auto old_aggregate = aggregate(level, valid);
      const auto increase = command.quantity.value() - old_leaves.value();
      if (!valid || increase >
                        std::numeric_limits<std::uint64_t>::max() -
                            old_aggregate) {
        result.result = lob::OrderBookResult::CapacityExhausted;
        return result;
      }
    }

    auto* amended = find(command.order_id);
    if (amended == nullptr) {
      std::abort();
    }
    amended->leaves = command.quantity;
    if (command.quantity > old_leaves) {
      const ModelOrder moved = *amended;
      if (!erase(command.order_id) ||
          insert(moved) != lob::OrderBookResult::Accepted) {
        std::abort();
      }
    }
    result.result = lob::OrderBookResult::Accepted;
  }
  return result;
}

StepResult ReferenceModel::apply_lifecycle(const TraceCommand& command) {
  StepResult result;
  if (!instrument_id_.is_valid() || command.instrument_id != instrument_id_) {
    result.result = lob::OrderBookResult::InvalidInstrument;
    return result;
  }

  lob::InstrumentState next = lob::InstrumentState::Invalid;
  lob::StatusEventKind event_kind = lob::StatusEventKind::StateTransition;
  lob::StatusReason reason = lob::StatusReason::Invalid;
  bool reset = false;
  bool preserve_headroom = false;
  if (command.kind == TraceCommandKind::Halt &&
      instrument_state_ == lob::InstrumentState::Active) {
    next = lob::InstrumentState::Halted;
    reason = lob::StatusReason::TradingHalt;
  } else if (command.kind == TraceCommandKind::Resume &&
             instrument_state_ == lob::InstrumentState::Halted) {
    next = lob::InstrumentState::Active;
    reason = lob::StatusReason::TradingResume;
    preserve_headroom = true;
  } else if (command.kind == TraceCommandKind::Close &&
             (instrument_state_ == lob::InstrumentState::Active ||
              instrument_state_ == lob::InstrumentState::Halted)) {
    next = lob::InstrumentState::Closed;
    event_kind = lob::StatusEventKind::Reset;
    reason = lob::StatusReason::EndOfDay;
    reset = true;
  } else if (command.kind == TraceCommandKind::Open &&
             instrument_state_ == lob::InstrumentState::Closed) {
    next = lob::InstrumentState::Active;
    reason = lob::StatusReason::SessionOpen;
    preserve_headroom = true;
  } else {
    result.result = lob::OrderBookResult::InvalidStateTransition;
    return result;
  }

  if (last_command_ == std::numeric_limits<std::uint64_t>::max()) {
    result.result = lob::OrderBookResult::CapacityExhausted;
    return result;
  }
  result.command_sequence = accept_command();
  const auto required = preserve_headroom ? std::size_t{2} : std::size_t{1};
  if (status_outbox_capacity_ - status_outbox_.size() < required) {
    result.result = lob::OrderBookResult::StatusOutboxFull;
    return result;
  }
  if (last_engine_ == std::numeric_limits<std::uint64_t>::max()) {
    result.result = lob::OrderBookResult::CapacityExhausted;
    return result;
  }

  append_status(next, event_kind, reason);
  instrument_state_ = next;
  if (reset) {
    bids_.clear();
    asks_.clear();
  }
  result.result = lob::OrderBookResult::Accepted;
  return result;
}

StepResult ReferenceModel::execute_aggressive(
    const TraceCommand& command, lob::Side side,
    std::optional<lob::OrderId> replaced_order) {
  StepResult result;
  result.command_sequence =
      required_domain<lob::CommandSequence>(last_command_);
  ReferenceModel candidate = *this;
  if (replaced_order && !candidate.erase(*replaced_order)) {
    std::abort();
  }

  std::uint64_t remainder = command.quantity.value();
  std::vector<PlannedReport> planned;
  planned.reserve(lob::kMaximumFillsPerCommand);
  const auto take_level = [&](auto& levels) {
    while (remainder != 0 && !levels.empty() &&
           crosses(side, command.price.value(), levels.begin()->first)) {
      if (planned.size() == lob::kMaximumFillsPerCommand) {
        return false;
      }
      auto level = levels.begin();
      auto& resting = level->second.front();
      const auto fill = std::min(remainder, resting.leaves.value());
      planned.push_back(
          {resting.order_id, resting.price,
           required_domain<lob::Quantity>(fill)});
      remainder -= fill;
      if (fill == resting.leaves.value()) {
        level->second.erase(level->second.begin());
        if (level->second.empty()) {
          levels.erase(level);
        }
      } else {
        resting.leaves = required_domain<lob::Quantity>(
            resting.leaves.value() - fill);
      }
    }
    return true;
  };

  const bool within_fill_capacity =
      side == lob::Side::Buy ? take_level(candidate.asks_)
                             : take_level(candidate.bids_);
  if (!within_fill_capacity) {
    result.result = lob::OrderBookResult::CapacityExhausted;
    return result;
  }

  if (remainder != 0) {
    const auto inserted = candidate.insert(
        {command.order_id, side, command.price,
         required_domain<lob::Quantity>(remainder)});
    if (inserted != lob::OrderBookResult::Accepted) {
      result.result = inserted;
      return result;
    }
  }

  const auto fill_count = static_cast<std::uint64_t>(planned.size());
  if (fill_count > std::numeric_limits<std::uint64_t>::max() - last_engine_ ||
      fill_count > std::numeric_limits<std::uint64_t>::max() - last_match_) {
    result.result = lob::OrderBookResult::CapacityExhausted;
    return result;
  }
  if (planned.size() >
      execution_outbox_capacity_ - execution_outbox_.size()) {
    result.result = lob::OrderBookResult::LosslessOutboxFull;
    fail_closed();
    return result;
  }

  result.synchronous_reports.reserve(planned.size());
  for (std::size_t index = 0; index < planned.size(); ++index) {
    const auto offset = static_cast<std::uint64_t>(index) + 1;
    const auto& fill = planned[index];
    result.synchronous_reports.push_back(
        {required_domain<lob::MatchId>(last_match_ + offset), instrument_id_,
         command.order_id, fill.resting_order_id, fill.resting_price,
         fill.match_quantity,
         required_domain<lob::EngineSequence>(last_engine_ + offset)});
  }
  candidate.last_engine_ += fill_count;
  candidate.last_match_ += fill_count;
  candidate.execution_outbox_.insert(candidate.execution_outbox_.end(),
                                     result.synchronous_reports.begin(),
                                     result.synchronous_reports.end());
  *this = std::move(candidate);
  result.result = lob::OrderBookResult::Accepted;
  return result;
}

std::optional<ReferenceModel::LocatedOrder> ReferenceModel::locate(
    lob::OrderId order_id) const noexcept {
  for (const auto& [price, level] : bids_) {
    for (std::size_t index = 0; index < level.size(); ++index) {
      if (level[index].order_id == order_id) {
        return LocatedOrder{lob::Side::Buy, price, index};
      }
    }
  }
  for (const auto& [price, level] : asks_) {
    for (std::size_t index = 0; index < level.size(); ++index) {
      if (level[index].order_id == order_id) {
        return LocatedOrder{lob::Side::Sell, price, index};
      }
    }
  }
  return std::nullopt;
}

const ReferenceModel::ModelOrder* ReferenceModel::find(
    lob::OrderId order_id) const noexcept {
  const auto location = locate(order_id);
  if (!location) {
    return nullptr;
  }
  if (location->side == lob::Side::Buy) {
    return &bids_.find(location->price)->second[location->index];
  }
  return &asks_.find(location->price)->second[location->index];
}

ReferenceModel::ModelOrder* ReferenceModel::find(
    lob::OrderId order_id) noexcept {
  const auto location = locate(order_id);
  if (!location) {
    return nullptr;
  }
  if (location->side == lob::Side::Buy) {
    return &bids_.find(location->price)->second[location->index];
  }
  return &asks_.find(location->price)->second[location->index];
}

lob::OrderBookResult ReferenceModel::insert(ModelOrder order) {
  if (find(order.order_id) != nullptr) {
    return lob::OrderBookResult::DuplicateOrderId;
  }
  if (active_order_count() >= active_order_capacity_) {
    return lob::OrderBookResult::CapacityExhausted;
  }

  const auto insert_into = [&](auto& levels) {
    auto level = levels.find(order.price.value());
    if (level == levels.end()) {
      if (levels.size() >= price_level_capacity_) {
        return lob::OrderBookResult::CapacityExhausted;
      }
      level = levels.try_emplace(order.price.value()).first;
    } else {
      bool valid = false;
      const auto current = aggregate(level->second, valid);
      if (!valid || order.leaves.value() >
                        std::numeric_limits<std::uint64_t>::max() - current) {
        return lob::OrderBookResult::CapacityExhausted;
      }
    }
    level->second.push_back(order);
    return lob::OrderBookResult::Accepted;
  };
  return order.side == lob::Side::Buy ? insert_into(bids_)
                                      : insert_into(asks_);
}

bool ReferenceModel::erase(lob::OrderId order_id) noexcept {
  const auto location = locate(order_id);
  if (!location) {
    return false;
  }
  const auto erase_from = [&](auto& levels) {
    auto level = levels.find(location->price);
    level->second.erase(level->second.begin() +
                        static_cast<std::ptrdiff_t>(location->index));
    if (level->second.empty()) {
      levels.erase(level);
    }
  };
  if (location->side == lob::Side::Buy) {
    erase_from(bids_);
  } else {
    erase_from(asks_);
  }
  return true;
}

std::size_t ReferenceModel::active_order_count() const noexcept {
  std::size_t count = 0;
  for (const auto& [price, level] : bids_) {
    static_cast<void>(price);
    count += level.size();
  }
  for (const auto& [price, level] : asks_) {
    static_cast<void>(price);
    count += level.size();
  }
  return count;
}

lob::CommandSequence ReferenceModel::accept_command() noexcept {
  ++last_command_;
  return required_domain<lob::CommandSequence>(last_command_);
}

void ReferenceModel::fail_closed() {
  if (instrument_state_ != lob::InstrumentState::Active ||
      status_outbox_.size() >= status_outbox_capacity_ ||
      last_engine_ == std::numeric_limits<std::uint64_t>::max()) {
    std::abort();
  }
  append_status(lob::InstrumentState::Halted,
                lob::StatusEventKind::StateTransition,
                lob::StatusReason::LosslessOutboxFull);
  instrument_state_ = lob::InstrumentState::Halted;
}

void ReferenceModel::append_status(lob::InstrumentState resulting_state,
                                   lob::StatusEventKind event_kind,
                                   lob::StatusReason reason) {
  const auto previous = instrument_state_;
  ++last_engine_;
  status_outbox_.push_back(
      {lob::StatusScope::Instrument, instrument_id_, previous, resulting_state,
       event_kind, reason,
       required_domain<lob::EngineSequence>(last_engine_)});
}

ObservableState ReferenceModel::observable_state() const {
  ObservableState state;
  state.instrument_state = instrument_state_;
  state.last_command = domain_or_zero<lob::CommandSequence>(last_command_);
  state.last_engine = domain_or_zero<lob::EngineSequence>(last_engine_);
  state.last_match = domain_or_zero<lob::MatchId>(last_match_);
  state.active_order_count = active_order_count();
  state.bid_level_count = bids_.size();
  state.ask_level_count = asks_.size();
  if (!bids_.empty()) {
    state.best_bid = required_domain<lob::PriceTicks>(bids_.begin()->first);
  }
  if (!asks_.empty()) {
    state.best_ask = required_domain<lob::PriceTicks>(asks_.begin()->first);
  }
  const auto append_side = [&](const auto& levels, lob::Side side,
                               auto& depth, auto& orders) {
    for (const auto& [raw_price, level] : levels) {
      bool valid = false;
      const auto total = aggregate(level, valid);
      if (!valid) {
        std::abort();
      }
      const auto level_price = required_domain<lob::PriceTicks>(raw_price);
      depth.push_back({level_price, required_domain<lob::Quantity>(total),
                       level.size()});
      for (const auto& order : level) {
        orders.push_back({order.order_id, instrument_id_, side, order.price,
                          order.leaves});
      }
    }
  };
  append_side(bids_, lob::Side::Buy, state.bids, state.bid_orders);
  append_side(asks_, lob::Side::Sell, state.asks, state.ask_orders);
  return state;
}

std::vector<lob::ExecutionReport> ReferenceModel::drain_execution_reports() {
  auto reports = std::move(execution_outbox_);
  execution_outbox_.clear();
  return reports;
}

std::vector<lob::SystemStatus> ReferenceModel::drain_statuses() {
  auto statuses = std::move(status_outbox_);
  status_outbox_.clear();
  return statuses;
}

const std::vector<lob::ExecutionReport>&
ReferenceModel::pending_execution_reports() const noexcept {
  return execution_outbox_;
}

const std::vector<lob::SystemStatus>& ReferenceModel::pending_statuses()
    const noexcept {
  return status_outbox_;
}

std::vector<lob::RestingOrderView> ReferenceModel::active_orders() const {
  const auto state = observable_state();
  auto result = state.bid_orders;
  result.insert(result.end(), state.ask_orders.begin(), state.ask_orders.end());
  return result;
}

bool ReferenceModel::validate_invariants() const noexcept {
  if (active_order_count() > active_order_capacity_ ||
      bids_.size() > price_level_capacity_ ||
      asks_.size() > price_level_capacity_ ||
      execution_outbox_.size() > execution_outbox_capacity_ ||
      status_outbox_.size() > status_outbox_capacity_ ||
      (instrument_state_ == lob::InstrumentState::Active &&
       status_outbox_.size() == status_outbox_capacity_) ||
      (instrument_state_ == lob::InstrumentState::Closed &&
       active_order_count() != 0)) {
    return false;
  }
  if (!bids_.empty() && !asks_.empty() &&
      instrument_state_ == lob::InstrumentState::Active &&
      bids_.begin()->first >= asks_.begin()->first) {
    return false;
  }

  std::set<std::uint64_t> ids;
  const auto validate_side = [&](const auto& levels, lob::Side side) {
    for (const auto& [price, level] : levels) {
      if (price <= 0 || level.empty()) {
        return false;
      }
      bool valid = false;
      static_cast<void>(aggregate(level, valid));
      if (!valid) {
        return false;
      }
      for (const auto& order : level) {
        if (!order.order_id.is_valid() || order.side != side ||
            order.price.value() != price || !order.leaves.is_valid() ||
            !ids.insert(order.order_id.value()).second) {
          return false;
        }
      }
    }
    return true;
  };
  return validate_side(bids_, lob::Side::Buy) &&
         validate_side(asks_, lob::Side::Sell);
}

bool execution_report_equal(const lob::ExecutionReport& left,
                            const lob::ExecutionReport& right) noexcept {
  return left.match_id == right.match_id &&
         left.instrument_id == right.instrument_id &&
         left.aggressive_order_id == right.aggressive_order_id &&
         left.resting_order_id == right.resting_order_id &&
         left.match_price == right.match_price &&
         left.match_quantity == right.match_quantity &&
         left.engine_sequence == right.engine_sequence;
}

bool system_status_equal(const lob::SystemStatus& left,
                         const lob::SystemStatus& right) noexcept {
  return left.scope == right.scope &&
         left.instrument_id == right.instrument_id &&
         left.previous_state == right.previous_state &&
         left.resulting_state == right.resulting_state &&
         left.kind == right.kind && left.reason == right.reason &&
         left.engine_sequence == right.engine_sequence;
}

std::string describe_state(const ObservableState& state) {
  std::ostringstream output;
  output << "state=" << static_cast<unsigned>(state.instrument_state)
         << " command=" << state.last_command.value()
         << " engine=" << state.last_engine.value()
         << " match=" << state.last_match.value()
         << " orders=" << state.active_order_count
         << " bid_levels=" << state.bid_level_count
         << " ask_levels=" << state.ask_level_count
         << " best_bid=" << (state.best_bid ? state.best_bid->value() : 0)
         << " best_ask=" << (state.best_ask ? state.best_ask->value() : 0)
         << " bids=";
  for (const auto& level : state.bids) {
    output << '[' << level.price.value() << ':'
           << level.aggregate_leaves_quantity.value() << ':'
           << level.order_count << ']';
  }
  output << " asks=";
  for (const auto& level : state.asks) {
    output << '[' << level.price.value() << ':'
           << level.aggregate_leaves_quantity.value() << ':'
           << level.order_count << ']';
  }
  output << " bid_fifo=";
  for (const auto& order : state.bid_orders) {
    output << '[' << order.order_id.value() << ':' << order.price.value() << ':'
           << order.leaves_quantity.value() << ']';
  }
  output << " ask_fifo=";
  for (const auto& order : state.ask_orders) {
    output << '[' << order.order_id.value() << ':' << order.price.value() << ':'
           << order.leaves_quantity.value() << ']';
  }
  return output.str();
}

}  // namespace phase1_test
