#ifndef RVM_INTERNAL_H
#define RVM_INTERNAL_H

#include <vector>
#include <unordered_map>

class Rvm;
typedef Rvm* rvm_t;
typedef int trans_t;

std::unordered_map<trans_t, RvmTransaction*> g_trans_list;
static trans_t g_trans_id = 0;

class RvmSegment {
  friend class Rvm;
  friend class RvmTransaction;

 public:
  RvmSegment(std::string segname, std::string segpath, void* segbase, size_t segsize)
          : name_(segname), path_(segpath), base_(segbase), size_(segsize), owned_by_(nullptr) {};
  ~RvmSegment() {};

  RvmTransaction* GetOwner() {
    return owned_by_;
  }

  void SetOwner(RvmTransaction* owner) {
    owned_by_ = owner;
  }

  bool HasOwner() {
    return owned_by_ != nullptr;
  }

 private:
  std::string name_;
  std::string path_;
  void* base_;
  size_t size_;
  RvmTransaction* owned_by_;
};

class Rvm {
 public:
  Rvm(const char* directory) : directory_(directory) {};
  ~Rvm() {};

  void* MapSegment(std::string segname, size_t size_to_create);
  void UnmapSgement(void* segbase);
  void DestroySegment(std::string segname);

  trans_t BeginTransaction(int numsegs, void** segbases);
  void TruncateLog();
 private:
  std::string directory_;
  std::unordered_map<std::string, RvmSegment*> name_to_segment_map_;
  std::unordered_map<void*, RvmSegment*> base_to_segment_map_;

 private:
  std::string construct_segment_path(std::string segname) {
    return directory_ + "seg_" + segname;
  }
};

class RvmTransaction {
 public:
  RvmTransaction(trans_t tid, Rvm* rvm) : id_(tid), rvm_(rvm) {};

  void AboutToModify(void* segbase, int offset, int size);
  void Commit();
  void Abort();
  void AddSegment(RvmSegment* segment);

 private:
  trans_t id_;
  Rvm* rvm_;
  std::unordered_map<void*, RvmSegment*> base_to_segment_map_;
};






#endif // RVM_INTERNAL_H