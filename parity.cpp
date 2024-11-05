//
//  parity.cpp
//  HVAC_CHECKPOINT
//
//  Created by Farid Talibli on 5/11/24.
//

#include <iostream>
#include <vector>
#include <thread>
#include <bitset>

/**
 * @brief Computes the parity for a specific part of the data block.
 *
 * This function iterates through a specific part of the data, computes the XOR operation across
 * blocks, and stores the result in the `parity_block`.
 *
 * @param data The input vector containing the data for which parity needs to be computed.
 * @param data_size The size of the input data.
 * @param block_size The size of each block in the data.
 * @param part_index The index of the part currently being processed by this thread.
 * @param part_size The size of each part within a block.
 * @param parity_block The vector where the computed parity values will be stored.
 * @param thread_count The total number of threads used in the computation.
 */
void compute_parity(const std::vector<uint8_t> &data, size_t data_size, size_t block_size,
                    size_t part_index, size_t part_size, std::vector<uint8_t> &parity_block,
                    size_t thread_count)
{
  for (size_t i = 0; i < part_size; ++i)
  {
    uint8_t parity = 0;
    for (size_t block = 0; block < thread_count; ++block)
    {
      size_t data_index = i + part_size * part_index + block_size * block;
      if (data_index < data_size)
      {
        parity ^= data[data_index];
      }
    }
    if (part_index * part_size + i < block_size)
    {
      parity_block[part_index * part_size + i] = parity;
    }
  }
}

/**
 * @brief Orchestrates the parallel computation of data parity by splitting data into parts
 * and processing it in parallel using multiple threads.
 *
 * This function divides the input data into `n` blocks, where `n` equals `thread_count`.
 * Each block is then further divided into `n` parts. Each thread computes the parity
 * across corresponding parts of all blocks in parallel.
 *
 * @param data The input vector containing the data for which parity needs to be computed.
 * @param parity_block The vector where the computed parity values will be stored.
 * @param thread_count The total number of threads used for parallel computation.
 */
void compute_data_parity(std::vector<uint8_t> &data, std::vector<uint8_t> &parity_block, size_t thread_count)
{
  size_t data_size = data.size();
  if (data_size == 0 || thread_count == 0)
  {
    std::cerr << "Error: Data size or thread count is zero." << std::endl;
    return;
  }

  size_t block_size = (data_size + thread_count - 1) / thread_count;
  size_t part_size = (block_size + thread_count - 1) / thread_count;

  parity_block.resize(block_size, 0);
  std::vector<std::thread> threads;

  for (size_t i = 0; i < std::min(thread_count, (data_size + block_size - 1) / block_size); ++i)
  {
    if (i * part_size < data_size)
    {
      threads.emplace_back(
          compute_parity,
          std::ref(data),
          data_size,
          block_size,
          i,
          part_size,
          std::ref(parity_block),
          thread_count);
    }
  }

  for (auto &th : threads)
  {
    th.join();
  }
}
