#include "src/Level/level.h"
#include "src/allocator.h"
#include "util/file_access.h"
#include "util/random.h"
#include "util/System.hpp"
#include "util/uniform.hpp"
#include <sstream>
#include <cstring>
#include <assert.h>
#include <thread>
#include <ctime>
#include <sys/time.h>

#define LOG(msg) std::cout << msg << "\n"
#define LAYOUT "_level"
#define FIXED 1
#define MIXED_TEST 1
//#define TEST_BANDWIDTH 1

const uint64_t POOLSIZE = (uint64_t)1024*1024*1024*30;

PMEMobjpool *pop;
#ifdef FIXED
LevelHashing<Key_t> *level;
#else
LevelHashing<char *> *level;
#endif

uint64_t *workload;
uint64_t *persist_workload;
uint64_t *value_workload;
struct timeval tv1, tv2;
/*fixed length 16-byte key*/
struct string_key{
  char key[16];
};

struct my_root{
	PMEMoid _level;
};

struct range {
  uint64_t index;
  uint64_t begin;
  uint64_t end;
  uint64_t random_num;
};

void clear_cache(int insert_num){
  uint32_t not_found = 0;
  auto _value_workload = reinterpret_cast<Value_t *>(value_workload);
  for(int i =0; i < insert_num; ++i){
    if(_value_workload[i] == NONE){
      not_found++;
    }
  }
  printf("clear cache: not found = %u\n", not_found);
}

void mixed(struct range *_range) {
#ifdef FIXED
  size_t key;
#else
  char* key;
  //string_key *var_workload = reinterpret_cast<string_key *>(workload);
  string_key *var_workload = reinterpret_cast<string_key *>(persist_workload);
#endif
  UniformRandom rng(_range->random_num);
  char arr[64];
  Value_t value;
  uint32_t random;
  uint32_t not_found = 0;
  auto _value_workload = reinterpret_cast<Value_t *>(value_workload);

  for (uint64_t i = _range->begin; i < _range->end; ++i) {
#ifdef FIXED
    key = workload[i];
    //key = i;
#else
    key = reinterpret_cast<char *>(var_workload + i);
#endif
    random = rng.next_uint32()%10;
    if(random <= 1){
      /*Insert operation*/
      value = _value_workload[i];
      level->Insert(pop, key, value);
    }else{
      /*Search operation*/
      if (level->Get(pop, key) == NONE)
      {
        not_found++;
      }
    }  
  }
  //std::cout <<"not_found = "<<not_found<<std::endl;
}

void concurr_insert(struct range *_range) {
#ifdef FIXED
  size_t key;
#else
  char* key;
  //string_key *var_workload = reinterpret_cast<string_key *>(workload);
  string_key *var_workload = reinterpret_cast<string_key *>(persist_workload);
#endif
  Value_t value;
  auto _value_workload = reinterpret_cast<Value_t *>(value_workload);

  for (uint64_t i = _range->begin; i < _range->end; ++i) {
#ifdef FIXED
    key = workload[i];
#else
    key = reinterpret_cast<char *>(var_workload + i);
#endif
    value = _value_workload[i];
    level->Insert(pop, key, value);
  }
}

void concurr_get(struct range *_range) {
#ifdef FIXED
  size_t key;
#else
  char* key;
  string_key *var_workload = reinterpret_cast<string_key *>(workload);
#endif
  
  uint32_t not_found = 0;
  for (uint64_t i = _range->begin; i < _range->end; ++i) {
#ifdef FIXED
    key = workload[i];
#else
    key = reinterpret_cast<char *>(var_workload + i);
#endif
    if (level->Get(pop, key) == NONE)
    {
      not_found++;
    }
  }
  //std::cout <<"Get: not_found = "<<not_found<<std::endl;
}

void concurr_delete(struct range *_range) {
#ifdef FIXED
  size_t key;
#else
  char* key;
  string_key *var_workload = reinterpret_cast<string_key *>(workload);
#endif

  uint32_t not_found = 0;
  for (uint64_t i = _range->begin; i < _range->end; ++i) {
#ifdef FIXED
    key = workload[i];
#else
    key = reinterpret_cast<char *>(var_workload + i);
#endif
    if (level->Delete(pop, key) == false) {
	    not_found++;
    } 
  }
  
  //std::cout<<"Delete: not found = "<<not_found<<std::endl;
}


int main(int argc, char const *argv[])
{
	assert(argc >= 4);
	int initCap = atoi(argv[1]);
	int insert_num = atoi(argv[2]);
	int thread_num = atoi(argv[3]);
	uint32_t mixed_num = 200000000;

	std::cout<<"The levels is "<<initCap<<std::endl;
	std::cout<<"The inserted number is "<<insert_num<<std::endl;
	std::cout<<"The thread number is "<<thread_num<<std::endl;
	const char *file = "/mnt/pmem0/pmem_level.data";
	//const char *file = "pmem_level.data";
	PMEMoid root;
	struct my_root *rr;

	if (file_exists(file) != 0)
		{
			//pop = pool<my_root>::create(file, LAYOUT, POOLSIZE, CREATE_MODE_RW);
			pop = pmemobj_create(file, LAYOUT, POOLSIZE, 0666);
			if (pop == NULL)
			{
				perror("pmemobj_create error");
				return 1;
			}
			root = pmemobj_root(pop, sizeof(struct my_root));
			rr = (my_root*)pmemobj_direct(root);
			//pmemobj_alloc(pop, &rr->_cceh, sizeof(CCEH), CCEH_TYPE, create_CCEH, &initCap);
#ifdef FIXED
			pmemobj_zalloc(pop, &rr->_level, sizeof(LevelHashing<Key_t>), LEVEL_TYPE);
			level = (LevelHashing<Key_t> *)pmemobj_direct(rr->_level);
#else
			pmemobj_zalloc(pop, &rr->_level, sizeof(LevelHashing<char *>), LEVEL_TYPE);
			level = (LevelHashing<char *> *)pmemobj_direct(rr->_level);
#endif
			initialize_level(pop, level, &initCap);
			//Initialize_CCEH(pop, eh, initCap);

			std::cout<<"Successfully create a pool"<<std::endl;
		}else{
			pop = pmemobj_open(file, LAYOUT);
			if (pop == NULL)
			{
				perror("pmemobj_open error");
				return 1;
			}

			root = pmemobj_root(pop, sizeof(struct my_root));
			rr = (struct my_root*)pmemobj_direct(root);
#ifdef FIXED
			level = (LevelHashing<Key_t> *)pmemobj_direct(rr->_level);
#else
			level = (LevelHashing<char *> *)pmemobj_direct(rr->_level);
#endif
			remapping(level);
			std::cout<<"Successfully open a pool"<<std::endl;
		}
		level->display_size();
	srand((unsigned)time(NULL)); 

  int chunk_size = insert_num / thread_num;
  struct range rarray[thread_num];
  for (int i = 0; i < thread_num; ++i) {
    rarray[i].index = i;
    rarray[i].random_num = rand();
    rarray[i].begin = i * chunk_size + 1;
    rarray[i].end = (i + 1) * chunk_size + 1;
  }
  rarray[thread_num - 1].end = insert_num + 1;

#ifdef TEST_BANDWIDTH
  struct range rarray_insert[24];
  int insert_chunk = insert_num / 24;
  for(int i = 0; i < 24; ++i){
    rarray_insert[i].index = i;
    rarray_insert[i].random_num = rand();
    rarray_insert[i].begin = i * insert_chunk + 1;
    rarray_insert[i].end = (i + 1) * insert_chunk + 1;
  }
  rarray_insert[23].end = insert_num + 1;
#endif

	/* Generate Workload*/
	//Allocator::ZAllocate((void **)&workload, kCacheLineSize, sizeof(uint64_t) * (insert_num + 100) * 4);
	PMEMoid pm_ptr;
	//auto ret = pmemobj_zalloc(pop, &pm_ptr, sizeof(uint64_t) * (insert_num + 100) * 4, 1000);
        //workload = (uint64_t*)pmemobj_direct(pm_ptr);	
#ifdef MIXED_TEST
  workload = (uint64_t*)malloc((mixed_num + 100)*sizeof(uint64_t)*4);
  value_workload = (uint64_t*)malloc((mixed_num + 100)*sizeof(uint64_t));
#ifndef FIXED 
  Allocator::ZAllocate((void **)&persist_workload, kCacheLineSize, sizeof(uint64_t) * (mixed_num + 100) * 2);
#endif
#else
  workload = (uint64_t*)malloc((insert_num + 100)*sizeof(uint64_t)*4);
  value_workload = (uint64_t*)malloc((insert_num + 100)*sizeof(uint64_t));
#ifndef FIXED 
  Allocator::ZAllocate((void **)&persist_workload, kCacheLineSize, sizeof(uint64_t) * (insert_num + 100) * 2);
#endif
#endif
  int i;
  unsigned long long init[4] = {0x12345ULL, 0x23456ULL, 0x34567ULL, 0x45678ULL},
  length = 4;
  init_by_array64(init, length);

  /* Generate Workload*/
int generate_num = insert_num;
#ifdef MIXED_TEST
  generate_num = mixed_num;
#endif

  char var_key[24];
  for(int i = 0; i < generate_num*2 + 2; ++i){
    uint64_t _key = genrand64_int64();
#ifdef FIXED
    workload[i] = _key;
#else
    snprintf(var_key, 24, "%lld", _key);
    var_key[15] = '\0';
    string_key *var_workload = reinterpret_cast<string_key *>(workload);
    strcpy(reinterpret_cast<char *>(var_workload + i), var_key);
#endif
  }

  for(int i = 0; i < generate_num + 1; ++i){
    uint64_t _value = genrand64_int64();
    value_workload[i] = _value;
  }

#ifndef FIXED
  string_key *var_workload = reinterpret_cast<string_key *>(workload);
  string_key *p_var_workload = reinterpret_cast<string_key *>(persist_workload);
  for(int i = 0; i < generate_num + 1; ++i){
    strcpy(reinterpret_cast<char *>(p_var_workload + i), reinterpret_cast<char *>(var_workload + i));
  }
#endif

//-----------------------------------------------Concurrent Insertion Test-----------------------------------------------------------------------
	std::thread *thread_array[1024];
	
	LOG("Concurrent insertion begin");
std::string insertion = "Insertion_";
insertion = insertion + std::to_string(thread_num);
System::profile(insertion, [&](){
	gettimeofday(&tv1, NULL);
#ifdef TEST_BANDWIDTH
	for (int i = 0; i < 24; ++i) {
	thread_array[i] = new std::thread(concurr_insert, &rarray_insert[i]);
	}
	for (int i = 0; i < 24; ++i) {
	thread_array[i]->join();
	delete thread_array[i];
	}
#else
	for (int i = 0; i < thread_num; ++i) {
	thread_array[i] = new std::thread(concurr_insert, &rarray[i]);
	}
	for (int i = 0; i < thread_num; ++i) {
	thread_array[i]->join();
	delete thread_array[i];
	}
#endif
	gettimeofday(&tv2, NULL);
	});
	auto duration = (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 + (double) (tv2.tv_sec - tv1.tv_sec);
		printf ("For %d threads, Insertion Total time = %f seconds, the throughput is %f options/s\n", thread_num,
	         (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
	         (double) (tv2.tv_sec - tv1.tv_sec), insert_num/duration);	
	//eh->Get_Number();

//-----------------------------------------------Concurrent postive Get Test-----------------------------------------------------------------------
	//std::cout<<"There are "<<eh->GetItemNum()<<" items inserted in the hashing index!"<<std::endl;
	//rarray[thread_num-1].end = insert_num + 5;
	/*
	clear_cache(insert_num);
	LOG("Concurrent positive get begin!");
	std::string pos= "Pos_search_";
 pos = pos + std::to_string(thread_num);
  System::profile(pos, [&](){
	gettimeofday(&tv1, NULL);
	for (int i = 0; i < thread_num; ++i)
	{
		thread_array[i] = new std::thread(concurr_get, &rarray[i]);
	}

	for (int i = 0; i < thread_num; ++i)
	{
		thread_array[i]->join();
		delete thread_array[i];
	}
	gettimeofday(&tv2, NULL);
	});
	LOG("Concurrent positive get done!");
	duration = (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 + (double) (tv2.tv_sec - tv1.tv_sec);
	printf ("For %d threads, Postive Search Total time = %f seconds, the throughput is %f options/s\n", thread_num,
	         (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
	         (double) (tv2.tv_sec - tv1.tv_sec), insert_num/duration);	
	//eh->Get_Number();
	*/

 #ifdef MIXED_TEST
    clear_cache(insert_num);
    LOG("Concurrent mixed "
      "begin!------------------------------------------------------------");
  chunk_size = mixed_num / thread_num;
  for (int i = 0; i < thread_num; ++i) {
    rarray[i].begin = insert_num + i * chunk_size + 1;
    rarray[i].end = insert_num + (i + 1) * chunk_size + 1;
  }
  //rarray[thread_num - 1].end = insert_num + mixed_num + 1;
  std::string mix = "Mixed_";
 mix = mix + std::to_string(thread_num);
 //System::profile(mix, [&](){
  gettimeofday(&tv1, NULL);
  for (int i = 0; i < thread_num; ++i) {
    thread_array[i] = new std::thread(mixed, &rarray[i]);
  }

  for (int i = 0; i < thread_num; ++i) {
    thread_array[i]->join();
    delete thread_array[i];
  }
  gettimeofday(&tv2, NULL);
  duration = (double)(tv2.tv_usec - tv1.tv_usec) / 1000000 +
             (double)(tv2.tv_sec - tv1.tv_sec);
  printf(
      "For %d threads, Mixed Total time = %f seconds, the throughput is %f "
      "options/s\n",
      thread_num,
      (double)(tv2.tv_usec - tv1.tv_usec) / 1000000 +
          (double)(tv2.tv_sec - tv1.tv_sec),
      mixed_num / duration);
  //});
#endif

/*
	for (int i = 0; i < thread_num; ++i) {
		rarray[i].begin = insert_num + i * chunk_size + 1;
		rarray[i].end = insert_num + (i + 1) * chunk_size + 1;
	}
	rarray[thread_num - 1].end = insert_num + insert_num + 1;
	LOG("Concurrent negative search begin!");
	 std::string neg= "NP_search_";
 neg = neg + std::to_string(thread_num);
  System::profile(neg, [&](){
	gettimeofday(&tv1, NULL);
	for (int i = 0; i < thread_num; ++i)
	{
		thread_array[i] = new std::thread(concurr_get, &rarray[i]);
	}

	for (int i = 0; i < thread_num; ++i)
	{
		thread_array[i]->join();
		delete thread_array[i];
	}
	gettimeofday(&tv2, NULL);
	});
	LOG("Concurrent negative get done!");
	duration = (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 + (double) (tv2.tv_sec - tv1.tv_sec);
	printf ("For %d threads, Negative Search Total time = %f seconds, the throughput is %f options/s\n", thread_num,
	         (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
	         (double) (tv2.tv_sec - tv1.tv_sec), insert_num/duration);

	
	LOG("Concurrent delete begin!");
	for (int i = 0; i < thread_num; ++i) {
		rarray[i].begin = i * chunk_size + 1;
		rarray[i].end = (i + 1) * chunk_size + 1;
	}
	rarray[thread_num - 1].end = insert_num + 1;
	std::string del = "Delete_";
  del = del + std::to_string(thread_num);
  System::profile(del, [&](){
	//System::profile("Delete", [&](){
	gettimeofday(&tv1, NULL);
	for (int i = 0; i < thread_num; ++i)
	{
		thread_array[i] = new std::thread(concurr_delete, &rarray[i]);
	}

	for (int i = 0; i < thread_num; ++i)
	{
		thread_array[i]->join();
		delete thread_array[i];
	}
	gettimeofday(&tv2, NULL);
	});
	LOG("Concurrent delete done!");
	duration = (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 + (double) (tv2.tv_sec - tv1.tv_sec);
	printf ("For %d threads, Delete Total time = %f seconds, the throughput is %f options/s\n", thread_num,
	         (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
	         (double) (tv2.tv_sec - tv1.tv_sec), insert_num/duration);	
//----------------------------------------------Concurrent negative Get Test-----------------------------------------------------------------------
	/*
	LOG("Concurrent negative get begin!");
	for (int i = 0; i < thread_num; ++i)
	{
		rarray[i].begin = insert_num + i*chunk_size + 1;
		rarray[i].end = insert_num + (i+1)*chunk_size + 1;
	}

		
	pmemobj_close(pop);
//-----------------------------------------------Persistence Test-----------------------------------------------------------------------
/*
	std::cout<<"Insert Testing"<<std::endl;
	pop = pmemobj_open(file, LAYOUT);
	if (pop == NULL)
	{
		perror("pmemobj_open error");
		return 1;
	}

	root = pmemobj_root(pop, sizeof(struct my_root));
	rr = (struct my_root*)pmemobj_direct(root);
	level = (LevelHashing*)pmemobj_direct(rr->_level);
	remapping(level);

	Key_t key;
	char array[64]; 
	Value_t value = (Value_t)array;

	for (int i = 1; i < insert_num+3; ++i)
	{
		key = i;
		level->Insert(pop, key, value);
	}
	pmemobj_close(pop);

	// Test if it has been correclty stored in persistent storage
	std::cout<<"Read Testing"<<std::endl;
	pop = pmemobj_open(file, LAYOUT);
	if (pop == NULL)
	{
		perror("pmemobj_open error");
		return 1;
	}

	root = pmemobj_root(pop, sizeof(struct my_root));
	rr = (struct my_root*)pmemobj_direct(root);
	level = (LevelHashing*)pmemobj_direct(rr->_level);
	remapping(level);

	for (int i = 1; i < insert_num+3; ++i)
	{
		key = i;
		if (level->Get(pop, key) == NONE)
		{
			std::cout<<"Search the key "<< i << ": ERROR!"<<std::endl;
		}
	}
	pmemobj_close(pop);
*/
	return 0;
}
