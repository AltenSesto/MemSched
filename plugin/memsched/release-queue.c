/* 
 * Copyright (c) 2014 MDH.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Public License v3.0
 * which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/gpl.html
 */
/*
'REL_Q_SIZE' should have a higher value than the highest valued server period.
Also, the value of 'REL_Q_SIZE' should have the power of 2 , i.e., 2..4..8..16..32..64..128..256..512 etc.
*/
#define REL_Q_SIZE 128

// REL_Q_SIZE / 32
#define BITSIZE 4


#ifndef _INTSIZEBIT_
#define INT_SIZE_BIT (sizeof(int)<<3) // Integer size in bits (size in bytes multiplied by 8).
#endif

#ifndef _DIVBYSYSBIT_
#define DIV_BY_SYS_BIT(x) ( x >> (ffs(INT_SIZE_BIT)-1) ) // Divide x by the system bit size.
#endif

#ifndef _SETBIT_
#define SET_BIT(x) (1 << x) // The value x represents the bit-index of an integer, that should be set.
#endif

/*
   The node representation in the 'bitmap_extension' structure...
*/
typedef struct list10 relNode;
struct list10 {
  int index;
  relNode *next;
};

// This is the main data-structure of this queue implementation.
typedef struct list11 {
  //modified by MemSched group
  unsigned long virtual_time_set; //keep track of when the timer is set. Needed for memsched
  
  int virtual_time;	      // Keep track of the current time (especially during wrap-arounds).
  int nr_of_elements;	      // Keep track of the number of elements in the queue.

  int active_bitmap_int_index; // Optimization: Keeps track off which integers in bitmap to
                               // disregard. For example, if no values are between 1-32, then
                               // ignore the first integer in the active bitmap.

  int bitmap_size;            // Size is the length of a 'bitmap[]' (number of indexes [0][1][2][3]...),
                              // 'bitmap_size'*INT_SIZE_BIT should be >= than the largest period value.
  int active_bitmap;          // Holds 0|1, e.g., which of the two bitmaps/bitmap-extensions that are active.
  int bitmap[2][BITSIZE];     // A bitmap is an array of integers. The first integer,[0],
                              // represents the values 1-32. The second integer,[1],
                              // represents 33-64 etc.
                              // Two bitmaps/bitmap-extensions are required to handle wrap-arounds.

  relNode *bitmap_extension[2][REL_Q_SIZE+1]; // This queue is mapped to the bitmap, e.g., its corresponding index holds.
} relPq;


int find_largest_period(server_t *queue, int nr_of_elements, int cpu);
int relPq_init(relPq *queue, int largest_period);
void relPq_insert(relPq *queue, int value, relNode *node);
int relPq_peek(relPq *queue, int *value);
relNode *relPq_retrieve(relPq *queue, int *value);
void relPq_destroy(relPq *queue);
//void printqueue(relPq *queue);
//void printlist(relPq *queue);
//void printbitssimple(int n);
//int my_ffs(int x);

/*

int main(int argc, char *argv[]) {

  relPq release_queue;       // Our queue...
  int i, index, choice;
  task *point;
  task *task_queue;       // This list just holds all task TCB structs.
  int largest_period = 0; // Store the largest period value, needed to calc. bitmap size.

  // Find the largest period in 'argv[]'
  for ( i = 1; i < argc; i++ ) {
    if (atoi(argv[i]) > largest_period)
      largest_period = atoi(argv[i]);
  }

  // Set bitmap size and allocate memory...
  init_pq(&release_queue, largest_period);

  task_queue = malloc(sizeof(task)*(argc-1)); // A holder for task TCBs

  // Insert task data into 'task_queue'
  for ( i = 1; i < argc; i++ ) {
    task_queue[i-1].period = atoi(argv[i]);
    task_queue[i-1].next = NULL;
  }

  // INITIALIZE THE SIMULATION
  for ( i = 0; i < (argc-1); i++ ) {
    bitmap_insert(&release_queue, task_queue[i].period, &task_queue[i]); // Insert tasks period in bitmap
  }

  printf("\n\n");
  
  while (1) {
  
    choice = 0;
    printf("Choose (1-5):\n1 - Retrieve element\n2 - Insert element\n3 - View queue\n4 - View list\n5 - Exit\n");
    scanf("%d", &choice);
  
    switch (choice) {
  
      case 1:
	index = 0;
	bitmap_retrieve(&release_queue, &index);        // Returns the index in bitmap.
	point = bitmap_retrieve(&release_queue, &index); // Returns the node ('point') in 'bitmap_extension'.
	if (point != NULL) {
	  printf("\nRetrieved index: %d, Period:%d\n", index, point->period);
	}
	else {
	  printf("FISHY!!!\n");
	  free(task_queue);
	  destroy_pq(&release_queue);
	  return 1;
	}
      break;
    
      case 2:
	printf("Value of element:\n");
	scanf("%d", &index);
	bitmap_insert(&release_queue, index, point);
      break;

      case 3:
        printqueue(&release_queue);
      break;

      case 4:
        printlist(&release_queue);
      break;
    
      case 5:
	free(task_queue);
	destroy_pq(&release_queue);
	exit(1);
      break;
    
      default:
	printf("\nIncorrect choice, try again...\n");
      break;
  
    }
  
  }

  
  return 1;

}

*/
int find_largest_period(server_t *queue, int nr_of_elements, int cpu) {

  int i, largest;

  largest = 0;

  for (i = 0; i < nr_of_elements; i++) {
	if(queue[i].cpu == cpu){//multicore:to indicate the largest server is fetched only from the specific core
    		if (queue[i].period > largest)
		      largest = queue[i].period;
	}
  }

  return largest;

}

// Initialize the queue structure, e.g., set size, set variables and allocate mamory.
int relPq_init(relPq *queue, int largest_period) {

  int i;

  queue->bitmap_size = 0;
  queue->virtual_time = 0;
  queue->nr_of_elements = 0;

  // Iterate until we find a bitmap size that is >= the largest period value
  while ( 1 ) {
    if (queue->bitmap_size*INT_SIZE_BIT >= largest_period)
      break;
    queue->bitmap_size++;
  }

  queue->active_bitmap_int_index = 0;
  queue->active_bitmap = 0;
/*
  if ( !(queue->bitmap[0] = kmalloc(sizeof(int)*queue->bitmap_size, GFP_ATOMIC)) )
    return -1;

  if ( !(queue->bitmap[1] = kmalloc(sizeof(int)*queue->bitmap_size, GFP_ATOMIC)) )
    return -1;

  if ( !(queue->bitmap_extension[0] = kmalloc(sizeof(relNode *)*queue->bitmap_size*INT_SIZE_BIT, GFP_ATOMIC)) )
    return -1;

  if ( !(queue->bitmap_extension[1] = kmalloc(sizeof(relNode *)*queue->bitmap_size*INT_SIZE_BIT, GFP_ATOMIC)) )
    return -1;
*/
  for ( i = 0; i <= (queue->bitmap_size*INT_SIZE_BIT); i++ ) {
    queue->bitmap_extension[0][i] = NULL;
    queue->bitmap_extension[1][i] = NULL;
  }
  
  for ( i = 0; i < queue->bitmap_size; i++ ) {
  
    queue->bitmap[0][i] = 0;
    queue->bitmap[1][i] = 0; 
  }

  return 1;

}

// Deallocate memory of the queue structure.
void relPq_destroy(relPq *queue) {
/*
  kfree(queue->bitmap[0]);
  kfree(queue->bitmap[1]);
  kfree(queue->bitmap_extension[0]);
  kfree(queue->bitmap_extension[1]);
*/
}


// Insert value 'value' in queue 'queue'. 'node' is the node to be inserted in 'bitmap_extension[value]'.
void relPq_insert(relPq *queue, int value, relNode *node) {

  relNode *temp;
  int bitmap_index;
  int xor_flag;       // Has value 0|1, depending on which bitmap that is active.
  int div_by_32_flag; // If the inserted value is even divisible by 32, then bitmap indexing will not work,
                      // that is why we use this flag in order to get the correct index into the bitmap...

  div_by_32_flag = 0;
  xor_flag = 0;
  queue->nr_of_elements++;

  if (value > (queue->bitmap_size*INT_SIZE_BIT)) { // The value wraps around the bitmap-length
    value -= queue->bitmap_size*INT_SIZE_BIT; // Do the wrapping...
    xor_flag = 1; // This will make us put the new value in the other (inactive) bitmap...
  }

  // Check if it is even divisible by 32, if so, index to bitmap must be corrected...
  if ( (0x000000000000001F & value) == 0 ) {
    div_by_32_flag = 1;
  }

  bitmap_index = (DIV_BY_SYS_BIT(value)-div_by_32_flag); // The index of the bitmap where the new value should be inserted.

  queue->bitmap[(queue->active_bitmap ^ xor_flag)][bitmap_index] |= SET_BIT( (((value-(bitmap_index*INT_SIZE_BIT)))-1) );
  //printf("  -Active bitmap:%d, value:%d (bit-index:%ld), index:%d\n", (queue->active_bitmap ^ xor_flag), value, ((value-(bitmap_index*INT_SIZE_BIT))), bitmap_index);
  //printbitssimple(queue->bitmap[(queue->active_bitmap ^ xor_flag)][bitmap_index]);

  // Also insert a node in the bitmap extension structure...
  if ( queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value] == NULL ) { // Empty...
    queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value] = node;
    node->next = NULL;
  }
  else { // One or several nodes in this index...
    temp = queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value];
    queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value] = node;
    node->next = temp;
  }

}

int relPq_peek(relPq *queue, int *value) {

  int i, lsb, active, index, virtual;

  lsb = 0;

  active = queue->active_bitmap;
  index = queue->active_bitmap_int_index;
  virtual = queue->virtual_time;

  // Wuuuw...there are no elements...return a negative value!
  if (queue->nr_of_elements == 0) {
    *value = -1;
    return -1;
  }

  for ( i = index; i < queue->bitmap_size; i++ ) {
      if ( (lsb = ffs(queue->bitmap[active][i])) == 0 ) {
        index++;
        if ( index == queue->bitmap_size ) { // No values att all in this bitmap...
          active ^= 1;                       // ...then we shift to the other bitmap,
          index = 0;                         // i.e., we do a wrap-around...

          // The virtual time must be adjusted for the wrap-around...
          virtual = (queue->bitmap_size*INT_SIZE_BIT) - virtual;
          virtual *= -1; // Negate...

          i = -1; // Start from the beginning of the new bitmap...
          //printf("\nBITMAP SHIFT to %d!!!\n", queue->active_bitmap);
        }
      }
      else
        break;
  }

  *value = lsb+(i*INT_SIZE_BIT); // Return the smallest value...
  return virtual;
}

// Fetch the smallest value in the queue structure.
// If value==0, then an index of the smallest value is returned (in variable 'index'), 'relNode' is NULL.
// If value>0, then it will return a node ('relNode') in 'bitmap_extension' at index 'value'.
// If no more nodes exists at that index, then 'relNode'=NULL is returned.
relNode *relPq_retrieve(relPq *queue, int *value) {

  relNode *temp;
  int i, lsb;

  lsb = 0;

  // Wuuuw...there are no elements...return a negative value!
  if (queue->nr_of_elements == 0 && *value == 0) {
    *value = -1;
    return NULL;
  }

  if ( *value == 0 ) { // Find and return the smallest value in the bitmap.
    for ( i = queue->active_bitmap_int_index; i < queue->bitmap_size; i++ ) {
      if ( (lsb = ffs(queue->bitmap[queue->active_bitmap][i])) == 0 ) {
        queue->active_bitmap_int_index++;
        if ( (queue->active_bitmap_int_index) == queue->bitmap_size ) { // No values att all in this bitmap...
          queue->active_bitmap ^= 1;                                    // ...then we shift to the other bitmap,
          queue->active_bitmap_int_index = 0;                           // i.e., we do a wrap-around...

          // The virtual time must be adjusted for the wrap-around...
          queue->virtual_time = (queue->bitmap_size*INT_SIZE_BIT) - queue->virtual_time;
          queue->virtual_time *= -1; //queue->virtual_time = ~(queue->virtual_time) | 1; // Negate...

          i = -1; // Start from the beginning of the new bitmap...
          //printf("\nBITMAP SHIFT to %d!!!\n", queue->active_bitmap);
        }
      }
      else
        break;
    }
	
    *value = lsb+(i*INT_SIZE_BIT); // Return the smallest value...
	//printk("KERN_WARNING at_core: 1 value: %d\n", value);
    queue->nr_of_elements--;
	
    // Remove it from the bit-map ONLY IF there is one element at that index position!
    if (queue->bitmap_extension[queue->active_bitmap][*value]->next == NULL) {
      queue->bitmap[queue->active_bitmap][i] ^= SET_BIT( (lsb-1) ); // Zero the bit we are retrieving...
    }
	
  }
  else { // Return a node (if any) in index 'value'.

    if ( queue->bitmap_extension[queue->active_bitmap][*value] == NULL ) { // Empty...
      return NULL;
    }

    temp = queue->bitmap_extension[queue->active_bitmap][*value];

    if ( queue->bitmap_extension[queue->active_bitmap][*value]->next != NULL )
      queue->bitmap_extension[queue->active_bitmap][*value] = queue->bitmap_extension[queue->active_bitmap][*value]->next;
    else
      queue->bitmap_extension[queue->active_bitmap][*value] = NULL;

    temp->next = NULL;

    return temp;
  }

  return NULL;

}

/*
void printqueue(relPq *queue) {

  int j;

  printf("-----------------------------------------ACTIVE=0-----------------------------------------\n");
  for (j = 0; j < queue->bitmap_size; j++) {
    printf("[%d]\n", j);
    printbitssimple(queue->bitmap[0][j]);
  }
  printf("\n-----------------------------------------ACTIVE=1-----------------------------------------\n");
  for (j = 0; j < queue->bitmap_size; j++) {
    printf("[%d]\n", j);
    printbitssimple(queue->bitmap[1][j]);
  }
  printf("------------------------------------------------------------------------------------------\n");

}

void printlist(relPq *queue) {

  int j;
  resch_task_t *temp;
//task *temp;

  printf("\n");
  printf("--------------------------------------\n");

  for (j = 0; j < (queue->bitmap_size*INT_SIZE_BIT); j++) {
    if (queue->bitmap_extension[0][j] != NULL) {
      temp = queue->bitmap_extension[0][j];
      while (1) {
        printf("[active=0] [bitmap-index=%d] Period:%d\n", j, temp->period);
        if (temp->next == NULL) {
          printf("--------------------------------------\n");
          break;
        }
        else {
          temp = temp->next;
        }
      }
    }
  }

  for (j = 0; j < (queue->bitmap_size*INT_SIZE_BIT); j++) {
    if (queue->bitmap_extension[1][j] != NULL) {
      temp = queue->bitmap_extension[1][j];
      while (1) {
        printf("[active=1] [bitmap-index=%d] Period:%d\n", j, temp->period);
        if (temp->next == NULL) {
          printf("--------------------------------------\n");
          break;
        }
        else {
          temp = temp->next;
        }
      }
    }
  }

}

// Print n as a binary number, good for debugging =)
void printbitssimple(int n) {

  unsigned int i;

  printf("  |");
  for ( i = 32; i > 0; i--)
    printf("%d|", i);
  printf("\n");
  printf("  ");

  i = 1<<(sizeof(n) * 8 - 1);
  while (i > 0) {
    if (n & i) {
      if (i > 256)
        printf("|1 ");
      else
        printf("|1");
    }
    else {
      if (i > 256)
        printf("|0 ");
      else
        printf("|0");
    }
    i >>= 1;
  }
  printf("|\n");
}


int my_ffs(int x) {

  int r = 1;

  if (!x)
    return 0;

  if (!(x & 0xffff)) {
    x >>= 16;
    r += 16;
  }
  if (!(x & 0xff)) {
    x >>= 8;
    r += 8;
  }
  if (!(x & 0xf)) {
    x >>= 4;
    r += 4;
  }
  if (!(x & 3)) {
    x >>= 2;
    r += 2;
  }
  if (!(x & 1)) {
    x >>= 1;
    r += 1;
  }
  return r;
}
*/
