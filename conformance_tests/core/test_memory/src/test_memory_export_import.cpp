/*
 *
 * Copyright (C) 2021-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <chrono>
#include <thread>
#include <boost/process.hpp>
#include <boost/filesystem.hpp>
#ifdef __linux__
#include <sys/mman.h>
#endif

#include "gtest/gtest.h"

#include "utils/utils.hpp"
#include "test_harness/test_harness.hpp"
#include "logging/logging.hpp"
#ifdef __linux__
#include "net/unix_comm.hpp"
#endif
#include <sstream>
#include <string>

namespace bp = boost::process;
namespace fs = boost::filesystem;

namespace lzt = level_zero_tests;

#include <level_zero/ze_api.h>

namespace {
typedef struct {
  int fd;
  bool is_immediate;
} thread_args;

#ifdef __linux__
static int get_imported_fd(std::string driver_id, bp::opstream &child_input,
                           bool is_immediate) {
  int fd;
  const char *socket_path = "external_memory_socket";

  // launch a new process that exports memory
  fs::path helper_path(fs::current_path() / "memory");
  std::vector<fs::path> paths;
  paths.push_back(helper_path);
  fs::path helper = bp::search_path("test_import_helper", paths);
  bp::child import_memory_helper(
      helper, bp::args({driver_id, is_immediate ? "1" : "0"}),
      bp::std_in < child_input);
  import_memory_helper.detach();

  struct sockaddr_un local_addr, remote_addr;
  local_addr.sun_family = AF_UNIX;
  strcpy(local_addr.sun_path, socket_path);
  unlink(local_addr.sun_path);

  int receive_socket = socket(AF_UNIX, SOCK_STREAM, 0);
  if (receive_socket < 0) {
    perror("Socket Create Error");
    throw std::runtime_error("Could not create socket");
  }

  if (bind(receive_socket, (struct sockaddr *)&local_addr,
           strlen(local_addr.sun_path) + sizeof(local_addr.sun_family)) < 0) {
    close(receive_socket);
    perror("Socket Bind Error");
    throw std::runtime_error("Could not bind to socket");
  }

  LOG_DEBUG << "Receiver listening...";

  if (listen(receive_socket, 1) < 0) {
    close(receive_socket);
    perror("Socket Listen Error");
    throw std::runtime_error("Could not listen on socket");
  }

  int len = sizeof(struct sockaddr_un);
  int other_socket = accept(receive_socket, (struct sockaddr *)&remote_addr,
                            (socklen_t *)&len);
  if (other_socket == -1) {
    close(receive_socket);
    perror("Server Accept Error");
    throw std::runtime_error("[Server] Could not accept connection");
  }
  LOG_DEBUG << "[Server] Connection accepted";

  char data[ZE_MAX_IPC_HANDLE_SIZE];
  if ((fd = lzt::read_fd_from_socket(other_socket, data)) < 0) {
    close(other_socket);
    close(receive_socket);
    throw std::runtime_error("Failed to receive memory fd from exporter");
  }
  LOG_DEBUG << "[Server] Received memory file descriptor from client";

  close(other_socket);
  close(receive_socket);
  return fd;
}
#else
static int send_handle(std::string driver_id, bp::opstream &child_input,
                       uint64_t handle, lzt::lztWin32HandleTestType handleType,
                       bool is_immediate) {
  // launch a new process that exports memory
  fs::path helper_path(fs::current_path() / "memory");
  std::vector<fs::path> paths;
  paths.push_back(helper_path);
  bp::ipstream output;
  fs::path helper = bp::search_path("test_import_helper", paths);
  bp::child import_memory_helper(
      helper, bp::args({driver_id, is_immediate ? "1" : "0"}),
      bp::std_in<child_input, bp::std_out> output);
  HANDLE targetHandle;
  auto result =
      DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(handle),
                      import_memory_helper.native_handle(), &targetHandle,
                      DUPLICATE_SAME_ACCESS, TRUE, DUPLICATE_SAME_ACCESS);
  if (result > 0) {
    child_input << handleType << std::endl;

    BOOL pipeConnected = FALSE;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    BOOL writeSuccess = FALSE;
    DWORD bytesWritten;
    LPCTSTR externalMemoryTestPipeName =
        TEXT("\\\\.\\pipe\\external_memory_socket");
    hPipe = CreateNamedPipe(
        externalMemoryTestPipeName, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, sizeof(uint64_t), sizeof(uint64_t), 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) {
      LOG_ERROR << "CreateNamedPipe failed with Error " << GetLastError()
                << std::endl;
      return -1;
    }

    pipeConnected = ConnectNamedPipe(hPipe, NULL)
                        ? TRUE
                        : (GetLastError() == ERROR_PIPE_CONNECTED);

    if (!pipeConnected) {
      import_memory_helper.terminate();
      CloseHandle(hPipe);
      return -1;
    }
    writeSuccess =
        WriteFile(hPipe, &targetHandle, sizeof(uint64_t), &bytesWritten, NULL);

    if (!writeSuccess) {
      LOG_ERROR << "WriteFile to pipe failed with Error " << GetLastError()
                << std::endl;
      return -1;
    }
    std::ostringstream streamHandle;
    streamHandle << targetHandle;
    std::string handleString = streamHandle.str();
    child_input << handleString << std::endl;
    std::string line;

    while (std::getline(output, line) && !line.empty())
      LOG_INFO << line << std::endl;
    import_memory_helper.wait();
    return import_memory_helper.native_exit_code();
    CloseHandle(hPipe);
  } else {
    import_memory_helper.terminate();
    return -1;
  }
}
#endif

class zeDeviceGetExternalMemoryProperties : public ::testing::Test {
protected:
  void RunGivenValidDeviceWhenExportingMemoryAsDMABufTest(bool is_immediate);
  void RunGivenValidDeviceWhenImportingMemoryTest(bool is_immediate);
  void
  RunGivenValidDeviceWhenImportingMemoryWithNTHandleTest(bool is_immediate);
  void
  RunGivenValidDeviceWhenImportingMemoryWithKMTHandleTest(bool is_immediate);
  void
  RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInSameThreadTest(
      bool is_immediate);
  void
  RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInMultiThreadTest(
      bool is_immediate);
};

#ifdef __linux__
void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInSameThreadTest(
        bool is_immediate) {
  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto device = lzt::get_default_device(driver);

  /* Export Memory As DMA_BUF*/
  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationExportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF)) {
    GTEST_SKIP() << "Device does not support exporting DMA_BUF";
  }

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;

  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  void *exported_memory;
  auto size = 1024;
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                             &exported_memory));

  // Fill the allocated memory with some pattern so we can verify
  // it was exported successfully
  auto export_cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, is_immediate);

  uint8_t pattern = 0xAB;
  lzt::append_memory_fill(export_cmd_bundle.list, exported_memory, &pattern,
                          sizeof(pattern), size, nullptr);
  lzt::close_command_list(export_cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(export_cmd_bundle, UINT64_MAX);

  // set up request to export the external memory handle
  ze_external_memory_export_fd_t export_fd = {};
  export_fd.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_FD;
  export_fd.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
  ze_memory_allocation_properties_t alloc_props = {};
  alloc_props.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
  alloc_props.pNext = &export_fd;
  lzt::get_mem_alloc_properties(context, exported_memory, &alloc_props);
  EXPECT_NE(export_fd.fd, 0);

  /* Import exported Memory As DMA_BUF*/
  auto external_memory_import_properties =
      lzt::get_external_memory_properties(device);
  if (!(external_memory_import_properties.memoryAllocationImportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF)) {
    LOG_WARNING << "Device does not support importing DMA_BUF";
    GTEST_SKIP();
  }
  auto import_cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, is_immediate);

  void *imported_memory;
  auto import_mem_size = 1024;

  ze_external_memory_import_fd_t import_fd = {};
  import_fd.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD;
  import_fd.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
  import_fd.fd = export_fd.fd;

  ze_device_mem_alloc_desc_t device_alloc_import_desc = {};
  device_alloc_import_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_import_desc.pNext = &import_fd;
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeMemAllocDevice(context, &device_alloc_import_desc,
                             import_mem_size, 1, device, &imported_memory));

  auto verification_memory =
      lzt::allocate_shared_memory(import_mem_size, 1, 0, 0, device, context);
  lzt::append_memory_copy(import_cmd_bundle.list, verification_memory,
                          imported_memory, import_mem_size);
  lzt::close_command_list(import_cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(import_cmd_bundle, UINT64_MAX);

  for (size_t i = 0; i < import_mem_size; i++) {
    EXPECT_EQ(static_cast<uint8_t *>(verification_memory)[i],
              0xAB); // this pattern is written in test_import_helper
  }

  // cleanup
  lzt::destroy_command_bundle(export_cmd_bundle);
  lzt::free_memory(context, exported_memory);
  lzt::free_memory(context, imported_memory);
  lzt::free_memory(context, verification_memory);
  lzt::destroy_command_bundle(import_cmd_bundle);
  lzt::destroy_context(context);
}

void memory_import_thread(thread_args *args) {
  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationImportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF)) {
    LOG_WARNING << "Device does not support importing DMA_BUF";
    GTEST_SKIP();
  }
  auto cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, args->is_immediate);

  void *imported_memory;
  auto size = 1024;

  ze_external_memory_import_fd_t import_fd = {};
  import_fd.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD;
  import_fd.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
  import_fd.fd = args->fd;

  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &import_fd;
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                             &imported_memory));

  auto verification_memory =
      lzt::allocate_shared_memory(size, 1, 0, 0, device, context);
  lzt::append_memory_copy(cmd_bundle.list, verification_memory, imported_memory,
                          size);
  lzt::close_command_list(cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(cmd_bundle, UINT64_MAX);

  for (size_t i = 0; i < size; i++) {
    EXPECT_EQ(static_cast<uint8_t *>(verification_memory)[i],
              0xAB); // this pattern is written in test_import_helper
  }

  // cleanup
  lzt::free_memory(context, imported_memory);
  lzt::free_memory(context, verification_memory);
  lzt::destroy_command_bundle(cmd_bundle);
  lzt::destroy_context(context);
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInMultiThreadTest(
        bool is_immediate) {
  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto device = lzt::get_default_device(driver);

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationExportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF)) {
    GTEST_SKIP() << "Device does not support exporting DMA_BUF";
  }

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;

  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  void *exported_memory;
  auto size = 1024;
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                             &exported_memory));

  // Fill the allocated memory with some pattern so we can verify
  // it was exported successfully
  auto cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, is_immediate);

  uint8_t pattern = 0xAB;
  lzt::append_memory_fill(cmd_bundle.list, exported_memory, &pattern,
                          sizeof(pattern), size, nullptr);
  lzt::close_command_list(cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(cmd_bundle, UINT64_MAX);

  // set up request to export the external memory handle
  ze_external_memory_export_fd_t export_fd = {};
  export_fd.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_FD;
  export_fd.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
  ze_memory_allocation_properties_t alloc_props = {};
  alloc_props.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
  alloc_props.pNext = &export_fd;
  lzt::get_mem_alloc_properties(context, exported_memory, &alloc_props);
  EXPECT_NE(export_fd.fd, 0);

  // spawn a new thread and pass fd as argument
  thread_args args = {};
  args.fd = export_fd.fd;
  args.is_immediate = is_immediate;
  std::thread thread(memory_import_thread, &args);

  thread.join();

  // cleanup
  lzt::destroy_command_bundle(cmd_bundle);
  lzt::free_memory(context, exported_memory);
  lzt::destroy_context(context);
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenExportingMemoryAsDMABufTest(bool is_immediate) {
  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto device = lzt::get_default_device(driver);

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationExportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF)) {
    GTEST_SKIP() << "Device does not support exporting DMA_BUF";
  }

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;

  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  void *exported_memory;
  auto size = 1024;
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                             &exported_memory));

  // Fill the allocated memory with some pattern so we can verify
  // it was exported successfully
  auto cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, is_immediate);

  uint8_t pattern = 0xAB;
  lzt::append_memory_fill(cmd_bundle.list, exported_memory, &pattern,
                          sizeof(pattern), size, nullptr);
  lzt::close_command_list(cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(cmd_bundle, UINT64_MAX);

  // set up request to export the external memory handle
  ze_external_memory_export_fd_t export_fd = {};
  export_fd.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_FD;
  export_fd.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
  ze_memory_allocation_properties_t alloc_props = {};
  alloc_props.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
  alloc_props.pNext = &export_fd;
  lzt::get_mem_alloc_properties(context, exported_memory, &alloc_props);

  // mmap the fd to the exported device memory to the host space
  // and verify host can read
  EXPECT_NE(export_fd.fd, 0);
  auto mapped_memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, export_fd.fd, 0);

  if (mapped_memory == MAP_FAILED) {
    perror("Error:");
    if (errno == ENODEV) {
      FAIL() << "Filesystem does not support memory mapping: "
                "ZE_RESULT_ERROR_UNSUPPORTED_FEATURE";
    } else {
      FAIL() << "Error mmap-ing exported file descriptor";
    }
  }

  for (size_t i = 0; i < size; i++) {
    EXPECT_EQ(static_cast<uint8_t *>(mapped_memory)[i], pattern);
  }

  // cleanup
  lzt::destroy_command_bundle(cmd_bundle);
  lzt::free_memory(context, exported_memory);
  lzt::destroy_context(context);
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenImportingMemoryTest(bool is_immediate) {
  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationImportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF)) {
    LOG_WARNING << "Device does not support importing DMA_BUF";
    GTEST_SKIP();
  }
  auto cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, is_immediate);

  void *imported_memory;
  auto size = 1024;

  // set up request to import the external memory handle
  auto driver_properties = lzt::get_driver_properties(driver);
  bp::opstream child_input;
  auto imported_fd = get_imported_fd(lzt::to_string(driver_properties.uuid),
                                     child_input, is_immediate);
  ze_external_memory_import_fd_t import_fd = {};
  import_fd.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD;
  import_fd.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
  import_fd.fd = imported_fd;

  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &import_fd;
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                             &imported_memory));

  auto verification_memory =
      lzt::allocate_shared_memory(size, 1, 0, 0, device, context);
  lzt::append_memory_copy(cmd_bundle.list, verification_memory, imported_memory,
                          size);
  lzt::close_command_list(cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(cmd_bundle, UINT64_MAX);

  LOG_DEBUG << "Importer sending done msg " << std::endl;
  // import helper can now call free on its handle to memory
  child_input << "Done"
              << std::endl; // The content of this message doesn't really matter

  for (size_t i = 0; i < size; i++) {
    EXPECT_EQ(static_cast<uint8_t *>(verification_memory)[i],
              0xAB); // this pattern is written in test_import_helper
  }

  // cleanup
  lzt::free_memory(context, imported_memory);
  lzt::free_memory(context, verification_memory);
  lzt::destroy_command_bundle(cmd_bundle);
  lzt::destroy_context(context);
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenImportingMemoryWithNTHandleTest(bool is_immediate) {
  GTEST_SKIP() << "Test Not Supported on Linux";
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenImportingMemoryWithKMTHandleTest(bool is_immediate) {
  GTEST_SKIP() << "Test Not Supported on Linux";
}

#else

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInSameThreadTest(
        bool is_immediate) {
  GTEST_SKIP() << "Test Not Supported on Windows";
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInMultiThreadTest(
        bool is_immediate) {
  GTEST_SKIP() << "Test Not Supported on Windows";
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenExportingMemoryAsDMABufTest(bool is_immediate) {
  GTEST_SKIP() << "Test Not Supported on Windows";
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenImportingMemoryTest(bool is_immediate) {
  GTEST_SKIP() << "Test Not Supported on Windows";
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenImportingMemoryWithNTHandleTest(bool is_immediate) {
  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationImportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32)) {
    LOG_WARNING << "Device does not support exporting OPAQUE_WIN32";
    GTEST_SKIP();
  }
  void *exported_memory;
  auto size = 1024;

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32;
  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  auto result = zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                                 &exported_memory);
  if (ZE_RESULT_SUCCESS != result) {
    FAIL() << "Error allocating device memory to be imported\n";
  }

  auto cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, is_immediate);

  uint8_t pattern = 0xAB;
  lzt::append_memory_fill(cmd_bundle.list, exported_memory, &pattern,
                          sizeof(pattern), size, nullptr);
  lzt::close_command_list(cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(cmd_bundle, UINT64_MAX);

  ze_external_memory_export_win32_handle_t export_handle = {};
  export_handle.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_WIN32;
  export_handle.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32;
  ze_memory_allocation_properties_t alloc_props = {};
  alloc_props.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
  alloc_props.pNext = &export_handle;
  lzt::get_mem_alloc_properties(context, exported_memory, &alloc_props);
  auto driver_properties = lzt::get_driver_properties(driver);
  bp::opstream child_input;
  int child_result =
      send_handle(lzt::to_string(driver_properties.uuid), child_input,
                  reinterpret_cast<uint64_t>(export_handle.handle),
                  lzt::lztWin32HandleTestType::LZT_OPAQUE_WIN32, is_immediate);

  // cleanup
  lzt::free_memory(context, exported_memory);
  lzt::destroy_command_bundle(cmd_bundle);
  lzt::destroy_context(context);
  if (child_result != 0) {
    FAIL() << "Child Failed import\n";
  }
}

void zeDeviceGetExternalMemoryProperties::
    RunGivenValidDeviceWhenImportingMemoryWithKMTHandleTest(bool is_immediate) {
  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationImportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32_KMT)) {
    LOG_WARNING << "Device does not support exporting WIN32 KMT Handle";
    GTEST_SKIP();
  }
  void *exported_memory;
  auto size = 1024;

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32_KMT;
  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  auto result = zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                                 &exported_memory);
  if (ZE_RESULT_SUCCESS != result) {
    FAIL() << "Error allocating device memory to be imported\n";
  }

  auto cmd_bundle = lzt::create_command_bundle(
      context, device, 0, ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0, 0, is_immediate);

  uint8_t pattern = 0xAB;
  lzt::append_memory_fill(cmd_bundle.list, exported_memory, &pattern,
                          sizeof(pattern), size, nullptr);
  lzt::close_command_list(cmd_bundle.list);
  lzt::execute_and_sync_command_bundle(cmd_bundle, UINT64_MAX);

  ze_external_memory_export_win32_handle_t export_handle = {};
  export_handle.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_WIN32;
  export_handle.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32_KMT;
  ze_memory_allocation_properties_t alloc_props = {};
  alloc_props.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
  alloc_props.pNext = &export_handle;
  lzt::get_mem_alloc_properties(context, exported_memory, &alloc_props);
  auto driver_properties = lzt::get_driver_properties(driver);
  bp::opstream child_input;
  int child_result =
      send_handle(lzt::to_string(driver_properties.uuid), child_input,
                  reinterpret_cast<uint64_t>(export_handle.handle),
                  lzt::lztWin32HandleTestType::LZT_KMT_WIN32, is_immediate);

  LOG_DEBUG << "Exporter sending done msg " << std::endl;

  // cleanup
  lzt::free_memory(context, exported_memory);
  lzt::destroy_command_bundle(cmd_bundle);
  lzt::destroy_context(context);
  if (child_result != 0) {
    FAIL() << "Child Failed import\n";
  }
}

#endif // __linux__

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryAsDMABufThenSameThreadCanImportBufferContainingValidData) {
  RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInSameThreadTest(
      false);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryAsDMABufOnImmediateCmdListThenSameThreadCanImportBufferContainingValidData) {
  RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInSameThreadTest(
      true);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryAsDMABufThenOtherThreadCanImportBufferContainingValidData) {
  RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInMultiThreadTest(
      false);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryAsDMABufOnImmediateCmdListThenOtherThreadCanImportBufferContainingValidData) {
  RunGivenValidDeviceWhenExportingAndImportingMemoryAsDMABufInMultiThreadTest(
      true);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryAsDMABufThenHostCanMMAPBufferContainingValidData) {
  RunGivenValidDeviceWhenExportingMemoryAsDMABufTest(false);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryAsDMABufOnImmediateCmdListThenHostCanMMAPBufferContainingValidData) {
  RunGivenValidDeviceWhenExportingMemoryAsDMABufTest(true);
}

TEST_F(zeDeviceGetExternalMemoryProperties,
       GivenValidDeviceWhenImportingMemoryThenImportedBufferHasCorrectData) {
  RunGivenValidDeviceWhenImportingMemoryTest(false);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenImportingMemoryOnImmediateCmdListThenImportedBufferHasCorrectData) {
  RunGivenValidDeviceWhenImportingMemoryTest(true);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenImportingMemoryWithNTHandleThenImportedBufferHasCorrectData) {
  RunGivenValidDeviceWhenImportingMemoryWithNTHandleTest(false);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenImportingMemoryWithNTHandleOnImmediateCmdListThenImportedBufferHasCorrectData) {
  RunGivenValidDeviceWhenImportingMemoryWithNTHandleTest(true);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenImportingMemoryWithKMTHandleThenImportedBufferHasCorrectData) {
  RunGivenValidDeviceWhenImportingMemoryWithKMTHandleTest(false);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenImportingMemoryWithKMTHandleOnImmediateCmdListThenImportedBufferHasCorrectData) {
  RunGivenValidDeviceWhenImportingMemoryWithKMTHandleTest(true);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryWithD3DTextureThenResourceSuccessfullyExported) {

  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationExportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE)) {
    LOG_WARNING << "Device does not support exporting D3D Texture";
    GTEST_SKIP();
  }
  void *exported_memory;
  auto size = 1024;

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE;
  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  auto result = zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                                 &exported_memory);
  if (ZE_RESULT_SUCCESS != result) {
    FAIL() << "Error allocating device memory to be imported\n";
  }
  // cleanup
  lzt::free_memory(context, exported_memory);
  lzt::destroy_context(context);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryWithD3DTextureKmtThenResourceSuccessfullyExported) {

  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationExportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE_KMT)) {
    LOG_WARNING << "Device does not support exporting D3D Texture KMT";
    GTEST_SKIP();
  }
  void *exported_memory;
  auto size = 1024;

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE_KMT;
  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  auto result = zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                                 &exported_memory);
  if (ZE_RESULT_SUCCESS != result) {
    FAIL() << "Error allocating device memory to be imported\n";
  }
  // cleanup
  lzt::free_memory(context, exported_memory);
  lzt::destroy_context(context);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryWithD3D12HeapThenResourceSuccessfullyExported) {

  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationExportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_HEAP)) {
    LOG_WARNING << "Device does not support exporting D3D12 Heap";
    GTEST_SKIP();
  }
  void *exported_memory;
  auto size = 1024;

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_HEAP;
  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  auto result = zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                                 &exported_memory);
  if (ZE_RESULT_SUCCESS != result) {
    FAIL() << "Error allocating device memory to be imported\n";
  }
  // cleanup
  lzt::free_memory(context, exported_memory);
  lzt::destroy_context(context);
}

TEST_F(
    zeDeviceGetExternalMemoryProperties,
    GivenValidDeviceWhenExportingMemoryWithD3D12ResourceThenResourceSuccessfullyExported) {

  auto driver = lzt::get_default_driver();
  auto context = lzt::create_context(driver);
  auto devices = lzt::get_ze_devices(driver);
  auto device = devices[0];

  auto external_memory_properties = lzt::get_external_memory_properties(device);
  if (!(external_memory_properties.memoryAllocationExportTypes &
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE)) {
    LOG_WARNING << "Device does not support exporting D3D12 Resource";
    GTEST_SKIP();
  }
  void *exported_memory;
  auto size = 1024;

  ze_external_memory_export_desc_t export_desc = {};
  export_desc.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
  export_desc.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE;
  ze_device_mem_alloc_desc_t device_alloc_desc = {};
  device_alloc_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
  device_alloc_desc.pNext = &export_desc;

  auto result = zeMemAllocDevice(context, &device_alloc_desc, size, 1, device,
                                 &exported_memory);
  if (ZE_RESULT_SUCCESS != result) {
    FAIL() << "Error allocating device memory to be imported\n";
  }
  // cleanup
  lzt::free_memory(context, exported_memory);
  lzt::destroy_context(context);
}

} // namespace
