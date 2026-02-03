/**
 * Coordination File Implementation
 */

#include "phoenix/coordination/coordination_file.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace phoenix {
namespace coordination {

CoordinationResult CoordinationFile::Create(
    const std::string& file_path,
    const CoordinationMetadata& metadata) {

  // Generate JSON content
  std::ostringstream json;
  json << "{\n";
  json << "  \"version\": \"" << metadata.shm_name << "\",\n";
  json << "  \"shm_name\": \"" << metadata.shm_name << "\",\n";
  json << "  \"shm_size\": " << metadata.shm_size << ",\n";
  json << "  \"queue_capacity\": " << metadata.queue_capacity << ",\n";
  json << "  \"payload_size\": " << metadata.payload_size << ",\n";
  json << "  \"producer_pid\": " << metadata.producer_pid << ",\n";
  json << "  \"creation_timestamp\": " << metadata.creation_time.count() << ",\n";
  json << "  \"last_modify_timestamp\": " << metadata.modify_time.count() << "\n";
  json << "}\n";

  // Write to file atomically using temp file
  std::string temp_path = file_path + ".tmp.XXXXXX";
  int fd = mkstemp(const_cast<char*>(temp_path.c_str()));
  if (fd < 0) {
    return CoordinationResult::kIoError;
  }

  ssize_t written = write(fd, json.str().c_str(), json.str().size());
  close(fd);

  if (written != static_cast<ssize_t>(json.str().size())) {
    unlink(temp_path.c_str());
    return CoordinationResult::kIoError;
  }

  // Rename to final location (atomic)
  if (rename(temp_path.c_str(), file_path.c_str()) < 0) {
    unlink(temp_path.c_str());
    return CoordinationResult::kIoError;
  }

  return CoordinationResult::kSuccess;
}

CoordinationResult CoordinationFile::Read(
    const std::string& file_path,
    CoordinationMetadata& metadata) {

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return CoordinationResult::kFileNotFound;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  if (content.empty()) {
    return CoordinationResult::kParseError;
  }

  // Simple JSON parsing (avoiding external dependencies)
  // Format:
  // {
  //   "shm_name": "/phoenix_ipc_12345",
  //   "shm_size": 65536,
  //   "queue_capacity": 1024,
  //   "payload_size": 32768,
  //   "producer_pid": 12345,
  //   "creation_timestamp": 1704067200,
  //   "last_modify_timestamp": 1704067200
  // }

  // Parse shm_name
  size_t pos = content.find("\"shm_name\"");
  if (pos == std::string::npos) return CoordinationResult::kInvalidFormat;
  size_t colon = content.find(":", pos);
  size_t quote1 = content.find("\"", colon);
  size_t quote2 = content.find("\"", quote1 + 1);
  if (quote1 == std::string::npos || quote2 == std::string::npos) {
    return CoordinationResult::kParseError;
  }
  metadata.shm_name = content.substr(quote1 + 1, quote2 - quote1 - 1);

  // Parse shm_size
  pos = content.find("\"shm_size\"");
  if (pos == std::string::npos) return CoordinationResult::kInvalidFormat;
  colon = content.find(":", pos);
  size_t comma = content.find(",", colon);
  metadata.shm_size = std::stoull(content.substr(colon + 1, comma - colon - 1));

  // Parse queue_capacity
  pos = content.find("\"queue_capacity\"");
  if (pos == std::string::npos) return CoordinationResult::kInvalidFormat;
  colon = content.find(":", pos);
  comma = content.find(",", colon);
  metadata.queue_capacity = std::stoull(content.substr(colon + 1, comma - colon - 1));

  // Parse payload_size
  pos = content.find("\"payload_size\"");
  if (pos == std::string::npos) return CoordinationResult::kInvalidFormat;
  colon = content.find(":", pos);
  comma = content.find(",", colon);
  metadata.payload_size = std::stoull(content.substr(colon + 1, comma - colon - 1));

  // Parse producer_pid
  pos = content.find("\"producer_pid\"");
  if (pos == std::string::npos) return CoordinationResult::kInvalidFormat;
  colon = content.find(":", pos);
  comma = content.find(",", colon);
  metadata.producer_pid = static_cast<pid_t>(
      std::stoll(content.substr(colon + 1, comma - colon - 1)));

  // Parse creation_timestamp
  pos = content.find("\"creation_timestamp\"");
  if (pos == std::string::npos) return CoordinationResult::kInvalidFormat;
  colon = content.find(":", pos);
  comma = content.find(",", colon);
  metadata.creation_time = std::chrono::seconds(
      std::stoll(content.substr(colon + 1, comma - colon - 1)));

  // Parse last_modify_timestamp
  pos = content.find("\"last_modify_timestamp\"");
  if (pos == std::string::npos) return CoordinationResult::kInvalidFormat;
  colon = content.find(":", pos);
  size_t brace = content.find("}", colon);
  metadata.modify_time = std::chrono::seconds(
      std::stoll(content.substr(colon + 1, brace - colon - 1)));

  return CoordinationResult::kSuccess;
}

CoordinationResult CoordinationFile::Touch(const std::string& file_path) {
  struct stat st;
  if (stat(file_path.c_str(), &st) < 0) {
    return CoordinationResult::kFileNotFound;
  }

  // Update modification time
  if (utimensat(0, file_path.c_str(), nullptr, 0) < 0) {
    return CoordinationResult::kIoError;
  }

  return CoordinationResult::kSuccess;
}

CoordinationResult CoordinationFile::Delete(const std::string& file_path) {
  if (unlink(file_path.c_str()) < 0) {
    if (errno == ENOENT) {
      return CoordinationResult::kFileNotFound;
    }
    return CoordinationResult::kIoError;
  }
  return CoordinationResult::kSuccess;
}

bool CoordinationFile::Exists(const std::string& file_path,
                              CoordinationMetadata* metadata) {
  struct stat st;
  if (stat(file_path.c_str(), &st) < 0) {
    return false;
  }

  if (!S_ISREG(st.st_mode)) {
    return false;
  }

  if (metadata != nullptr) {
    CoordinationResult result = Read(file_path, *metadata);
    if (result != CoordinationResult::kSuccess) {
      return false;
    }
  }

  return true;
}

bool CoordinationFile::IsProducerAlive(const CoordinationMetadata& metadata) {
  if (metadata.producer_pid <= 0) {
    return false;
  }
  return kill(metadata.producer_pid, 0) == 0;
}

const char* CoordinationFile::ResultToString(CoordinationResult result) {
  switch (result) {
    case CoordinationResult::kSuccess:
      return "Success";
    case CoordinationResult::kFileNotFound:
      return "File not found";
    case CoordinationResult::kParseError:
      return "Failed to parse file";
    case CoordinationResult::kInvalidFormat:
      return "Invalid file format";
    case CoordinationResult::kVersionMismatch:
      return "Version mismatch";
    case CoordinationResult::kProducerGone:
      return "Producer process no longer exists";
    case CoordinationResult::kIoError:
      return "I/O error";
    default:
      return "Unknown error";
  }
}

std::string CoordinationFile::GenerateShmName(const std::string& base_name) {
  std::ostringstream oss;
  oss << "/" << base_name << "_" << getpid() << "_" << time(nullptr);
  return oss.str();
}

std::string CoordinationFile::GetDefaultCoordFile(const std::string& base_name) {
  const char* home = getenv("HOME");
  if (home == nullptr) {
    home = "/tmp";
  }

  std::ostringstream oss;
  oss << home << "/." << base_name << "_coord";
  return oss.str();
}

}  // namespace coordination
}  // namespace phoenix
