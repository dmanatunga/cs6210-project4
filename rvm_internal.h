#ifndef RVM_INTERNAL_H
#define RVM_INTERNAL_H

#include <vector>
#include <list>
#include <unordered_map>

#define DEBUG 1
#if !DEBUG
  #define NDEBUG
#endif

class Rvm;
typedef Rvm* rvm_t;
typedef int trans_t;

std::unordered_map<trans_t, RvmTransaction*> g_trans_list;
static trans_t g_trans_id = 0;

class RvmSegment {
 public:
  RvmSegment(Rvm* rvm, std::string segname, size_t segsize);
  ~RvmSegment();

  const std::string& get_name() const {
    return name_;
  }

  const std::string& get_path() const {
    return path_;
  }

  char* get_base_ptr() {
    return base_;
  }

  size_t get_size() const {
    return size_;
  }

  void set_owner(RvmTransaction* owner) {
    owned_by_ = owner;
  }

  bool has_owner() const {
    return owned_by_ != nullptr;
  }

 private:
  Rvm* rvm_;
  std::string name_;
  std::string path_;
  char* base_;
  size_t size_;
  RvmTransaction* owned_by_;
};

class UndoRecord {
 public:
  UndoRecord(RvmSegment* segment, size_t offset, size_t size);
  ~UndoRecord();

  void Rollback();
  size_t get_offset() const {
    return offset_;
  }

  size_t get_size() const {
    return size_;
  }

  const char* get_segment_base_ptr() const {
    return segment_->get_base_ptr();
  }

  const std::string& get_segment_name() const {
    return segment_->get_name();
  }

 private:
  RvmSegment* segment_;
  size_t offset_;
  size_t size_;
  char* undo_copy_;
};


class RedoRecord {

 public:
  RedoRecord(std::string segname, size_t offset, size_t size);
  RedoRecord(const UndoRecord* record);
  ~RedoRecord();

  const std::string& get_segment_name() const {
    return segment_name_;
  }

  size_t get_offset() const {
    return offset_;
  }

  size_t get_size() const {
    return size_;
  }

  char* get_data_ptr() const {
    return data_;
  }

 private:
  std::string segment_name_;
  size_t offset_;
  size_t size_;
  char* data_;
};

class RvmTransaction {
 public:
  RvmTransaction(trans_t tid, Rvm* rvm) : id_(tid), rvm_(rvm) {};

  void AboutToModify(void* segbase, size_t offset, size_t size);
  void Commit();
  void Abort();
  void AddSegment(RvmSegment* segment);

 private:
  trans_t id_;
  Rvm* rvm_;
  std::unordered_map<void*, RvmSegment*> base_to_segment_map_;
  std::list<UndoRecord*> undo_records_;
};

class Rvm {
 public:
  Rvm(const char* directory);
  ~Rvm();

  void* MapSegment(std::string segname, size_t segsize);
  void UnmapSegment(void* segbase);
  void DestroySegment(std::string segname);
  trans_t BeginTransaction(int numsegs, void** segbases);
  void TruncateLog();
  void ApplyRedoLog(RvmSegment* segment);
  inline std::string construct_segment_path(std::string segname) {
    return directory_ + "seg_" + segname + ".rvm";
  }

 private:
  std::string directory_;
  std::string log_path_;
  std::unordered_map<std::string, RvmSegment*> name_to_segment_map_;
  std::unordered_map<void*, RvmSegment*> base_to_segment_map_;
  std::vector<RedoRecord*> committed_logs_;

  inline std::string construct_log_path() {
    return directory_ + "redo_log.rvm";
  }
};

#endif // RVM_INTERNAL_H