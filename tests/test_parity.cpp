// test/test_parity.cpp
#include <iostream>
#include <vector>
#include <numeric>
#include <cassert>
#include "parity.cpp"

void printVector(const std::vector<uint8_t> &vec, const std::string &label)
{
  std::cout << label << ": ";
  for (uint8_t val : vec)
  {
    std::cout << static_cast<int>(val) << " ";
  }
  std::cout << std::endl;
}

void assertEqual(const std::vector<uint8_t> &vec1, const std::vector<uint8_t> &vec2, const std::string &message)
{
  assert(vec1 == vec2 && message.c_str());
  std::cout << message << " - Passed" << std::endl;
}

int main()
{
  // Edge Case 1: Empty data
  {
    std::vector<uint8_t> data;
    std::vector<uint8_t> parity_block;
    size_t thread_count = 4;

    compute_data_parity(data, parity_block, thread_count);
    assertEqual(parity_block, std::vector<uint8_t>(), "Test 1 (Empty data)");
  }

  // Edge Case 2: Data size smaller than thread count
  {
    std::vector<uint8_t> data = {1, 2, 3};
    std::vector<uint8_t> parity_block;
    size_t thread_count = 4;

    compute_data_parity(data, parity_block, thread_count);
    printVector(parity_block, "Test 2 (Data size smaller than thread count)");
  }

  // Edge Case 3: Data size exactly matching thread count
  {
    std::vector<uint8_t> data = {1, 2, 3, 4};
    std::vector<uint8_t> parity_block;
    size_t thread_count = 4;

    compute_data_parity(data, parity_block, thread_count);
    printVector(parity_block, "Test 3 (Data size matching thread count)");
  }

  // Edge Case 4: Single element data
  {
    std::vector<uint8_t> data = {42};
    std::vector<uint8_t> parity_block;
    size_t thread_count = 1;

    compute_data_parity(data, parity_block, thread_count);
    printVector(parity_block, "Test 4 (Single element data)");
    assertEqual(parity_block, std::vector<uint8_t>{42}, "Test 4 (Single element data)");
  }

  // Edge Case 5: Data size is a multiple of thread count
  {
    std::vector<uint8_t> data(16);
    std::iota(data.begin(), data.end(), 1);

    std::vector<uint8_t> parity_block;
    size_t thread_count = 4;

    compute_data_parity(data, parity_block, thread_count);
    printVector(parity_block, "Test 5 (Data size is a multiple of thread count)");
  }

  // Generic Test Case 1:
  {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint8_t> parity_block;
    size_t thread_count = 2;

    compute_data_parity(data, parity_block, thread_count);
    printVector(data, "Test 6 (Specific input)");

    // Expected output: {7, 5, 11, 13, 15}
    std::vector<uint8_t> expected_output = {7, 5, 11, 13, 15};
    assertEqual(parity_block, expected_output, "Test 6");
  }

  std::cout << "All tests completed." << std::endl;
  return 0;
}
