#ifndef RVM_INTERNAL_H
#define RVM_INTERNAL_H

#include <vector>
#include <unordered_map>

struct rvm {
  char* directory;
  std::unordered_map<char*, void*> segment_map;
};

typedef struct rvm* rvm_t;
typedef int trans_t;


#endif // RVM_INTERNAL_H