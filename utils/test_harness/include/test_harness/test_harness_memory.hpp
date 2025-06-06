/*
 *
 * Copyright (C) 2019 - 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef level_zero_tests_ZE_TEST_HARNESS_MEMORY_HPP
#define level_zero_tests_ZE_TEST_HARNESS_MEMORY_HPP

#include <level_zero/ze_api.h>
#include "utils/utils.hpp"
#include "gtest/gtest.h"

#define SKIP_IF_SHARED_SYSTEM_ALLOC_UNSUPPORTED()                              \
  do {                                                                         \
    if (!level_zero_tests::supports_shared_system_alloc()) {                   \
      GTEST_SKIP() << "Device does not support shared system "                 \
                      "allocation, skipping the test";                         \
    }                                                                          \
  } while (0)

namespace level_zero_tests {

enum lztWin32HandleTestType {
  LZT_OPAQUE_WIN32 = 0,
  LZT_KMT_WIN32,
  LZT_D3D11_TEXTURE_WIN32,
  LZT_D3D11_TEXTURE_KMT_WIN32,
  LZT_D3D12_HEAP_WIN32,
  LZT_D3D12_RESOURCE_WIN32
};

const auto memory_allocation_sizes =
    ::testing::Values(1, 10, 100, 1000, 10000, 100000);
const auto memory_allocation_alignments = ::testing::Values(
    0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384,
    32768, 65536, 1u << 17, 1u << 18, 1u << 19, 1u << 20, 1u << 21, 1u << 22);
const auto memory_allocation_alignments_small =
    ::testing::Values(0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048,
                      4096, 8192, 16384, 32768, 65536);
const auto memory_allocation_alignments_large = ::testing::Values(
    1u << 17, 1u << 18, 1u << 19, 1u << 20, 1u << 21, 1u << 22, 1u << 23);

void *aligned_malloc(size_t size, size_t alignment);
void *aligned_malloc_no_check(size_t size, size_t alignment,
                              ze_result_t *result);

void *allocate_host_memory(const size_t size);
void *allocate_host_memory_with_allocator_selector(const size_t size,
                                                   bool is_shared_system);
void *allocate_host_memory(const size_t size, const size_t alignment);
void *allocate_host_memory(const size_t size, const size_t alignment,
                           const ze_context_handle_t context);
void *allocate_host_memory(const size_t size, const size_t alignment,
                           const ze_host_mem_alloc_flags_t flags, void *pNext,
                           ze_context_handle_t context);
void *allocate_host_memory_no_check(const size_t size, const size_t alignment,
                                    ze_context_handle_t context,
                                    ze_result_t *result);
void *allocate_device_memory(const size_t size);
void *allocate_device_memory_with_allocator_selector(const size_t size,
                                                     bool is_shared_system);
void *allocate_device_memory(const size_t size, const size_t alignment);
void *allocate_device_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t flags,
                             ze_context_handle_t context);
void *allocate_device_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t flags);
void *allocate_device_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t flags,
                             ze_device_handle_t device,
                             ze_context_handle_t context);
void *allocate_device_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t flags,
                             const uint32_t ordinal,
                             ze_device_handle_t device_handle,
                             ze_context_handle_t context);
void *allocate_device_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t flags,
                             void *pNext, const uint32_t ordinal,
                             ze_device_handle_t device_handle,
                             ze_context_handle_t context);
void *allocate_device_memory_no_check(const size_t size, const size_t alignment,
                                      const ze_device_mem_alloc_flags_t flags,
                                      void *pNext, const uint32_t ordinal,
                                      ze_device_handle_t device_handle,
                                      ze_context_handle_t context,
                                      ze_result_t *result);
void *allocate_shared_memory(const size_t size);
void *allocate_shared_memory_with_allocator_selector(const size_t size,
                                                     bool is_shared_system);
void *allocate_shared_memory(const size_t size, ze_device_handle_t device);

void *allocate_shared_memory(const size_t size, const size_t alignment);

void *allocate_shared_memory(const size_t size, const size_t alignment,
                             ze_context_handle_t context);

void *allocate_shared_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t dev_flags,
                             const ze_host_mem_alloc_flags_t host_flags);
void *allocate_shared_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t dev_flags,
                             const ze_host_mem_alloc_flags_t host_flags,
                             ze_device_handle_t device);
void *allocate_shared_memory_with_allocator_selector(
    const size_t size, const size_t alignment,
    const ze_device_mem_alloc_flags_t dev_flags,
    const ze_host_mem_alloc_flags_t host_flags, ze_device_handle_t device,
    bool is_shared_system);
void *allocate_shared_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t dev_flags,
                             const ze_host_mem_alloc_flags_t host_flags,
                             ze_device_handle_t device,
                             ze_context_handle_t context);
void *allocate_shared_memory_with_allocator_selector(
    const size_t size, const size_t alignment,
    const ze_device_mem_alloc_flags_t dev_flags,
    const ze_host_mem_alloc_flags_t host_flags, ze_device_handle_t device,
    ze_context_handle_t context, bool is_shared_system);
void *allocate_shared_memory(const size_t size, const size_t alignment,
                             const ze_device_mem_alloc_flags_t device_flags,
                             void *device_pNext,
                             const ze_host_mem_alloc_flags_t host_flags,
                             void *host_pNext, ze_device_handle_t device,
                             ze_context_handle_t context);
void *allocate_shared_memory_no_check(
    const size_t size, const size_t alignment,
    const ze_device_mem_alloc_flags_t device_flags, void *device_pNext,
    const ze_host_mem_alloc_flags_t host_flags, void *host_pNext,
    ze_device_handle_t device, ze_context_handle_t context,
    ze_result_t *result);
void allocate_mem(void **memory, ze_memory_type_t mem_type, size_t size,
                  ze_context_handle_t context);

void aligned_free(void *ptr);
void free_memory(const void *ptr);
void free_memory_with_allocator_selector(const void *ptr,
                                         bool is_shared_system);
void free_memory(ze_context_handle_t context, const void *ptr);
void free_memory_with_allocator_selector(ze_context_handle_t context,
                                         const void *ptr,
                                         bool is_shared_system);

void allocate_mem_and_get_ipc_handle(ze_context_handle_t context,
                                     ze_ipc_mem_handle_t *handle, void **memory,
                                     ze_memory_type_t mem_type);
void allocate_mem_and_get_ipc_handle(ze_context_handle_t context,
                                     ze_ipc_mem_handle_t *handle, void **memory,
                                     ze_memory_type_t mem_type, size_t size);
void get_ipc_handle(ze_context_handle_t context, ze_ipc_mem_handle_t *handle,
                    void *memory);
void put_ipc_handle(ze_context_handle_t context, ze_ipc_mem_handle_t handle);
void open_ipc_handle(ze_context_handle_t context, ze_device_handle_t device,
                     ze_ipc_mem_handle_t mem_handle, void **memory);
void close_ipc_handle(ze_context_handle_t context, void **memory);
void write_data_pattern(void *buff, size_t size, int8_t data_pattern);
void validate_data_pattern(void *buff, size_t size, int8_t data_pattern);
void get_mem_alloc_properties(
    ze_context_handle_t context, const void *memory,
    ze_memory_allocation_properties_t *memory_properties);
void get_mem_alloc_properties(
    ze_context_handle_t context, const void *memory,
    ze_memory_allocation_properties_t *memory_properties,
    ze_device_handle_t *device);
void query_page_size(ze_context_handle_t context, ze_device_handle_t device,
                     size_t alloc_size, size_t *page_size);
void virtual_memory_reservation(ze_context_handle_t context, const void *pStart,
                                size_t size, void **memory);
void virtual_memory_free(ze_context_handle_t context, void *memory,
                         size_t alloc_size);
void physical_memory_allocation(ze_context_handle_t context,
                                ze_device_handle_t device,
                                ze_physical_mem_desc_t *desc,
                                ze_physical_mem_handle_t *memory);
void physical_device_memory_allocation(ze_context_handle_t context,
                                       ze_device_handle_t device,
                                       size_t allocation_size,
                                       ze_physical_mem_handle_t *memory);
void physical_host_memory_allocation(ze_context_handle_t context,
                                     size_t allocation_size,
                                     ze_physical_mem_handle_t *memory);
void physical_memory_destroy(ze_context_handle_t context,
                             ze_physical_mem_handle_t memory);
void virtual_memory_map(ze_context_handle_t context,
                        const void *reservedVirtualMemory, size_t size,
                        ze_physical_mem_handle_t physical_memory, size_t offset,
                        ze_memory_access_attribute_t access);
void virtual_memory_unmap(ze_context_handle_t hContext,
                          const void *reservedVirtualMemory, size_t size);
size_t create_page_aligned_size(size_t requested_size, size_t page_size);
void virtual_memory_reservation_get_access(ze_context_handle_t context,
                                           const void *ptr, size_t size,
                                           ze_memory_access_attribute_t *access,
                                           size_t *outSize);
void virtual_memory_reservation_set_access(ze_context_handle_t context,
                                           const void *ptr, size_t size,
                                           ze_memory_access_attribute_t access);
void *reserve_allocate_and_map_device_memory(
    ze_context_handle_t context, ze_device_handle_t device, size_t &allocSize,
    ze_physical_mem_handle_t *reservedPhysicalMemory);
void unmap_and_free_reserved_memory(
    ze_context_handle_t context, void *reservedMemory,
    ze_physical_mem_handle_t reservedPhysicalMemory, size_t allocSize);

inline bool supports_shared_system_alloc(
    const ze_device_memory_access_properties_t &access) {
  const auto alloc_cap = access.sharedSystemAllocCapabilities;
  return (alloc_cap & ZE_MEMORY_ACCESS_CAP_FLAG_RW) != 0;
}

inline bool supports_shared_system_alloc(const ze_device_handle_t device) {
  return supports_shared_system_alloc(get_memory_access_properties(device));
}

inline bool supports_shared_system_alloc(const ze_driver_handle_t driver) {
  return supports_shared_system_alloc(get_default_device(driver));
}

inline bool supports_shared_system_alloc() {
  return supports_shared_system_alloc(get_default_driver());
}

}; // namespace level_zero_tests
#endif
