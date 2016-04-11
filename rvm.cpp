#include "rvm.h"

rvm_t rvm_init(const char* directory) {
  return nullptr;
}

void* rvm_map(rvm_t rvm, const char* segname, int size_to_create) {
  // Check to see if the segment has already been mapped
  std::unordered_map<char*, void*>::iterator segment = rvm->segment_map.find(segname);
  if (segment == rvm->segment_map.end()) {

  } else {
    // Trying to re-map a segment that has already been mapped
    return (void*) -1;
  }

  return nullptr;
}

void rvm_unmap(rvm_t rvm, void* segbase) {

}

void rvm_destroy(rvm_t rvm, const char* segname) {
  // Check to see if the segment is mapped
  std::unordered_map<char*, void*>::iterator segment = rvm->segment_map.find(segname);
  if (segment == rvm->segment_map.end()) {

  } else {
    // Trying to destroy a segment that is currently mapped
    return;
  }
}

trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void** segbases) {
  for (int i = 0; i < numsegs; i++) {
    // Check if a segment is already being modified by another transaction
  }
  return 0;
}

void rvm_about_to_modify(trans_t tid, void* segbase, int offset, int size) {

}

void rvm_commit_trans(trans_t tid) {

}

void rvm_abort_trans(trans_t tid) {

}

void rvm_truncate_log(rvm_t rvm) {

}
