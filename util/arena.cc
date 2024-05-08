// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/arena.h"

namespace leveldb {

// Memory page size.
static const int kBlockSize = 4096;

Arena::Arena()
    : alloc_ptr_(nullptr), alloc_bytes_remaining_(0), memory_usage_(0) {}

Arena::~Arena() {
  // 只有在析构的时候，才会释放内存，通常内存池应该及时回收，重复利用
  for (size_t i = 0; i < blocks_.size(); i++) {
    delete[] blocks_[i];
  }
}

char* Arena::AllocateFallback(size_t bytes) {
  // 大于1024，直接调用new从内存中分配，此时alloc_ptr_和alloc_bytes_remaining_并没有变更
  if (bytes > kBlockSize / 4) {
    // Object is more than a quarter of our block size.  Allocate it separately
    // to avoid wasting too much space in leftover bytes.
    char* result = AllocateNewBlock(bytes);
    return result;
  }

  // 小于1024后，直接分配4096字节，上一个Block残留的内存将不会被使用。
  // We waste the remaining space in the current block.
  alloc_ptr_ = AllocateNewBlock(kBlockSize);
  alloc_bytes_remaining_ = kBlockSize;

  char* result = alloc_ptr_;
  alloc_ptr_ += bytes;
  alloc_bytes_remaining_ -= bytes;
  return result;
}

char* Arena::AllocateAligned(size_t bytes) {
  // 获取计算器当前需要对齐的字节数，最小时8字节对齐；否则按照void*的大小来对齐
  const int align = (sizeof(void*) > 8) ? sizeof(void*) : 8;
  // 必须是2的次幂，(align & (align - 1)) == 0 用来判断是否是2的次幂
  static_assert((align & (align - 1)) == 0,
                "Pointer size should be a power of 2");
  // A & (B - 1) = A % B
  // reinterpret_cast<uintptr_t>类型对应机器指针大小
  size_t current_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align - 1);

  // Current_mod为0，表示alloc_ptr_已经字节对齐；否则计算出gap字节大小，并记为slop
  size_t slop = (current_mod == 0 ? 0 : align - current_mod);
  // 总大小为：要分配的内存大小 + 偏差大小
  size_t needed = bytes + slop;
  char* result;
  // 使用内存池内存时，考虑对齐
  if (needed <= alloc_bytes_remaining_) {
    result = alloc_ptr_ + slop;
    alloc_ptr_ += needed;
    alloc_bytes_remaining_ -= needed;
  } else {
    // AllocateFallback always returned aligned memory
    // 重新分配的话，就不考虑alloc_ptr_是否对齐，底层operator new分配内存时会自动对齐
    result = AllocateFallback(bytes);
  }
  assert((reinterpret_cast<uintptr_t>(result) & (align - 1)) == 0);
  return result;
}

char* Arena::AllocateNewBlock(size_t block_bytes) {
  char* result = new char[block_bytes];
  blocks_.push_back(result);
  memory_usage_.fetch_add(block_bytes + sizeof(char*),
                          std::memory_order_relaxed);
  return result;
}

}  // namespace leveldb
