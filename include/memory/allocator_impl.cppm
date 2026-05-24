module;

#include <cstdio>

export module memory.allocator:impl;

import std.compat;
import memory.allocator;

export namespace memory::allocator {

// ============================================================================
// MemoryAllocator Implementation
// ============================================================================

template <size_t size>
Memory<size>::Memory()
    : memory_(static_cast<uint8_t *>(::operator new(size)),
              [](uint8_t *ptr) { ::operator delete(ptr); }) {}

template <size_t size> Memory<size>::~Memory() {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  memory_.reset();
}

// ============================================================================
// BumpAllocator Implementation
// ============================================================================

template <size_t size> Bump<size>::Bump() : Memory<size>(), offset_(0) {}

template <size_t size> Bump<size>::~Bump() = default;

template <size_t size>
template <typename T>
T *Bump<size>::allocate(size_t alignment) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  alignment = std::max(alignment, alignof(T));

  size_t allocation = sizeof(T);
  // Align current offset
  auto current = reinterpret_cast<uintptr_t>(this->memory() + offset_);
  size_t aligned = (current + alignment - 1) & ~(alignment - 1);
  size_t padding = aligned - current;

  if (offset_ + padding + allocation > this->capacity()) {
    std::println(stderr, "[Bump] Out of memory! Requested: {}, Available: {}",
                 allocation, this->capacity() - offset_);
    throw std::bad_alloc();
  }

  offset_ += padding;
  T *result = reinterpret_cast<T *>(this->memory() + offset_);
  offset_ += allocation;

  return result;
}

template <size_t size> void Bump<size>::reset() {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  offset_ = 0;
}

// ============================================================================
// StackAllocator Implementation
// ============================================================================

template <size_t size> Stack<size>::Stack() : Memory<size>(), offset_(0) {}

template <size_t size> Stack<size>::~Stack() = default;

template <size_t size>
template <typename T>
T *Stack<size>::push(size_t alignment) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  alignment = std::max(alignment, alignof(T));

  size_t header_size = sizeof(size_t);
  auto start = reinterpret_cast<uintptr_t>(this->memory() + offset_);

  // Align current offset
  size_t aligned_addr =
      (start + header_size + alignment - 1) & ~(alignment - 1);
  size_t header_addr = aligned_addr - header_size;

  size_t totalSize = (aligned_addr + sizeof(T)) - start;

  if (offset_ + totalSize > this->capacity()) {
    std::println(stderr, "[Stack] Out of memory! Requested: {}, Available: {}",
                 totalSize, this->capacity() - offset_);
    throw std::bad_alloc();
  }

  *reinterpret_cast<size_t *>(header_addr) = totalSize; // TODO check warning
  offset_ += totalSize;

  return reinterpret_cast<T *>(aligned_addr);
}

template <size_t size> void Stack<size>::pop(void *&ptr) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  auto *header = reinterpret_cast<size_t *>(reinterpret_cast<uint8_t *>(ptr) -
                                            sizeof(size_t));
  offset_ -= *header;
  ptr = nullptr;
}

// ============================================================================
// PoolAllocator Implementation
// ============================================================================

template <typename T, size_t poolSize>
Pool<T, poolSize>::Pool() : Memory<sizeof(T) * poolSize>() {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);

  // initialize freeList
  uint8_t *raw_mem = this->memory();
  auto base_mem = reinterpret_cast<uintptr_t>(raw_mem);

  constexpr size_t NodeAlignment = alignof(Node);
  uintptr_t aligned_base =
      (base_mem + NodeAlignment - 1) & ~(NodeAlignment - 1);
  Node mem = reinterpret_cast<Node *>(aligned_base);

  std::ranges::for_each(
      std::views::iota(size_t{0}, poolSize - 1), [&mem](size_t i) {
        reinterpret_cast<Node *>(mem + (i * sizeof(T)))->next =
            reinterpret_cast<Node *>(mem + ((i + 1) * sizeof(T)));
      });
  reinterpret_cast<Node *>(mem + ((poolSize - 1) * sizeof(T)))->next = nullptr;
  freeList = reinterpret_cast<Node *>(mem);
}

template <typename T, size_t poolSize> Pool<T, poolSize>::~Pool() = default;

template <typename T, size_t poolSize> T *Pool<T, poolSize>::allocate() {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  if (freeList == nullptr) {
    std::println(stderr, "[Pool] Out of memory!");
    throw std::bad_alloc();
  }
  Node *node = freeList;
  freeList = freeList->next;
  return reinterpret_cast<T *>(node);
}

template <typename T, size_t poolSize>
void Pool<T, poolSize>::deallocate(T *&ptr) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);

  Node *node = reinterpret_cast<Node *>(ptr);
  node->next = freeList;
  freeList = node;

  ptr = nullptr;
}

template <typename T, size_t poolSize>
size_t Pool<T, poolSize>::available() const {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  size_t count = 0;
  Node *node = freeList;
  while (node) {
    count++;
    node = node->next;
  }
  return count;
}

} // namespace memory::allocator
