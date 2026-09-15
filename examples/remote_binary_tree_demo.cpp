#include "microfmt/inspector/address_space.hpp"
#include "microfmt/inspector/remote_binary_tree.hpp"
#include "microfmt/sinks/stdio.hpp"
#include <cstddef>
#include <cstdint>

// Simulated binary search tree node structure in memory
struct BSTNode {
  BSTNode *left;
  BSTNode *right;
  int key;
  int value;
};

// Simulated tree container holding the root pointer
struct RemoteTreeContainer {
  BSTNode *root;
};

int main() {
  auto space_ref =
      microfmt::address_space_ref::make<microfmt::local_space_tag>();
  std::byte
      scratch[2048]; // Scratch buffer used for stack partitioning & node reads

  // Build a mock Binary Search Tree structure in memory:
  //          [40 : 400]
  //         /          \
  //    [20 : 200]    [60 : 600]
  //      /
  // [10 : 100]
  BSTNode node10{nullptr, nullptr, 10, 100};
  BSTNode node20{&node10, nullptr, 20, 200};
  BSTNode node60{nullptr, nullptr, 60, 600};
  BSTNode node40{&node20, &node60, 40, 400};

  RemoteTreeContainer remote_tree{&node40};
  uintptr_t tree_addr = reinterpret_cast<uintptr_t>(&remote_tree);

  // Generate the context generator using
  // remote_binary_tree_traits::bst_layout
  auto tree_generator =
      microfmt::remote_binary_tree_traits::bst_layout<int, int>(
          offsetof(RemoteTreeContainer, root), offsetof(BSTNode, left),
          offsetof(BSTNode, right), offsetof(BSTNode, key),
          offsetof(BSTNode, value));

  // Generate the actual binary tree context by passing the tree container
  // address
  auto tree_context = tree_generator(tree_addr);

  // Configure formatting options (e.g., custom key-value separators and
  // brackets)
  microfmt::container_options tree_opts{.kv_separator = " => ",
                                        .entry_separator = ", ",
                                        .open_bracket = "{ ",
                                        .close_bracket = " }"};

  // Wrap in remote_container_view passing context pointer and options
  microfmt::remote_container_view container_view(tree_addr, space_ref, scratch,
                                                 &tree_context, tree_opts);

  // Print the inspected tree (In-order traversal outputs sorted keys: 10,
  // 20, 40, 60)
  microfmt::print("Inspected BST: {}\n", container_view);
  // Expected Output: Inspected BST: { 10 => 100, 20 => 200, 40 => 400, 60 =>
  // 600 }

  return 0;
}