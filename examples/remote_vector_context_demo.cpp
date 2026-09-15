#include <microfmt/inspector/remote_vector.hpp>
#include <microfmt/sinks/stdio.hpp>
#include <vector>

// Simulated remote vector header structure
struct RemoteVectorHeader {
  int *data;
  size_t size;
  size_t capacity;
};

int main() {
  auto space_ref =
      microfmt::address_space_ref::make<microfmt::local_space_tag>();
  std::byte scratch[1024];

  // Setup mock data in "remote" (local) memory
  std::vector<int> remote_elements = {100, 200, 300, 400, 500};
  RemoteVectorHeader remote_vec{remote_elements.data(), remote_elements.size(),
                                remote_elements.capacity()};
  uintptr_t vec_addr = reinterpret_cast<uintptr_t>(&remote_vec);

  // Define the remote_vector_vtable using raw function pointers
  microfmt::remote_vector_vtable vector_vtbl{
      .get_size =
          [](const void *state, microfmt::address_space_ref space,
             microfmt::span<std::byte>, size_t &out_size) noexcept {
            uintptr_t addr = *static_cast<const uintptr_t *>(state);
            RemoteVectorHeader h;
            if (!space.read_bytes(addr, &h, sizeof(RemoteVectorHeader)))
              return false;
            out_size = h.size;
            return true;
          },
      .get_capacity =
          [](const void *state, microfmt::address_space_ref space,
             microfmt::span<std::byte>, size_t &out_cap) noexcept {
            uintptr_t addr = *static_cast<const uintptr_t *>(state);
            RemoteVectorHeader h;
            if (!space.read_bytes(addr, &h, sizeof(RemoteVectorHeader)))
              return false;
            out_cap = h.capacity;
            return true;
          },
      .get_element_address =
          [](const void *state, microfmt::address_space_ref space,
             microfmt::span<std::byte>, size_t index,
             uintptr_t &out_elem_addr) noexcept {
            uintptr_t addr = *static_cast<const uintptr_t *>(state);
            RemoteVectorHeader h;
            if (!space.read_bytes(addr, &h, sizeof(RemoteVectorHeader)))
              return false;
            out_elem_addr =
                reinterpret_cast<uintptr_t>(h.data) + (index * sizeof(int));
            return true;
          },
      .format_element =
          [](const void *, microfmt::address_space_ref space,
             microfmt::span<std::byte>, uintptr_t elem_addr,
             const microfmt::sink &out) noexcept {
            int val = 0;
            if (!space.read(elem_addr, val))
              return false;
            microfmt::format_to(out, "{}", val);
            return true;
          }};

  // Create the remote vector context using make_remote_vector_context
  // We pass the container address as the initial user state.
  auto vector_context =
      microfmt::make_remote_vector_context(vec_addr, vec_addr, vector_vtbl);

  // Configure formatting options (e.g., custom brackets and separators)
  microfmt::container_options vector_opts{.entry_separator = ", ",
                                          .open_bracket = "[",
                                          .close_bracket = "]",
                                          .max_print = 64};

  // Instantiate remote_container_view, binding to the context pointer
  // (&vector_context)
  microfmt::remote_container_view container_view(vec_addr, space_ref, scratch,
                                                 &vector_context, vector_opts);

  // Print the type-erased vector view safely
  microfmt::print("Inspected Remote Vector: {}\n", container_view);
  // Output: Inspected Remote Vector: [100, 200, 300, 400, 500]

  return 0;
}