# cs6210-project4
Authors: Pranith Kumar, Dilan Manatunga

This project contains a library implementation of Recoverable Virtual Memory.

## Project Structure
- rvm.h
  - Header containing interface functions
- rvm_internal.h
  - Header for internal RVM implementation
- rvm.cpp
  - Main code for RVM implementation
- tests/
  - Directory containing tests to verify RVM semantics

## Implementation
An application uses the RVM library by first creating an rvm instance using
the rvm_init() call. This call takes in a directory path. To support 
recoverable virtual memory, the library uses this path as a backing directory.
The backing directory will contain backing files that represent portions of recoverable virtual 
memory as well as log file that contains any recent committed changes.
 
An application can then map recoverable virtual memory segments into their memory
space using the rvm_map() call. If a segment previously existed, the 
library reads the memory segment from the backing file and then
applies any applicable changes from the log. If a segment did not exist, then the
library simply returns a 0-filled region.

To support recovery, all persistent changes to recoverable virtual memory segments
should be wrapped in a transaction. Specifically, an application calls rvm_begin_trans()
and passes in a list of segments that may be modified. Before a portion of a segment is 
about to be changed, the application should call rvm_about_to_modify(). At this point, 
the library makes an in-memory copy of that segment to serve as an undo record. This is to 
support the feature where an application can call rvm_abort_trans() during a transaction, 
and all segments will revert back to any changes made to regions specified in the 
rvm_about_to_modify() calls. Note, for safety reasons, rvm_about_to_modify() can be called 
multiple times, even if changes do not occur. For performance, if multiple rvm_about_to_modify() 
calls specify the same region, the library will only maintain the undo-copy from the first call.

An application can persist any changes made through the rvm_commit_trans() call. At this point,
the library will copy the data from all the regions specified in about_to_modify() calls and 
then persist these changes to the log file. Segment changes are persisted to the log file instead
of the backing file for performance reasons. (See Section Log File). 

If an application aborts a transaction through rvm_abort_trans(), then the library will
copy back the undo record to the segment, thereby undoing any changes.

After many committed transactions, the log file may grow large due to storing all the changes 
that have been made. In this case, the application can reduce the log file size by calling the
rvm_truncate_log() function. THe library will then take the log file and apply the changes
specified in the log file to the backing files.
 
### Log File
The log file is a binary file that contains the changes from recently committed transactions. 
Transaction changes are stored in the log file instead of the backing file for performance
reasons when using mechanical hard drives. Specifically, if changes were committed directly 
to the appropriate backing file, then the commit stage would have poor performance due to 
writing to random sectors in the hard drive. By writing these changes to the log file instead, 
we have better performance through sequential writes.

The log file is written in the following format:  
\<transaction-1>\<transaction-2>...\<transaction-N>: Committed Transactions

A transaction is specified in the following format:  
\<int bytes>: Transaction ID  
\<size_t bytes>: Number of Records = N  
\<record-1>\<record-2>...\<record-N>: Record of changes made in transaction  
\<size_t bytes>: Number of Records = N  
\<int bytes>: Transaction ID    

Note, the transaction data begins with a transaction ID followed by the number of records and also
 ends with the number of records followed by the transaction ID. This is done to serve as an 
error detector when writing to the file. During parsing, a transaction data will be considered invalid
if the IDs at the start and end do not match or if the number of records at the start or end do not match.

Records can be one of two types: REDO_RECORD or DESTROY_RECORD. The REDO_RECORD contains the changes made 
to a specific region in a segment during a transaction. The DESTROY_RECORD represents the destroying
of a recoverable virtual memory segment (which can be done through the rvm_destroy_segment() call). The
reason we have a DESTROY_RECORD is so that when rvm_destroy_segment() is called, we do not need to
go through the log file and destroy any records related to the segment. Instead, by simply having 
a destroy record, the library can know that any time this record is encountered, any previous 
segment changes should be ignored. 

A REDO_RECORD is specified in the following format:  
\<int bytes>: REDO_RECORD type code  
\<size_t bytes>: Length of segment name = N  
\<N bytes>: Characters making up segment name  
\<size_t bytes>: Region Offset in segment  
\<size_t bytes>: Size of Changed Region = M  
\<M bytes>: Bytes making up region of segment that was changed  

A DESTROY_RECORD is specified in the following format:  
\<int bytes>: DESTROY_RECORD type code  
\<size_t bytes>: Length of segment name = N  
\<N bytes>: Characters making up segment name  

### Backing File
The backing file is a simple binary file representing a recoverable virtual memory segment.

## Compilation
To compile a librvm.so shared library, run make in the top-level directory.
```bash
make
```

## Testing
To compile the tests, run make in the tests directory
```bash
make
```

The tests can be run using the run_tests.sh script:
```bash
./run_tests.sh
```

Or each test can be run individually:
```bash
./test01
```
Note, when running a test individually, it may be necessary to 
delete the backing directory that was created in previous
test runs.
