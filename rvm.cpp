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
  undo_copy_ = new char[size]();
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
  data_ = new char[size_]();
}

RedoRecord::RedoRecord(const UndoRecord* record)
        : segment_name_(record->get_segment_name()) {
  type_ = REDO_RECORD;
  size_ = record->get_size();
  offset_ = record->get_offset();
  data_ = new char[size_]();
  memcpy(data_, record->get_segment_base_ptr() + record->get_offset(), size_);
}

RedoRecord::RedoRecord(RecordType type, std::string segname)
        : type_(type), segment_name_(segname) {
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
  base_ = new char[size_]();

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
RvmTransaction::~RvmTransaction() {
  for (UndoRecord* record : undo_records_) {
    delete record;
  }
  undo_records_.clear();

  for (RedoRecord* record : redo_records_) {
    delete record;
  }
  redo_records_.clear();
}

void RvmTransaction::AboutToModify(void* segbase, size_t offset, size_t size) {
  std::unordered_map<void*, RvmSegment*>::iterator iterator = base_to_segment_map_.find(segbase);
  if (iterator == base_to_segment_map_.end()) {
#if DEBUG
    std::cerr << "RvmTransaction::AboutToModify(): Invalid Segment Base" << std::endl;
#endif
    exit(EXIT_FAILURE);
  }

  RvmSegment* segment = iterator->second;
  if (segment->get_size() < (offset + size)) {
#if DEBUG
    std::cerr << "RvmTransaction::AboutToModify(): offset and size outside of segment region" << std::endl;
#endif
    exit(EXIT_FAILURE);
  }

  for (UndoRecord* record : undo_records_) {
    if ((record->get_segment_base_ptr() == (const char*) segbase) &&
        (record->get_offset() == offset) &&
        (record->get_size() == size)) {
      // If we find a matching UndoRecord already, then no need to
      // create another undo record
      return;
    }
  }

  UndoRecord* undo_record = new UndoRecord(segment, offset, size);
  undo_records_.push_back(undo_record);
}

void RvmTransaction::Commit() {
  // Create a redo record for each undo record and delete
  // now unneeded undo record
  for (UndoRecord* record : undo_records_) {
    redo_records_.push_back(new RedoRecord(record));
    delete record;
  }
  undo_records_.clear();
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
Rvm::Rvm(std::string directory) : directory_(directory) {
  struct stat st;
  if (stat(directory_.c_str(), &st) == -1) {

    mkdir(directory_.c_str(), 0700);
  }

  // Map the segment from the disk
  log_path_ = construct_log_path();
  tmp_log_path_ = construct_tmp_path(log_path_);

  if (!file_exists(log_path_) && file_exists(tmp_log_path_)) {
    // If log file doesn't exist, but tmp log file does, then move
    // tmp file over to log file
    std::rename(tmp_log_path_.c_str(), log_path_.c_str());
  }

  if (file_exists(log_path_)) {
    std::ifstream log_file;
    log_file.open(log_path_, std::ifstream::binary);

    // Parse the log file
    log_file.seekg(0, log_file.end);
    long file_size = log_file.tellg();
    log_file.seekg(0, log_file.beg);
    while (log_file.good()) {

      if (log_file.tellg() == file_size) {
        // Reached end of file
        break;
      }

      RvmTransaction* rvm_trans = ParseTransaction(log_file);
      if (rvm_trans != nullptr) {
        committed_transactions_.push_back(rvm_trans);
      } else {
        // Failure in parsing log file, re-write the log file
        // with only transactions that were parsed correctly
        log_file.close();

        std::ofstream::openmode flags = std::ofstream::out | std::ofstream::binary | std::ofstream::trunc;
        std::ofstream tmp_log_file(tmp_log_path_, flags);
        for (RvmTransaction* committed_rvm_trans : committed_transactions_) {
          WriteTransactionToLog(tmp_log_file, committed_rvm_trans);
        }
        tmp_log_file.flush();

        // Make the temporary log file as the new log file
        std::remove(log_path_.c_str());
        std::rename(tmp_log_path_.c_str(), log_path_.c_str());
        return;
      }
    }
    log_file.close();
  }
}

Rvm::~Rvm() {
  for (RvmTransaction* rvm_trans : committed_transactions_) {
    delete rvm_trans;
  }
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

    if (rvm_segment->has_owner()) {
#if DEBUG
      std::cerr << "Rvm::UnmapSegment(): Segment " << segbase << " being used by Transaction " << rvm_segment->get_owner()->get_id() << std::endl;
#endif
      exit(EXIT_FAILURE);
    }

    // Erase the segment from the mapping structures
    base_to_segment_map_.erase(iterator);
    name_to_segment_map_.erase(rvm_segment->get_name());
    delete rvm_segment;
  } else {
    // Error: Segment does not exist
#if DEBUG
    std::cerr << "Rvm::UnmapSegment(): Segment " << segbase << " does not exist" << std::endl;
#endif
    exit(EXIT_FAILURE);
  }
}

void Rvm::DestroySegment(std::string segname) {
  // Search for a segment with the given name
  std::unordered_map<std::string, RvmSegment*>::iterator segment = name_to_segment_map_.find(segname);
  if (segment == name_to_segment_map_.end()) {
    // Create a one-off transaction that indicates the segment was destroyed
    trans_t tid = get_next_transaction_id();
    // Write to redo log that the segment was destroyed
    RedoRecord* record = new RedoRecord(RedoRecord::DESTROY_SEGMENT, segname);
    std::list<RedoRecord*> records;
    records.push_back(record);
    RvmTransaction* rvm_trans = new RvmTransaction(tid, this, records);
    CommitTransaction(rvm_trans); // Commit the transaction

    std::string segpath = construct_segment_path(segname);
    if (file_exists(segpath)) {
      // Delete the file if it exists
      if (remove(segpath.c_str()) != 0) {
#if DEBUG
        std::cerr << "Rvm::DestroySegment(): Error deleting file" << std::endl;
#endif
      }
    }
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
  trans_t tid = get_next_transaction_id();
  RvmTransaction* rvm_trans = new RvmTransaction(tid, this);
  g_trans_map[tid] = rvm_trans;
  for (int i = 0; i < numsegs; i++) {
    RvmSegment* rvm_segment = base_to_segment_map_[segbases[i]];
    rvm_trans->AddSegment(rvm_segment);
  }
  return tid;
}

void Rvm::CommitTransaction(RvmTransaction* rvm_trans) {
  rvm_trans->Commit(); // Commit the rvm_trans
  if (!rvm_trans->get_redo_records().empty()) {
    // Write transactions to file if it has anything to commit
    std::ofstream::openmode flags = std::ofstream::out | std::ofstream::binary | std::ofstream::app;
    std::ofstream log_file(log_path_, flags);
    WriteTransactionToLog(log_file, rvm_trans);
    log_file.flush();
    // Add rvm_trans to list of committed transactions
    committed_transactions_.push_back(rvm_trans);
  }

  // Remove rvm_trans from global map
  g_trans_map.erase(rvm_trans->get_id());
}

void Rvm::AbortTransaction(RvmTransaction* rvm_trans) {
  rvm_trans->Abort();
  // Remove transaction from list and delete
  g_trans_map.erase(rvm_trans->get_id());
  delete rvm_trans;
}

void Rvm::TruncateLog() {
  std::unordered_map<std::string, std::list<RedoRecord*>> commit_map;

  std::list<RedoRecord*> unbacked_records;
  for (RvmTransaction* rvm_trans : committed_transactions_) {
    // Loop through logs and separate based on which backing file
    // the logs apply to
    for (RedoRecord* record : rvm_trans->get_redo_records()) {
      if (record->get_type() == RedoRecord::RecordType::DESTROY_SEGMENT) {
        // If it is a delete record, then clear current list
        commit_map[record->get_segment_name()].clear();
      } else {
        // Regular record, so push back to commit list
        commit_map[record->get_segment_name()].push_back(record);
      }
    }
    // Clear the list of redo records
    rvm_trans->clear_redo_records();
    delete rvm_trans;
  }
  committed_transactions_.clear();

  // Loop through map and commit logs to backing file
  for (auto& pair : commit_map) {
    bool success = ApplyRecordsToBackingFile(pair.first, pair.second);
    if (!success) {
      // Logs not successfully applied, so save them
      for (RedoRecord* record : pair.second) {
         unbacked_records.push_back(record);
      }
    } else {
      // Successfully applied records, so delete them
      for (RedoRecord* record : pair.second) {
        delete record;
      }
    }
  }

  std::ofstream::openmode flags = std::ofstream::out | std::ofstream::binary | std::ofstream::trunc;
  std::ofstream log_file(tmp_log_path_, flags);
  if (!unbacked_records.empty()) {
    RvmTransaction* rvm_trans = new RvmTransaction(get_next_transaction_id(), this, unbacked_records);
    WriteTransactionToLog(log_file, rvm_trans);
    log_file.flush();
    committed_transactions_.push_back(rvm_trans);
  }

  // Make the temporary log file as the new log file
  std::remove(log_path_.c_str());
  std::rename(tmp_log_path_.c_str(), log_path_.c_str());
}

std::list<RedoRecord*> Rvm::GetRedoRecordsForSegment(RvmSegment* segment) {
  std::list<RedoRecord*> list;
  for (RvmTransaction* rvm_trans : committed_transactions_) {
    for (RedoRecord* record : rvm_trans->get_redo_records()) {
      if (record->get_segment_name() == segment->get_name()) {
        if (record->get_type() == RedoRecord::RecordType::DESTROY_SEGMENT) {
          list.clear();
        } else {
          list.push_back(record);
        }
      }
    }
  }
  return list;
}


RvmTransaction* Rvm::ParseTransaction(std::ifstream& log_file) {
  trans_t trans_id;
  size_t num_records;
  log_file.read((char*)&trans_id, sizeof(trans_t));
  log_file.read((char*)&num_records, sizeof(size_t));

  if (!log_file.good()) {
#if DEBUG
    std::cout << "Rvm::ParseTransaction(): Transaction parse failed" << std::endl;
#endif
    return nullptr;
  }

  std::list<RedoRecord*> records;
  for (size_t i = 0; i < num_records; i++) {
    RedoRecord* record = ParseRedoRecord(log_file);
    if (record == nullptr) {
      // If error occurred during parsing, delete
      // any created records and return null ptr
      for (RedoRecord* redo_record : records) {
        delete redo_record;
      }
      return nullptr;
    }
    records.push_back(record);
  }

  trans_t tmp_id;
  size_t tmp_num_records;
  log_file.read((char*)&tmp_num_records, sizeof(size_t));
  log_file.read((char*)&tmp_id, sizeof(trans_t));

  if (!log_file.good()) {
#if DEBUG
    std::cout << "Rvm::ParseTransaction(): Transaction parse failed" << std::endl;
#endif
    // If error occurred during parsing, delete
    // any created records and return null ptr
    for (RedoRecord* redo_record : records) {
      delete redo_record;
    }
    return nullptr;
  }

  if ((trans_id == tmp_id) && (tmp_num_records == num_records)
      && (num_records == records.size())) {
    RvmTransaction* rvm_trans = new RvmTransaction(trans_id, this, records);
    return rvm_trans;
  }

#if DEBUG
  std::cout << "Rvm::ParseTransaction(): Transaction check failed" << std::endl;
#endif

  // If error occurred during parsing, delete
  // any created records and return null ptr
  for (RedoRecord* redo_record : records) {
    delete redo_record;
  }
  return nullptr;
}


RedoRecord* Rvm::ParseRedoRecord(std::ifstream& log_file) {
  // RedoRecord Format

  // <size_t-bytes = N> : Length of segment name
  // <N-bytes> : Characters making up segment
  // <size_t-bytes> : Offset
  // <size_t-bytes = M> : Size of data
  // <M-bytes> : Characters making up data

  // Redo-log file exists, so read it in
  int type;
  log_file.read((char*)&type, sizeof(int));
  if (!log_file.good()) {
#if DEBUG
    std::cout << "Rvm::ParseRedoRecord(): Parse record failed" << std::endl;
#endif
    return nullptr;
  }

  switch (type) {
    case RedoRecord::REDO_RECORD: {
      char len_buf[sizeof(size_t)]; // Buffer to hold name length, offset, and size

      // Read Name length
      log_file.read(len_buf, sizeof(size_t));
      size_t name_len = *((size_t*)len_buf);
      if (!log_file.good()) {
#if DEBUG
        std::cout << "Rvm::ParseRedoRecord(): Parse record failed" << std::endl;
#endif
        return nullptr;
      }


      // Read name
      char* name_buf = new char[name_len + 1]; // +1 so that last byte will be string null-terminator
      name_buf[name_len] = 0;
      log_file.read(name_buf, sizeof(char) * name_len);
      if (!log_file.good()) {
#if DEBUG
        std::cout << "Rvm::ParseRedoRecord(): Parse record failed" << std::endl;
#endif
        delete[] name_buf;
        return nullptr;
      }


      // Read offset
      log_file.read(len_buf, sizeof(size_t));
      size_t offset = *((size_t*)len_buf);

      // Read size
      log_file.read(len_buf, sizeof(size_t));
      size_t size = *((size_t*)len_buf);

      if (!log_file.good()) {
#if DEBUG
        std::cout << "Rvm::ParseRedoRecord(): Parse record failed" << std::endl;
#endif
        delete[] name_buf;
        return nullptr;
      }

      RedoRecord* record = new RedoRecord(std::string(name_buf), offset, size);
      log_file.read(record->get_data_ptr(), size);

      if (!log_file.good()) {
#if DEBUG
        std::cout << "Rvm::ParseRedoRecord(): Parse record failed" << std::endl;
#endif
        delete[] name_buf;
        delete record;
        return nullptr;
      }

      delete[] name_buf;

      return record;
    }
    case RedoRecord::DESTROY_SEGMENT: {
      char len_buf[sizeof(size_t)]; // Buffer to hold name length, offset, and size

      // Read Name length
      log_file.read(len_buf, sizeof(size_t));
      size_t name_len = *((size_t*)len_buf);
      if (!log_file.good()) {
#if DEBUG
        std::cout << "Rvm::ParseRedoRecord(): Parse record failed" << std::endl;
#endif
        return nullptr;
      }

      // Read name
      char* name_buf = new char[name_len + 1](); // +1 so that last byte will be string null-terminator
      name_buf[name_len] = 0;
      log_file.read(name_buf, sizeof(char) * name_len);
      if (!log_file.good()) {
#if DEBUG
        std::cout << "Rvm::ParseRedoRecord(): Parse record failed" << std::endl;
#endif
        delete[] name_buf;
        return nullptr;
      }

      RedoRecord* record = new RedoRecord(RedoRecord::DESTROY_SEGMENT, std::string(name_buf));
      delete[] name_buf;
      return record;
    }
    default: {
#if DEBUG
      std::cerr << "Rvm::ParseRedoRecord: Invalid Type " << type << std::endl;
#endif
      return nullptr;

    }
  }
}

void Rvm::WriteTransactionToLog(std::ofstream& log_file, RvmTransaction* rvm_trans) {
  trans_t trans_id = rvm_trans->get_id();
  size_t num_records = rvm_trans->get_redo_records().size();
  log_file.write((char*) &trans_id, sizeof(trans_t));
  log_file.write((char*) &num_records, sizeof(size_t));

  WriteRecordsToLog(log_file, rvm_trans->get_redo_records());

  log_file.write((char*) &num_records, sizeof(size_t));
  log_file.write((char*) &trans_id, sizeof(trans_t));
}

void Rvm::WriteRecordsToLog(std::ofstream& log_file, const std::list<RedoRecord*>& records) {
  for (RedoRecord* record : records) {
    int type = record->get_type();
    log_file.write((char*)&type, sizeof(int));
    switch (type) {
      case RedoRecord::REDO_RECORD: {
        // RedoRecord Format
        // <4-byte>: type
        // <size_t-bytes = N> : Length of segment name
        // <N-bytes> : Characters making up segment
        // <size_t-bytes> : Offset
        // <size_t-bytes = M> : Size of data
        // <M-bytes> : Characters making up data

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
        size_t str_len = record->get_segment_name().length();
        log_file.write((char*)&str_len, sizeof(size_t)); // Write length of string
        log_file.write(record->get_segment_name().c_str(), str_len); // Write string data
        break;
      }
      default: {
#if DEBUG
        std::cerr << "Rvm::WriteRecordsToLog: Invalid log type " << type << std::endl;
#endif
        break;
      }
    }
  }
}


bool Rvm::ApplyRecordsToBackingFile(const std::string& segname,
                                    const std::list<RedoRecord*>& records) {

  std::ofstream backing_file(construct_segment_path(segname), std::ofstream::out | std::ofstream::ate);

  for (RedoRecord* record : records) {
    backing_file.seekp(0, backing_file.end);
    size_t file_size = (size_t)backing_file.tellp();
    assert(record->get_type()  == RedoRecord::RecordType::REDO_RECORD);
    if (file_size < record->get_offset()) {
      // If the file is smaller than the offset, than pad the file
      // with zeros till the offset
      char* pad = new char[record->get_offset() - file_size]();
      backing_file.write(pad, record->get_offset() - file_size);
      delete[] pad;
    } else {
      // Move to offset position and write data
      backing_file.seekp(record->get_offset());
    }
    backing_file.write(record->get_data_ptr(), record->get_size());
    if (!backing_file.good()) {
#if DEBUG
      std::cout << "Rvm::ApplyRecordsToBackingFile(): Error applying changes to backnig file" << std::endl;
#endif
      return false;
    }
  }
  backing_file.flush();
  return true;
}


///////////////////////////////////////////////////////////////////////////////
// Library functions (passthrough calls)
///////////////////////////////////////////////////////////////////////////////
rvm_t rvm_init(const char* directory) {
  if (directory == NULL) {
    return nullptr;
  }

  // Check if rvm instance for directory has already been made
  std::string dir(directory);
  std::unordered_map<std::string, Rvm*>::iterator it = g_rvm_instances.find(dir);
  if (it == g_rvm_instances.end()) {
    // Create new instance
    Rvm* rvm  = new Rvm(dir);
    g_rvm_instances[dir] = rvm;
    return rvm;
  } else {
    // Return existing instance
    return it->second;
  }

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
    std::cerr << "rvm_about_to_modify(): Negative size inputted" << size  << std::endl;
#endif
    exit(EXIT_FAILURE);
  }

  if (offset < 0) {
#if DEBUG
    std::cerr << "rvm_about_to_modify(): Negative offset inputted" << size  << std::endl;
#endif
    exit(EXIT_FAILURE);
  }

  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_map.find(tid);
  if (iter != g_trans_map.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->AboutToModify(segbase, (size_t) offset, (size_t) size);
  } else {
#if DEBUG
    std::cerr << "rvm_about_to_modify(): Invalid Transaction " << tid << std::endl;
#endif
    exit(EXIT_FAILURE);
  }
}

void rvm_commit_trans(trans_t tid) {
  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_map.find(tid);
  if (iter != g_trans_map.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->get_rvm()->CommitTransaction(rvm_trans);
  } else {
#if DEBUG
    std::cerr << "rvm_commit_trans(): Invalid Transaction " << tid << std::endl;
#endif
    exit(EXIT_FAILURE);
  }
}

void rvm_abort_trans(trans_t tid) {
  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_map.find(tid);
  if (iter != g_trans_map.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->get_rvm()->AbortTransaction(rvm_trans);
  } else {
#if DEBUG
    std::cerr << "rvm_abort_trans(): Invalid Transaction " << tid << std::endl;
#endif
    exit(EXIT_FAILURE);
  }
}

void rvm_truncate_log(rvm_t rvm) {
  rvm->TruncateLog();
}
