/* Test a linked-list implementation using RVM */

#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <string>
#include <iostream>
#include <assert.h>
#include <time.h>

#define NUM_NODES 200

class node {
public:
  node() { val = -1; prev = NULL; next = NULL; }
  ~node() { delete node_string; }

  void set_val(int newval) { val = newval; }
  int get_val(void) { return val; }
  void set_next(node* newval) { next = newval; }
  node* get_next(void) { return next; }
  void set_prev(node* newval) { prev = newval; }
  node* get_prev(void) { return prev; }
  void set_node_string(std::string name) { node_string = new std::string(name); }
  std::string get_node_string(void) { return *node_string; }

private:
  node *prev, *next;
  int val;
  std::string* node_string;
};

class linked_list {
public:
  linked_list()
  {
    rvm = rvm_init("linked_list");
    num_nodes = 0;
    root = create_new_node();
  }

  void print_nodes(void)
  {
    node *curr_node = root;

    while(curr_node->get_next() != NULL) {
      curr_node = curr_node->get_next();
      std::cout << curr_node->get_val() << " ";
    }

    std::cout << std::endl;
  }

  node *create_new_node(void)
  {
    num_nodes++;
    node *curr_node = (node *) rvm_map(rvm, get_node_string().c_str(), sizeof(node));
    curr_node->set_next(NULL);
    curr_node->set_prev(NULL);
    curr_node->set_node_string(get_node_string());

    return curr_node;
  }

  int get_num_nodes(void)
  {
    return num_nodes;
  }

  node *new_tail_node(void)
  {
    trans_t trans1, trans2;
    node *tail_node = get_tail_node();
    void *trans1_seg[1], *trans2_seg[1];

    trans1_seg[0] = tail_node;
    trans1 = rvm_begin_trans(rvm, 1, trans1_seg);

    rvm_about_to_modify(trans1, tail_node, 0, sizeof(node));
    tail_node->set_next(create_new_node());

    trans2_seg[0] = tail_node->get_next();
    trans2 = rvm_begin_trans(rvm, 1, trans2_seg);

    rvm_about_to_modify(trans2, tail_node->get_next(), 0, sizeof(node));
    tail_node->get_next()->set_prev(tail_node);
    tail_node->get_next()->set_next(NULL);

    rvm_commit_trans(trans2);
    rvm_commit_trans(trans1);

    return tail_node->get_next();
  }

  void unlink_node(trans_t trans, node *nodep)
  {
    assert(nodep != root);

    rvm_about_to_modify(trans, nodep->get_prev(), 0, sizeof(node));

    nodep->get_prev()->set_next(nodep->get_next());

    if (nodep->get_next() != NULL) {
      rvm_about_to_modify(trans, nodep->get_next(), 0, sizeof(node));
      nodep->get_next()->set_prev(nodep->get_prev());
    }

    num_nodes--;
  }

  void delete_node(node *nodep)
  {
    const char *seg_del = nodep->get_node_string().c_str();
    rvm_unmap(rvm, nodep); 
    rvm_destroy(rvm, seg_del);
  }

  void get_node_list(int val, int& num, node**& del_list)
  {
    num = 0;

    node *curr_node = root;

    while(curr_node->get_next() != NULL) {
      curr_node = curr_node->get_next();
      if (curr_node->get_val() > val) {
        num++;
        del_list = (node **)realloc(del_list, sizeof(node *) * num);
        del_list[num - 1] = curr_node;
      }
    }

    return;
  }

  trans_t prepare_deletion(int num, node **del_list)
  {
    void *full_del_list[num * 3];
    int total = 0;

    for (total = 0; total < num; total++) {
      full_del_list[total] = del_list[total];
    }

    // copy previous nodes
    for (int i = 0; i < num; i++) {
      full_del_list[total++] = del_list[i]->get_prev();
    }

    // copy next nodes if they are not NULL
    for (int i = 0; i < num; i++) {
      if (del_list[i]->get_next() != NULL)
        full_del_list[total++] = del_list[i]->get_next();
    }

    return rvm_begin_trans(rvm, total, (void **)full_del_list);
  }

  void complete_deletion(trans_t trans)
  {
    rvm_commit_trans(trans);
  }

  ~linked_list() {
    node *curr_node = root, *next_node;

    while (curr_node->get_next() != NULL) {
      next_node = curr_node->get_next();
      delete_node(curr_node);
      curr_node = next_node;
    }

    delete_node(curr_node);
  }

private:
  node *root;
  int num_nodes;
  rvm_t rvm;

  node *get_tail_node(void)
  {
    node *curr_node = root;

    while(curr_node->get_next() != NULL)
      curr_node = curr_node->get_next();

    return curr_node;
  }

  std::string get_node_string(void)
  {
    std::string node_string = std::string("NODE_") + std::to_string(num_nodes);
    return node_string;
  }
};

int main(int argc, char** argv) {
  linked_list *list = new linked_list();

  srand(time(NULL));

  std::cout << "Initial list size is " << list->get_num_nodes() << std::endl;

  std::cout << "Inserting " << NUM_NODES << " nodes" << std::endl;

  for (int i = 0; i < NUM_NODES; i++) {
    node *new_node = list->new_tail_node();
    new_node->set_val(rand() % 1000);
  }

  std::cout << "Now list size is " << list->get_num_nodes() << std::endl;
  //list->print_nodes();

  int del_val = rand() % 1000;
  std::cout << "Deleting all nodes whose val is greater than " << del_val << std::endl;

  int num;
  node **del_list = NULL;
  list->get_node_list(del_val, num, del_list);

  std::cout << "Found " << num << " nodes whose val is greater than "
            << del_val << std::endl;

  trans_t trans = list->prepare_deletion(num, del_list);
  for (int i = 0; i < num; i++)
    list->unlink_node(trans, del_list[i]);
  list->complete_deletion(trans);

  // free the nodes
  for (int i = 0; i < num; i++) {
    list->delete_node(del_list[i]);
  }

  std::cout << "Final list size is " << list->get_num_nodes() << std::endl;

  std::cout << "Cleaning up..." << std::endl;

  delete list;
  return 0;
}
