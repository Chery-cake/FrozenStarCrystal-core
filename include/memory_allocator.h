#ifndef BUMP_ALLOCATOR_H_
#define BUMP_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace core {

/**
 * @brief Base class for memory allocation
 * The memory is istored in a unique_ptr<uint_8>
 */
template <size_t size> class MemoryAllocator {
public:
  explicit MemoryAllocator();
  virtual ~MemoryAllocator();

  // Disable copy and move
  MemoryAllocator(const MemoryAllocator &) = delete;
  MemoryAllocator &operator=(const MemoryAllocator &) = delete;
  MemoryAllocator(MemoryAllocator &&) = delete;
  MemoryAllocator &operator=(MemoryAllocator &&) = delete;

  /**
   * @brief Return the allocated byte size
   * @return size_t
   */
  [[nodiscard]] size_t capacity() const { return size; }

protected:
  mutable std::mutex memoryMutex_;

  /**
   * @brief Return the allocated memory
   * @retrun uint8_t*
   */
  [[nodiscard]] const uint8_t *memory() const { return memory_.get(); }
  [[nodiscard]] uint8_t *memory() { return memory_.get(); }

private:
  std::unique_ptr<uint8_t, void (*)(uint8_t *)> memory_;
};

/**
 * @brief Bump Allocator for fast allocations
 */
template <size_t size> class BumpAllocator : public MemoryAllocator<size> {
public:
  explicit BumpAllocator();
  ~BumpAllocator() override;

  /**
   * @brief Allocate a object in the memory
   * if their is no space it will throw a bad_alloc
   * @template T type of the object being allocated
   * @param alignment override the set alignment
   * @return T* pointer to the object allocated
   */
  template <typename T> T *allocate(size_t alignment = alignof(T));

  /**
   * @brief Clear the byte offset of the memory
   */
  void reset();

  /**
   * @brief Get the byte offset of the allocations
   * @return size_t
   */
  [[nodiscard]] const size_t &bytes_allocated() const { return offset_; }

private:
  size_t offset_;
};

/**
 * @brief Stack Allocator for variable allocations
 */
template <size_t size> class StackAllocator : public MemoryAllocator<size> {
public:
  explicit StackAllocator();
  ~StackAllocator() override;

  /**
   * @brief Push a object to the memory stack, with a header
   * if their is no space it will throw a bad_alloc
   * @template T type of the object being allocated
   * @param alignment override the set alingment
   * @return T* pointer to the object allocated
   */
  template <typename T> T *push(size_t alignment = alignof(T));

  /**
   * @brief Removes the last entry on the stack with itś header
   * @param ptr pointer to the object being removed
   * the pointer is turned into a nullptr to prevent using of the deleted data
   */
  void pop(void *&ptr);

  /**
   * @brief Get the byte offset of the allocations
   * @return size_t
   */
  [[nodiscard]] const size_t &bytes_allocated() const { return offset_; }

private:
  size_t offset_;
};

/**
 * @brief Pool Allocator for multiple identical allocations
 */
template <typename T, size_t poolSize>
class PoolAllocator : public MemoryAllocator<sizeof(T) * poolSize> {
public:
  explicit PoolAllocator();
  ~PoolAllocator() override;

  /**
   * @brief Allocate the space on the first free position of the list
   * if their is no free position throw a bad_alloc
   * @return T* pointer to the object allocated
   */
  T *allocate();

  /**
   * @brief Deallocate the pointer and add the space back on to the list
   * @param ptr pointer to the object being deallocated
   * the pointer is turned into a nullptr to prevent using of the deleted data
   */
  void deallocate(T *&ptr);

  /**
   * @brief Get the amount of available positions of the list
   * @retunr size_t
   */
  [[nodiscard]] size_t available() const;

private:
  union Node {
    alignas(std::max(alignof(T),
                     alignof(Node *))) std::array<uint8_t, sizeof(T)> data;
    Node *next;
  };

  Node *freeList;
};

} // namespace core

// template implementation
#include "../src/memory_allocator_impl.hpp"

#endif // BUMP_ALLOCATOR_H_
