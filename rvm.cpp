#include "rvm.h"
#include <stdio.h>
#include <fstream>

void* Rvm::MapSegment(std::string segname, size_t size_to_create) {
  std::unordered_map<std::string, RvmSegment*>::iterator segment = name_to_segment_map_.find(segname);
  // Check to see if the segment has already been mapped
  if (segment == name_to_segment_map_.end()) {
    // Map the segment from the disk
    std::string segpath = construct_segment_path(segname);
    std::ifstream segbacking(segpath);
    void* segbase;
    size_t segsize = size_to_create;
    if (segbacking.good()) {
      // Backing file exists, so read it in
      segbase = nullptr;
    } else {
      segbase = malloc(segsize);
      if (segbase == NULL) {
        perror("Malloc Failed");
        exit(-1);
      }
    }

    // Create the segment and insert mappings for segment
    RvmSegment* rvm_segment = new RvmSegment(segname, segpath, segbase, segsize);
    name_to_segment_map_[segname] = rvm_segment;
    base_to_segment_map_[segbase] = rvm_segment;
    return segbase;
  } else {
    // Trying to re-map a segment that has already been mapped
#if DEBUG
    std::cout << "Segment " << segname << " already mapped." << std::endl;
#endif
    return (void*) -1;
  }
}

void Rvm::UnmapSgement(void* segbase) {
  std::unordered_map<void*, RvmSegment*>::iterator segment = base_to_segment_map_.find(segbase);
  if (segment == base_to_segment_map_.end()) {
    // Error: Segment does not exist
#if DEBUG
    std::cerr << "Error: Invalid Segment Base (" << segbase << ")" << std::endl;
#endif
    return;
  } else {
    RvmSegment* rvm_segment = segment->second;

#if DEBUG
    assert(segbase == rvm_segment->base_);
#endif

    free(rvm_segment->base_);
    name_to_segment_map_.erase(rvm_segment->name_);
    base_to_segment_map_.erase(rvm_segment->base_);
    delete rvm_segment;
  }
}

void Rvm::DestroySegment(std::string segname) {
  // Check to see if the segment is mapped
  std::unordered_map<std::string, RvmSegment*>::iterator segment = name_to_segment_map_.find(segname);
  if (segment == name_to_segment_map_.end()) {
    std::string segpath = construct_segment_path(segname);
    if (remove(segpath.c_str()) != 0) {
#if DEBUG
      perror("Error deleting file");
#endif
    }
  } else {
#if DEBUG
    std::cout << "Cannot Destroy: Segment " <<< segname << " already mapped." << std::endl;
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
      if (rvm_segment->HasOwner()) {
        return (trans_t)-1;
      }
    } else {
      return (trans_t)-1;
    }
  }

  // Create the transaction
  trans_t tid = g_trans_id++;
  RvmTransaction* rvm_trans = new RvmTransaction(tid, this);
  g_trans_list[tid] = rvm_trans;
  for (int i = 0; i < numsegs; i++) {
    rvm_trans->AddSegment(base_to_segment_map_[segbases[i]]);
  }
  return tid;
}

void Rvm::TruncateLog() {

}

void RvmTransaction::AboutToModify(void* segbase, int offset, int size) {

}

void RvmTransaction::Commit() {

}

void RvmTransaction::Abort() {

}

void RvmTransaction::AddSegment(RvmSegment* segment) {
  base_to_segment_map_[segment->base_] = segment;
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
  if (size_to_create <= 0) {
    return (void*) -1;
  }
  return rvm->MapSegment(std::string(segname), (size_t) size_to_create);
}

void rvm_unmap(rvm_t rvm, void* segbase) {
  rvm->UnmapSgement(segbase);
}

void rvm_destroy(rvm_t rvm, const char* segname) {
  rvm->DestroySegment(std::string(segname));
}

trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void** segbases) {
  return rvm->BeginTransaction(numsegs, segbases);
}

void rvm_about_to_modify(trans_t tid, void* segbase, int offset, int size) {
  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_list.find(tid);
  if (iter != g_trans_list.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->AboutToModify(segbase, offset, size);
  } else {
#if DEBUG
    std::cout << "about_to_modify(): Inputted Invalid Transaction " << tid << std::endl;
#endif
  }
}

void rvm_commit_trans(trans_t tid) {
  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_list.find(tid);
  if (iter != g_trans_list.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->Commit();
  } else {
#if DEBUG
    std::cout << "Committing Invalid Transaction " << tid << std::endl;
#endif
  }
}

void rvm_abort_trans(trans_t tid) {
  std::unordered_map<trans_t, RvmTransaction*>::iterator iter = g_trans_list.find(tid);
  if (iter != g_trans_list.end()) {
    RvmTransaction* rvm_trans = iter->second;
    rvm_trans->Abort();
  } else {
#if DEBUG
    std::cout << "Aborting Invalid Transaction " << tid << std::endl;
#endif
  }
}

void rvm_truncate_log(rvm_t rvm) {
  rvm->TruncateLog();
}






