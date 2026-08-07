#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace lob {

namespace detail {

struct DomainAccess;

}  // namespace detail

template <typename Tag, std::integral Representation>
class IntegerDomain final {
 public:
  using rep_type = Representation;

  constexpr IntegerDomain() noexcept = default;

  [[nodiscard]] constexpr rep_type value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return value_ >= minimum_value;
  }

  static constexpr rep_type reserved_value = rep_type{0};
  static constexpr rep_type minimum_value = rep_type{1};
  static constexpr rep_type maximum_value =
      std::numeric_limits<rep_type>::max();

  constexpr auto operator<=>(const IntegerDomain&) const noexcept = default;

 private:
  friend struct detail::DomainAccess;

  constexpr explicit IntegerDomain(rep_type value) noexcept : value_(value) {}

  rep_type value_{reserved_value};
};

namespace detail {

struct DomainAccess final {
  template <typename Domain>
  [[nodiscard]] static constexpr Domain from_rep(
      typename Domain::rep_type value) noexcept {
    return Domain{value};
  }

  template <typename Domain, std::integral Source>
    requires(!std::same_as<std::remove_cv_t<Source>,
                           typename Domain::rep_type>)
  [[nodiscard]] static constexpr Domain from_rep(Source) noexcept = delete;
};

}  // namespace detail

struct OrderIdTag;
struct InstrumentIdTag;
struct MatchIdTag;
struct CommandSequenceTag;
struct EngineSequenceTag;
struct PriceTicksTag;
struct QuantityTag;

using OrderId = IntegerDomain<OrderIdTag, std::uint64_t>;
using InstrumentId = IntegerDomain<InstrumentIdTag, std::uint32_t>;
using MatchId = IntegerDomain<MatchIdTag, std::uint64_t>;
using CommandSequence = IntegerDomain<CommandSequenceTag, std::uint64_t>;
using EngineSequence = IntegerDomain<EngineSequenceTag, std::uint64_t>;
using PriceTicks = IntegerDomain<PriceTicksTag, std::int64_t>;
using Quantity = IntegerDomain<QuantityTag, std::uint64_t>;

enum class DomainConversionResult : std::uint8_t {
  Success = 0,
  ReservedValue = 1,
  OutOfRange = 2,
};

template <typename Domain>
struct CheckedDomainValue final {
  Domain value{};
  DomainConversionResult result{DomainConversionResult::OutOfRange};

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return result == DomainConversionResult::Success;
  }
};

template <typename Domain, std::integral Source>
  requires(!std::same_as<std::remove_cv_t<Source>, bool>)
[[nodiscard]] constexpr CheckedDomainValue<Domain> checked_domain_cast(
    Source source) noexcept {
  using Representation = typename Domain::rep_type;

  if (!std::in_range<Representation>(source)) {
    return {};
  }

  const auto value = static_cast<Representation>(source);
  if (value == Domain::reserved_value) {
    return {{}, DomainConversionResult::ReservedValue};
  }
  if (value < Domain::minimum_value) {
    return {{}, DomainConversionResult::OutOfRange};
  }

  return {detail::DomainAccess::from_rep<Domain>(value),
          DomainConversionResult::Success};
}

enum class QuantityArithmeticResult : std::uint8_t {
  Success = 0,
  Zero = 1,
  Overflow = 2,
  Underflow = 3,
  InvalidOperand = 4,
};

struct CheckedQuantity final {
  Quantity value{};
  QuantityArithmeticResult result{QuantityArithmeticResult::InvalidOperand};

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return result == QuantityArithmeticResult::Success;
  }
};

[[nodiscard]] constexpr CheckedQuantity checked_add(Quantity left,
                                                    Quantity right) noexcept {
  if (!left.is_valid() || !right.is_valid()) {
    return {};
  }
  if (right.value() > Quantity::maximum_value - left.value()) {
    return {{}, QuantityArithmeticResult::Overflow};
  }

  return {detail::DomainAccess::from_rep<Quantity>(left.value() + right.value()),
          QuantityArithmeticResult::Success};
}

[[nodiscard]] constexpr CheckedQuantity checked_subtract(
    Quantity left, Quantity right) noexcept {
  if (!left.is_valid() || !right.is_valid()) {
    return {};
  }
  if (right.value() > left.value()) {
    return {{}, QuantityArithmeticResult::Underflow};
  }
  if (right == left) {
    return {{}, QuantityArithmeticResult::Zero};
  }

  return {detail::DomainAccess::from_rep<Quantity>(left.value() - right.value()),
          QuantityArithmeticResult::Success};
}

static_assert(sizeof(OrderId) == sizeof(std::uint64_t));
static_assert(sizeof(InstrumentId) == sizeof(std::uint32_t));
static_assert(sizeof(MatchId) == sizeof(std::uint64_t));
static_assert(sizeof(CommandSequence) == sizeof(std::uint64_t));
static_assert(sizeof(EngineSequence) == sizeof(std::uint64_t));
static_assert(sizeof(PriceTicks) == sizeof(std::int64_t));
static_assert(sizeof(Quantity) == sizeof(std::uint64_t));
static_assert(std::is_signed_v<PriceTicks::rep_type>);
static_assert(std::is_integral_v<Quantity::rep_type>);
static_assert(!std::is_floating_point_v<PriceTicks::rep_type>);
static_assert(!std::is_floating_point_v<Quantity::rep_type>);
static_assert(std::is_trivially_copyable_v<OrderId>);
static_assert(std::is_trivially_copyable_v<InstrumentId>);
static_assert(std::is_trivially_copyable_v<MatchId>);
static_assert(std::is_trivially_copyable_v<CommandSequence>);
static_assert(std::is_trivially_copyable_v<EngineSequence>);
static_assert(std::is_trivially_copyable_v<PriceTicks>);
static_assert(std::is_trivially_copyable_v<Quantity>);
static_assert(std::is_standard_layout_v<OrderId>);
static_assert(std::is_standard_layout_v<InstrumentId>);
static_assert(std::is_standard_layout_v<MatchId>);
static_assert(std::is_standard_layout_v<CommandSequence>);
static_assert(std::is_standard_layout_v<EngineSequence>);
static_assert(std::is_standard_layout_v<PriceTicks>);
static_assert(std::is_standard_layout_v<Quantity>);

}  // namespace lob
