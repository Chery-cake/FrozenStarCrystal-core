module;

export module memory.allocator;

import std.compat;

export namespace memory::allocator {

/**
 * @brief Base class for memory allocation
 * The memory is stored in a unique_ptr<uint_8>
 */
template <size_t size> class Memory {
private:
  std::unique_ptr<uint8_t, void (*)(uint8_t *)> memory_;

protected:
  mutable std::mutex memoryMutex_;

  /**
   * @brief Return the allocated memory
   * @retrun uint8_t*
   */
  [[nodiscard]] const uint8_t *memory() const { return memory_.get(); }
  [[nodiscard]] uint8_t *memory() { return memory_.get(); }

public:
  explicit Memory();
  virtual ~Memory();

  // Disable copy and move
  Memory(const Memory &) = delete;
  Memory &operator=(const Memory &) = delete;
  Memory(Memory &&) = delete;
  Memory &operator=(Memory &&) = delete;

  /**
   * @brief Return the allocated byte size
   * @return size_t
   */
  [[nodiscard]] size_t capacity() const { return size; }
};

/**
 * @brief Bump Allocator for fast allocations
 */
template <size_t size> class Bump : public Memory<size> {
private:
  size_t offset_;

public:
  explicit Bump();
  ~Bump() override;

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
};

/**
 * @brief Stack Allocator for variable allocations
 */
template <size_t size> class Stack : public Memory<size> {
private:
  size_t offset_;

public:
  explicit Stack();
  ~Stack() override;

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
};

/**
 * @brief Pool Allocator for multiple identical allocations
 */
template <typename T, size_t poolSize>
class Pool : public Memory<sizeof(T) * poolSize> {
private:
  union Node {
    alignas(std::max(alignof(T),
                     alignof(Node *))) std::array<uint8_t, sizeof(T)> data;
    Node *next;
  };

  Node *freeList;

public:
  explicit Pool();
  ~Pool() override;

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
};

} // namespace memory::allocator
