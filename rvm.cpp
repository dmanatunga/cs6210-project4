#include "rvm.h"
#include "rvm_internal.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cassert>

///////////////////////////////////////////////////////////////////////////////
// UndoRecord functions
///////////////////////////////////////////////////////////////////////////////
UndoRecord::UndoRecord(RvmSegment* segment, size_t offset, size_t size)
        : segment_(segment), offset_(offset), size_(size) {
  undo_copy_ = new char[size];
  memcpy(undo_copy_, &(segment_->get_base_ptr()[offset_]), size_ * sizeof(char));
}

UndoRecord::~UndoRecord() {
  delete[] undo_copy_;
}

void UndoRecord::Rollback() {
  memcpy(&(segment_->get_base_ptr()[offset_]), undo_copy_,  size_ * sizeof(char));
}

///////////////////////////////////////////////////////////////////////////////
// RedoRecord functions
///////////////////////////////////////////////////////////////////////////////
RedoRecord::RedoRecord(std::string segname, size_t offset, size_t size)
        : segment_name_(segname), offset_(offset), size_(size) {
  type_ = REDO_RECORD;
  data_ = new char[size_];
}

RedoRecord::RedoRecord(const UndoRecord* record)
        : segment_name_(record->get_segment_name()) {
  type_ = REDO_RECORD;
  size_ = record->get_size();
  offset_ = record->get_offset();
  data_ = new char[size_];
  memcpy(data_, record->get_segment_base_ptr() + record->get_offset(), size_);
}

RedoRecord::RedoRecord(RecordType type, std::string segname)
        : type_(type), segment_name_(segname) {
  type_ = type;
  size_ = 0;
  offset_ = 0;
  data_ = 0;
}

RedoRecord::~RedoRecord() {
  if (data_ != nullptr)
    delete[] data_;
}


///////////////////////////////////////////////////////////////////////////////
// RvmSegment functions
///////////////////////////////////////////////////////////////////////////////
RvmSegment::RvmSegment(Rvm* rvm, std::string segname, size_t segsize)
        : rvm_(rvm), name_(segname), size_(segsize), owned_by_(nullptr) {
  path_ = rvm_->construct_segment_path(segname);
  base_ = new char[size_];

  // Map the segment from the disk
  std::ifstream backing_file(path_, std::ifstream::binary);

  if (backing_file.good()) {
    // Backing file exists, so read it in
    backing_file.read(base_, segsize);
  }

  // Apply any changes stored in the redo log
  std::list<RedoRecord*> redo_records = rvm->GetRedoRecordsForSegment(this);
  // Go through redo records from oldest to newest
  // and apply redo records
  for (RedoRecord* record : redo_records) {
    size_t offset = record->get_offset();
    size_t copy_size = record->get_size();

    if ((offset + copy_size) <= size_) {
      // If redo record end occurs before end of segment
      memcpy(base_ + offset, record->get_data_ptr(), copy_size);
    } else if (offset < size_) {
      // If redo record end occurs after end of segment,
      // but redo record offset occurs before end of segment
      memcpy(base_ + offset, record->get_data_ptr(), size_ - offset);
    }

  }
}

RvmSegment::~RvmSegment() {
  delete[] base_;
}

///////////////////////////////////////////////////////////////////////////////
// RvmTransaction functions
///////////////////////////////////////////////////////////////////////////////
void RvmTransaction::AboutToModify(void* segbase, size_t offset, size_t size) {
  std::unordered_map<void*, RvmSegment*>::iterator iterator = base_to_segment_map_.find(segbase);
  if (iterator == base_to_segment_map_.end()) {
#if DEBUG
    std::cerr << "RvmTransaction::AboutToModify(): Invalid Segment Base" << std::endl;
#endif
    exit(1);
  }

  RvmSegment* segment = iterator->second;
  if (segment->get_size() < (offset + size)) {
#if DEBUG
    std::cerr << "RvmTransaction::AboutToModify(): offset and size outside of segment region" << std::endl;
#endif
    return;
  }

  UndoRecord* undo_record = new UndoRecord(segment, offset, size);
  undo_records_.push_back(undo_record);
}

void RvmTransaction::Commit() {
  std::list<RedoRecord*> redo_commits;
  for (UndoRecord* record : undo_records_) {
    redo_commits.push_back(new RedoRecord(record));
  }

  rvm_->WriteRecordsToLog(redo_commits);
  RemoveSegments();
}

void RvmTransaction::Abort() {
  while (!undo_records_.empty()) {
    UndoRecord* record = undo_records_.back();
    record->Rollback();
    undo_records_.pop_back();
    delete record;
  }
  RemoveSegments();
}

void RvmTransaction::AddSegment(RvmSegment* segment) {
  base_to_segment_map_[segment->get_base_ptr()] = segment;
  segment->set_owner(this);

}

void RvmTransaction::RemoveSegments() {
  for (auto const entry : base_to_segment_map_) {
    RvmSegment* segment = entry.second;
    assert(segment->get_owner() == this);
    segment->set_owner(nullptr);
  }
}


///////////////////////////////////////////////////////////////////////////////
// Rvm class functions
///////////////////////////////////////////////////////////////////////////////
Rvm::Rvm(const char* directory) : directory_(directory) {
  struct stat st;
  if (stat(directory_.c_str(), &st) == -1) {
    if (mkdir(directory_.c_str(), 0700) != 0) {
      std::cerr << "ERROR: Cannot create directory" << directory << std::endl;
      return;
    }
  }


  // Map the segment from the disk
  log_path_ = construct_log_path();
  if (file_exists(log_path_)) {
    std::ifstream log_file(log_path_, std::ifstream::binary);

    while (log_file.good()) {
      RedoRecord* record = ParseRedoRecord(log_file);
      if (record != nullptr) {
        committed_logs_.push_back(record);
      }
    }
  }
}

Rvm::~Rvm() {

}


void* Rvm::MapSegment(std::string segname, size_t segsize) {
  // Search for a segment with the given name
  std::unordered_map<std::string, RvmSegment*>::iterator segment = name_to_segment_map_.find(segname);
  if (segment == name_to_segment_map_.end()) {
    // Create RvmSegment from backing store
    RvmSegment* rvm_segment = new RvmSegment(this, segname, segsize);

    // Insert mappings for the segment
    name_to_segment_map_[rvm_segment->get_name()] = rvm_segment;
    base_to_segment_map_[rvm_segment->get_base_ptr()] = rvm_segment;
    return rvm_segment->get_base_ptr();
  } else {
    // Trying to re-map a segment that has already been mapped
#if DEBUG
    std::cout << "Rvm::MapSegment(): Segment " << segname << " already mapped." << std::endl;
#endif
    return (void*) -1;
  }
}

void Rvm::UnmapSegment(void* segbase) {
  // Search for a segment with the given base
  std::unordered_map<void*, RvmSegment*>::iterator iterator = base_to_segment_map_.find(segbase);
  if (iterator != base_to_segment_map_.end()) {
    RvmSegment* rvm_segment = iterator->second;

    assert(segbase == rvm_segment->get_base_ptr());

    // Erase the segment from the mapping structures
    base_to_segment_map_.erase(iterator);
    name_to_segment_map_.erase(rvm_segment->get_name());
    delete rvm_segment;
  } else {
    // Error: Segment does not exist
#if DEBUG
    std::cerr << "Rvm::UnmapSegment(): Segment " << segbase << " does not exist" << std::endl;
#endif
    return;
  }
}

void Rvm::DestroySegment(std::string segname) {
  // Search for a segment with the given name
  std::unordered_map<std::string, RvmSegment*>::iterator segment = name_to_segment_map_.find(segname);
  if (segment == name_to_segment_map_.end()) {
    RedoRecord* record = new RedoRecord(RedoRecord::DESTROY_SEGMENT, segname);
    WriteRecordToLog(record);

    std::string segpath = construct_segment_path(segname);
    if (file_exists(segpath)) {
      if (remove(segpath.c_str()) != 0) {
#if DEBUG
        std::cerr << "Rvm::DestroySegment(): Error deleting file" << std::endl;
#endif
      }
    }
    // TODO: Handle destroying any data in redo log
  } else {
#if DEBUG
    std::cout << "Rvm::DestroySegment(): Segment " << segname << " already mapped." << std::endl;
#endif
    // Trying to destroy a segment that is currently mapped
    return;
  }
}

trans_t Rvm::BeginTransaction(int numsegs, void** segbases) {
  // Check to see that input segment bases are valid
  for (int i = 0; i < numsegs; i++) {
    std::unordered_map<void*, RvmSegment*>::iterator iterator = base_to_segment_map_.find(segbases[i]);
    // Make sure segment exists
    if (iterator != base_to_segment_map_.end()) {
      RvmSegment* rvm_segment = iterator->second;
      // Check if segment is already owned by another transaction
      if (rvm_segment->has_owner()) {
#if DEBUG
        std::cerr << "Rvm::BeginTransaction(): Segment " << rvm_segment->get_name() << " being modified by another transaction" << std::endl;
#endif
        return (trans_t)-1;
      }
    } else {
#if DEBUG
      std::cerr << "Rvm::BeginTransaction(): Segment " << segbases[i] << " does not exist" << std::endl;
#endif
      return (trans_t)-1;
    }
  }

  // Create the transaction
  trans_t tid = g_trans_id++;
  RvmTransaction* rvm_trans = new RvmTransaction(tid, this);
  g_trans_list[tid] = rvm_trans;
  for (int i = 0; i < numsegs; i++) {
    RvmSegment* rvm_segment = base_to_segment_map_[segbases[i]];
    rvm_trans->AddSegment(rvm_segment);
  }
  return tid;
}

void Rvm::TruncateLog() {

}

void Rvm::WriteRecordsToLog(std::list<RedoRecord*> records) {
  std::ofstream log_file(log_path_, std::ofstream::out | std::ostream::binary | std::ostream::app);

  for (RedoRecord* record : records) {

    char type = record->get_type();
    switch (type) {
      case RedoRecord::REDO_RECORD: {
        // RedoRecord Format
        // <1-byte>: type
        // <size_t-bytes = N> : Length of segment name
        // <N-bytes> : Characters making up segment
        // <size_t-bytes> : Offset
        // <size_t-bytes = M> : Size of data
        // <M-bytes> : Characters making up data
        log_file.write(&type, 1);
        size_t str_len = record->get_segment_name().length();
        log_file.write((char*)&str_len, sizeof(size_t)); // Write length of string
        log_file.write(record->get_segment_name().c_str(), str_len); // Write string data

        // Write offset
        size_t offset = record->get_offset();
        log_file.write((char*)&offset, sizeof(size_t));

        // Write size and data
        size_t size = record->get_size();
        log_file.write((char*)&size, sizeof(size_t));
        log_file.write(record->get_data_ptr(), size);
        break;
      }
      case RedoRecord::DESTROY_SEGMENT: {
        log_file.write(&type, 1);
        size_t str_len = record->get_segment_name().length();
        log_file.write((char*)&str_len, sizeof(size_t)); // Write length of string
        log_file.write(record->get_segment_name().c_str(), str_len); // Write string data
        break;
      }
      default:
        break;
    }
  }
  log_file.flush();
}

void Rvm::WriteRecordToLog(RedoRecord* record) {
  std::ofstream log_file(log_path_, std::ofstream::out | std::ostream::binary | std::ostream::app);

  char type = record->get_type();
  switch (type) {
    case RedoRecord::REDO_RECORD: {
      // RedoRecord Format
      // <1-byte>: type
      // <size_t-bytes = N> : Length of segment name
      // <N-bytes> : Characters making up segment
      // <size_t-bytes> : Offset
      // <size_t-bytes = M> : Size of data
      // <M-bytes> : Characters making up data
      log_file.write(&type, 1);
      size_t str_len = record->get_segment_name().length();
      log_file.write((char*)&str_len, sizeof(size_t)); // Write length of string
      log_file.write(record->get_segment_name().c_str(), str_len); // Write string data

      // Write offset
      size_t offset = record->get_offset();
      log_file.write((char*)&offset, sizeof(size_t));

      // Write size and data
      size_t size = record->get_size();
      log_file.write((char*)&size, sizeof(size_t));
      log_file.write(record->get_data_ptr(), size);
      break;
    }
    case RedoRecord::DESTROY_SEGMENT: {
      log_file.write(&type, 1);
      size_t str_len = record->get_segment_name().length();
      log_file.write((char*)&str_len, sizeof(size_t)); // Write length of string
      log_file.write(record->get_segment_name().c_str(), str_len); // Write string data
      break;
    }
    default:
      break;
  }

  committed_logs_.push_back(record);
  log_file.flush();
}

RedoRecord* Rvm::ParseRedoRecord(std::ifstream& log_file) {
  // RedoRecord Format
  // <size_t-bytes = N> : Length of segment name
  // <N-bytes> : Characters making up segment
  // <size_t-bytes> : Offset
  // <size_t-bytes = M> : Size of data
  // <M-bytes> : Characters making up data

  // Redo-log file exists, so read it in
  char type;
  log_file.read(&type, 1);

  switch (type) {
    case RedoRecord::REDO_RECORD: {
      char len_buf[sizeof(size_t)]; // Buffer to hold name length, offset, and size

      // Read Name length
      log_file.read(len_buf, sizeof(size_t));
      size_t name_len = *((size_t*)len_buf);

      // Read name
      char* name_buf = new char[name_len + 1]; // +1 so that last byte will be string null-terminator
      log_file.read(name_buf, sizeof(char) * name_len);

      // Read offset
      log_file.read(len_buf, sizeof(size_t));
      size_t offset = *((size_t*)len_buf);

      // Read size
      log_file.read(len_buf, sizeof(size_t));
      size_t size = *((size_t*)len_buf);

      RedoRecord* record = new RedoRecord(std::string(name_buf), offset, size);
      log_file.read(record->get_data_ptr(), size);

      return record;
    }
    case RedoRecord::DESTROY_SEGMENT: {
      char len_buf[sizeof(size_t)]; // Buffer to hold name length, offset, and size

      // Read Name length
      log_file.read(len_buf, sizeof(size_t));
      size_t name_len = *((size_t*)len_buf);

      // Read name
      char* name_buf = new char[name_len + 1]; // +1 so that last byte will be string null-terminator
      log_file.read(name_buf, sizeof(char) * name_len);

      RedoRecord* record = new RedoRecord(RedoRecord::DESTROY_SEGMENT, std::string(name_buf));
      return record;
    }
    default:
      return nullptr;
  }
}

std::list<RedoRecord*> Rvm::GetRedoRecordsForSegment(RvmSegment* segment) {
  std::list<RedoRecord*> list;
  for (RedoRecord* record : committed_logs_) {
    if (record->get_segment_name() == segment->get_name()) {
      if (record->get_type() == RedoRecord::RecordType::DESTROY_SEGMENT) {
        list.clear();
      } else {
        list.push_back(record);
      }
    }
  }
  return list;
}


///////////////////////////////////////////////////////////////////////////////
// Library functions (passthrough calls)
///////////////////////////////////////////////////////////////////////////////
rvm_t rvm_init(const char* directory) {
  if (directory == NULL) {
    return nullptr;
  }

  Rvm* rvm  = new Rvm(directory);
  return rvm;
}

void* rvm_map(rvm_t rvm, const char* segname, int size_to_create) {
  std::string name(segname);
  if (name.empty()) {
#if DEBUG
    std::cout << "rvm_map(): Invalid segment name" << std::endl;
#endif
    return (void*) -1;
  }

  if (size_to_create <= 0) {
#if DEBUG
    std::cout << "rvmp_map(): Invalid size to create" << std::endl;
#endif
    return (void*) -1;
  }
  return rvm->MapSegment(name, (size_t) size_to_create);
}

void rvm_unmap(rvm_t rvm, void* segbase) {
  rvm->UnmapSegment(segbase);
}

void rvm_destroy(rvm_t rvm, const char* segname) {
  rvm->DestroySegment(std::string(segname));
}

trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void** segbases) {
  return rvm->BeginTransaction(numsegs, segbases);
}

void rvm_about_to_modify(trans_t tid, void* segbase, int offset, int size) {
  if (size <= 0) {
#if DEBUG
    std::cerr << "rvm_about_to_modify(): Size not positive " << size  << std::endl;
#endif
    exit(1);
  }

  if (offset < 0) {
#if DEBUG
    std::cerr << "rvm_about_to_modify(): Negative offset inputted " << offset  << std::endl;
#endif
    exit(1);
  }

  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_list.find(tid);
  if (iter != g_trans_list.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->AboutToModify(segbase, (size_t) offset, (size_t) size);
  } else {
#if DEBUG
    std::cerr << "rvm_about_to_modify(): Invalid Transaction " << tid << std::endl;
#endif
    exit(1);
  }
}

void rvm_commit_trans(trans_t tid) {
  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_list.find(tid);
  if (iter != g_trans_list.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->Commit();

    // Remove transaction from list and delete
    g_trans_list.erase(iter);
    delete rvm_trans;
  } else {
#if DEBUG
    std::cerr << "rvm_commit_trans(): Invalid Transaction " << tid << std::endl;
#endif
  }
}

void rvm_abort_trans(trans_t tid) {
  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_list.find(tid);
  if (iter != g_trans_list.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->Abort();

    // Remove transaction from list and delete
    g_trans_list.erase(iter);
    delete rvm_trans;
  } else {
#if DEBUG
    std::cerr << "rvm_abort_trans(): Invalid Transaction " << tid << std::endl;
#endif
  }
}

void rvm_truncate_log(rvm_t rvm) {
  rvm->TruncateLog();
}


