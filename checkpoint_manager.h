#ifndef CHECKPOINT_MANAGER_H
#define CHECKPOINT_MANAGER_H

#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <cstring>

// Define the size of each data chunk
#define CHUNK_SIZE (1L * 24 * 1024 * 1024) ///< Chunk size is 24 MB

/**
 * @brief Metadata structure to track file size and associated chunk indices.
 */
struct FileMetadata
{
  size_t size;                       ///< Total size of the file.
  std::vector<size_t> chunk_indexes; ///< Indices of chunks associated with the file.
};

/**
 * @brief Represents an individual data chunk for checkpointing.
 */
struct CheckpointChunk
{
  std::unique_ptr<char[]> buffer; ///< Memory buffer to store chunk data.
  size_t offset;                  ///< Current position in the buffer.
  bool full;                      ///< Indicates whether the chunk is full.

  /**
   * @brief Constructor to initialize the buffer and reset the offset.
   */
  CheckpointChunk();
};

/**
 * @brief Manager class to handle checkpointing operations.
 */
class CheckpointManager
{
private:
  std::unordered_map<std::string, FileMetadata> file_metadata;      ///< Tracks metadata of files.
  std::unordered_map<std::string, size_t> current_file_chunk_index; ///< Current chunk index for each file.
  std::vector<std::unique_ptr<CheckpointChunk>> chunks;             ///< Collection of all chunks.
  std::mutex mtx;                                                   ///< Mutex for thread-safety.
  size_t global_chunk_index = 0;                                    ///< Global index for current chunk.
  std::unordered_map<int, std::string> fd_to_path_map;              ///< Maps file descriptors to their paths.
  int current_fd = 0;                                               ///< Integer pointing to latest fd.

  /**
   * @brief Retrieves the current chunk or allocates a new one if the index exceeds existing chunks.
   * @param chunk_index Index of the chunk to retrieve.
   * @return Pointer to the current chunk.
   */
  CheckpointChunk *get_current_chunk(size_t chunk_index);

  /**
   * @brief Allocates a new chunk and updates the global chunk index.
   */
  void allocate_new_chunk();

public:
  /**
   * @brief Constructor initializes the manager and creates the first chunk.
   */
  CheckpointManager();

  /**
   * @brief Writes data into chunks, splitting it across multiple chunks if necessary.
   * @param filename The name of the file being written.
   * @param buf Pointer to the data to write.
   * @param count Number of bytes to write.
   * @param local_fd The local file descriptor associated with the file being written.
   */
  void write_checkpoint(const std::string &filename, const void *buf, size_t count, int local_fd);

  /**
   * @brief Finalizes file writing by sending the last partially filled chunk.
   * @param filename The name of the file to finalize.
   * @param local_fd The local file descriptor associated with the file.
   */
  int finalize_file_write(const std::string &filename, int local_fd);

  /**
   * @brief Reads from a file at a specified offset.
   * @param fd The file descriptor to read from.
   * @param buf Buffer to store the read data.
   * @param count Number of bytes to read.
   * @param offset Offset in the file to read from.
   * @return Number of bytes read, 0 on EOF, or -1 on error.
   */
  ssize_t pread(int fd, void *buf, size_t count, off_t offset);
};

extern CheckpointManager checkpoint_manager;
#endif
