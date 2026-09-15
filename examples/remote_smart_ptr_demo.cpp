#include "microfmt/inspector/address_space.hpp"
#include "microfmt/inspector/remote_object.hpp"
#include "microfmt/inspector/remote_smart_ptr.hpp"
#include "microfmt/sinks/stdio.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// ============================================================================
// Target Target Data Structures (Simulating Remote Memory Layout)
// ============================================================================

// A resource managed by an intrusive pointer
struct RemoteResource {
  int ref_count;
  int error_code;
};

// A control block layout matching standard library shared_ptr structures
struct MockControlBlock {
  int32_t use_count;
  int32_t weak_count;
};

// The main remote structure we want to inspect
struct RemoteGameEntity {
  uint32_t entity_id;
  microfmt::string_ptr name;                   // Remote string pointer
  microfmt::remote_unique_ptr<int> health_ptr; // Remote unique_ptr
  microfmt::remote_shared_ptr<long> score_ptr; // Remote shared_ptr
  microfmt::remote_intrusive_ptr<RemoteResource,
                                 offsetof(RemoteResource, ref_count)>
      active_resource;
};

// ============================================================================
// Register Remote Structures using Macro DSL
// ============================================================================

MICROFMT_REMOTE_STRUCT_BEGIN(RemoteResource)
MICROFMT_REMOTE_FIELD(ref_count, int)
MICROFMT_REMOTE_FIELD(error_code, int)
MICROFMT_REMOTE_STRUCT_END()

MICROFMT_REMOTE_STRUCT_BEGIN(RemoteGameEntity)
MICROFMT_REMOTE_FIELD(entity_id, uint32_t)
MICROFMT_REMOTE_FIELD(name, microfmt::string_ptr)
MICROFMT_REMOTE_FIELD(health_ptr, microfmt::remote_unique_ptr<int>)
MICROFMT_REMOTE_FIELD(score_ptr, microfmt::remote_shared_ptr<long>)
MICROFMT_REMOTE_FIELD(active_resource,
                      microfmt::remote_intrusive_ptr<
                          RemoteResource, offsetof(RemoteResource, ref_count)>)
MICROFMT_REMOTE_STRUCT_END()

int main() {
  // Setup local address space ref and a scratch buffer for zero-allocation
  // parsing
  auto space_ref =
      microfmt::address_space_ref::make<microfmt::local_space_tag>();
  std::byte scratch[4096];

  // Populate mock remote memory objects
  const char *entity_name_str = "Player_One";
  int local_health = 100;

  RemoteResource local_resource{.ref_count = 2, .error_code = 0};

  // Construct the remote entity mock instance
  RemoteGameEntity remote_entity{
      .entity_id = 42,
      .name = microfmt::string_ptr(entity_name_str),
      .health_ptr = microfmt::remote_unique_ptr<int>(
          reinterpret_cast<uintptr_t>(&local_health)),
      .score_ptr = microfmt::remote_shared_ptr<long>(), // we will manually mock
                                                        // pointers if needed or
                                                        // assign via layout
      .active_resource =
          microfmt::remote_intrusive_ptr<RemoteResource,
                                         offsetof(RemoteResource, ref_count)>(
              reinterpret_cast<uintptr_t>(&local_resource))};

  // (Optional setup for shared_ptr mock pointers inside layout)
  // On a real remote target, these fields point directly to heap addresses.
  // For this local simulation test, we can set up the raw pointer members via
  // memcpy or placement if desired, or rely on the views reading from the
  // struct buffer layout.

  uintptr_t entity_addr = reinterpret_cast<uintptr_t>(&remote_entity);

  // Create a type-erased remote_object_view for RemoteGameEntity
  microfmt::remote_object_view entity_view(
      entity_addr, space_ref, microfmt::type_tag<RemoteGameEntity>{}, scratch);

  // Inspect and print the entire object graph recursively using
  // microfmt::print!
  microfmt::print("Inspected RemoteGameEntity:\n{}\n", entity_view);

  return 0;
}