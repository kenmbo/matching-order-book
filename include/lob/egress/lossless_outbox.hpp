#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace lob {

template <typename Record>
class LosslessOutbox final {
  static_assert(std::is_trivially_copyable_v<Record>);

 public:
  class Reservation final {
   public:
    Reservation() noexcept = default;

    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;

    Reservation(Reservation&& other) noexcept
        : outbox_(std::exchange(other.outbox_, nullptr)),
          start_(other.start_),
          count_(other.count_),
          written_(other.written_) {}

    Reservation& operator=(Reservation&&) = delete;

    ~Reservation() { static_cast<void>(abort()); }

    [[nodiscard]] bool valid() const noexcept { return outbox_ != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t written() const noexcept { return written_; }

    [[nodiscard]] bool write(const Record& record) noexcept {
      if (!valid() || written_ >= count_) {
        return false;
      }

      outbox_->slots_[(start_ + written_) & outbox_->mask_] = record;
      ++written_;
      return true;
    }

    [[nodiscard]] bool commit() noexcept {
      if (!valid() || written_ != count_ ||
          !outbox_->commit_reservation(start_, count_)) {
        return false;
      }

      outbox_ = nullptr;
      return true;
    }

    [[nodiscard]] bool abort() noexcept {
      if (!valid()) {
        return false;
      }

      const bool aborted = outbox_->abort_reservation(start_, count_);
      outbox_ = nullptr;
      return aborted;
    }

   private:
    friend class LosslessOutbox;

    Reservation(LosslessOutbox* outbox, std::size_t start,
                std::size_t count) noexcept
        : outbox_(outbox), start_(start), count_(count) {}

    LosslessOutbox* outbox_{};
    std::size_t start_{};
    std::size_t count_{};
    std::size_t written_{};
  };

  explicit LosslessOutbox(std::size_t capacity)
      : slots_(allocate_slots(capacity)),
        capacity_(capacity),
        mask_(capacity - std::size_t{1}) {}

  LosslessOutbox(const LosslessOutbox&) = delete;
  LosslessOutbox& operator=(const LosslessOutbox&) = delete;
  LosslessOutbox(LosslessOutbox&&) = delete;
  LosslessOutbox& operator=(LosslessOutbox&&) = delete;
  ~LosslessOutbox() = default;

  [[nodiscard]] Reservation reserve(std::size_t count) noexcept {
    if (reservation_active_ || count > available_capacity()) {
      return {};
    }

    reservation_active_ = true;
    reservation_start_ = write_cursor_;
    reserved_count_ = count;
    return Reservation{this, reservation_start_, reserved_count_};
  }

  [[nodiscard]] bool try_pop(Record& record) noexcept {
    if (published_count_ == 0) {
      return false;
    }

    record = slots_[read_cursor_];
    read_cursor_ = (read_cursor_ + std::size_t{1}) & mask_;
    --published_count_;
    return true;
  }

  [[nodiscard]] bool try_peek(Record& record) const noexcept {
    if (published_count_ == 0) {
      return false;
    }

    record = slots_[read_cursor_];
    return true;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::size_t size() const noexcept { return published_count_; }
  [[nodiscard]] bool empty() const noexcept { return published_count_ == 0; }
  [[nodiscard]] std::size_t available_capacity() const noexcept {
    return capacity_ - published_count_ - reserved_count_;
  }

  [[nodiscard]] bool validate_invariants() const noexcept {
    if (!is_power_of_two(capacity_) || read_cursor_ >= capacity_ ||
        write_cursor_ >= capacity_ || published_count_ > capacity_ ||
        reserved_count_ > capacity_ - published_count_ ||
        write_cursor_ != ((read_cursor_ + published_count_) & mask_)) {
      return false;
    }
    if (reservation_active_) {
      return reservation_start_ == write_cursor_;
    }
    return reserved_count_ == 0;
  }

 private:
  [[nodiscard]] static constexpr bool is_power_of_two(
      std::size_t value) noexcept {
    return value != 0 && (value & (value - std::size_t{1})) == 0;
  }

  [[nodiscard]] static std::unique_ptr<Record[]> allocate_slots(
      std::size_t capacity) {
    if (!is_power_of_two(capacity)) {
      std::abort();
    }
    return std::make_unique<Record[]>(capacity);
  }

  [[nodiscard]] bool commit_reservation(std::size_t start,
                                        std::size_t count) noexcept {
    if (!reservation_active_ || start != reservation_start_ ||
        count != reserved_count_) {
      return false;
    }

    published_count_ += reserved_count_;
    write_cursor_ = (write_cursor_ + reserved_count_) & mask_;
    clear_reservation();
    return true;
  }

  [[nodiscard]] bool abort_reservation(std::size_t start,
                                       std::size_t count) noexcept {
    if (!reservation_active_ || start != reservation_start_ ||
        count != reserved_count_) {
      return false;
    }

    clear_reservation();
    return true;
  }

  void clear_reservation() noexcept {
    reservation_active_ = false;
    reservation_start_ = 0;
    reserved_count_ = 0;
  }

  std::unique_ptr<Record[]> slots_;
  std::size_t capacity_{};
  std::size_t mask_{};
  std::size_t read_cursor_{};
  std::size_t write_cursor_{};
  std::size_t published_count_{};
  std::size_t reservation_start_{};
  std::size_t reserved_count_{};
  bool reservation_active_{};
};

}  // namespace lob
