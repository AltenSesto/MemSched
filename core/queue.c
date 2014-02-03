/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
/*
'REL_Q_SIZE' should have a higher value than the highest valued task period.
Also, the value of 'REL_Q_SIZE' should have the power of 2 , i.e., 2..4..8..16..32..64..128..256..512 etc.
*/
#define REL_Q_SIZE 2048

// REL_Q_SIZE / 32
#define BITMAPSIZE 64


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
typedef struct list10 ReschRelNode;
struct list10 {

  int index;
  ReschRelNode *next;
  ReschRelNode *prev; // <CHANGE>

};

// This is the main data-structure of this queue implementation.
typedef struct list11 {

  int FIFOinsertion;          // When nodes have the same position (same value), retrieve them in FIFO order (1=YES, 0=NO) <CHANGE>
  int virtual_time;	      // Keep track of the current time (especially during wrap-arounds).
  int nr_of_elements;	      // Keep track of the number of elements in the queue.

  int active_bitmap_int_index; // Optimization: Keeps track off which integers in bitmap to
                               // disregard. For example, if no values are between 1-32, then
                               // ignore the first integer in the active bitmap.

  int bitmap_size;            // Size is the length of a 'bitmap[]' (number of indexes [0][1][2][3]...),
                              // 'bitmap_size'*INT_SIZE_BIT should be >= than the largest period value.
  int active_bitmap;          // Holds 0|1, e.g., which of the two bitmaps/bitmap-extensions that are active.
  int bitmap[2][BITMAPSIZE];  // A bitmap is an array of integers. The first integer,[0],
                              // represents the values 1-32. The second integer,[1],
                              // represents 33-64 etc.
                              // Two bitmaps/bitmap-extensions are required to handle wrap-arounds.

  ReschRelNode *bitmap_extension[2][REL_Q_SIZE+1]; // This queue is mapped to the bitmap, e.g., its corresponding index holds.
  ReschRelNode *last[2][REL_Q_SIZE+1];             // Pointer to the last element at each position <CHANGE>
} ReschRelPq;


//int find_largest_period(server_t *queue, int nr_of_elements);
void ReschRelPq_update_size(ReschRelPq *queue, int largest_period);
void ReschRelPq_init(ReschRelPq *queue, int largest_period, int fifo); // <CHANGE>
void ReschRelPq_insert(ReschRelPq *queue, int value, ReschRelNode *node);
int ReschRelPq_peek(ReschRelPq *queue, int *value);
ReschRelNode *ReschRelPq_peekNode(ReschRelPq *queue, int value); // <CHANGE>
ReschRelNode *ReschRelPq_retrieve(ReschRelPq *queue, int *value);
void ReschRelPq_destroy(ReschRelPq *queue);
int my_ffs(int x);

/*
int find_largest_period(server_t *queue, int nr_of_elements) {

  int i, largest;

  largest = 0;

  for (i = 0; i < nr_of_elements; i++) {
    if (queue[i].period > largest)
      largest = queue[i].period;
  }

  return largest;

}
*/

void ReschRelPq_update_size(ReschRelPq *queue, int largest_period) {
 
  // Iterate until we find a bitmap size that is >= the largest period value
  while ( 1 ) {
    if (queue->bitmap_size*INT_SIZE_BIT >= largest_period)
      break;
    queue->bitmap_size++;
  }

}

// Initialize the queue structure, e.g., set size, set variables and allocate mamory.
void ReschRelPq_init(ReschRelPq *queue, int largest_period, int fifo) {

  int i;

  queue->bitmap_size = 0;
  queue->virtual_time = 0;
  queue->nr_of_elements = 0;

  queue->FIFOinsertion = fifo; // <CHANGE>

  // Iterate until we find a bitmap size that is >= the largest period value
  while ( 1 ) {
    if (queue->bitmap_size*INT_SIZE_BIT >= largest_period)
      break;
    queue->bitmap_size++;
  }

  queue->active_bitmap_int_index = 0;
  queue->active_bitmap = 0;
/*
  queue->bitmap[0] = kmalloc(sizeof(int)*queue->bitmap_size, GFP_ATOMIC);
  queue->bitmap[1] = kmalloc(sizeof(int)*queue->bitmap_size, GFP_ATOMIC);
  queue->bitmap_extension[0] = kmalloc(sizeof(ReschRelNode *)*queue->bitmap_size*INT_SIZE_BIT, GFP_ATOMIC);
  queue->bitmap_extension[1] = kmalloc(sizeof(ReschRelNode *)*queue->bitmap_size*INT_SIZE_BIT, GFP_ATOMIC);

  queue->last[0] = kmalloc(sizeof(ReschRelNode *)*queue->bitmap_size*INT_SIZE_BIT, GFP_ATOMIC); // <CHANGE>
  queue->last[1] = kmalloc(sizeof(ReschRelNode *)*queue->bitmap_size*INT_SIZE_BIT, GFP_ATOMIC); // <CHANGE>
*/

  for ( i = 0; i <= REL_Q_SIZE; i++ ) {
    queue->bitmap_extension[0][i] = NULL;
    queue->bitmap_extension[1][i] = NULL;

    queue->last[0][i] = NULL; // <CHANGE>
    queue->last[1][i] = NULL; // <CHANGE>
  }
  
  for ( i = 0; i < BITMAPSIZE; i++ ) {
  
    queue->bitmap[0][i] = 0;
    queue->bitmap[1][i] = 0; 
  }

}

// Deallocate memory of the queue structure.
void ReschRelPq_destroy(ReschRelPq *queue) {
/*
  kfree(queue->bitmap[0]);
  kfree(queue->bitmap[1]);
  kfree(queue->bitmap_extension[0]);
  kfree(queue->bitmap_extension[1]);

  kfree(queue->last[0]); // <CHANGE>
  kfree(queue->last[1]); // <CHANGE>
*/
}


// Insert value 'value' in queue 'queue'. 'node' is the node to be inserted in 'bitmap_extension[value]'.
void ReschRelPq_insert(ReschRelPq *queue, int value, ReschRelNode *node) {

  ReschRelNode *temp;
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
  
  if ( (bitmap_index < queue->active_bitmap_int_index) && (xor_flag == 0) ) { // NEWSTUFF
    queue->active_bitmap_int_index = bitmap_index; // NEWSTUFF
  } // NEWSTUFF

  queue->bitmap[(queue->active_bitmap ^ xor_flag)][bitmap_index] |= SET_BIT( (((value-(bitmap_index*INT_SIZE_BIT)))-1) );

  // <CHANGE>
  if (queue->FIFOinsertion == 0) { // LIFO

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

  } // <CHANGE>
  else { // FIFO

    // Also insert a node in the bitmap extension structure...
    if ( queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value] == NULL ) { // Empty...
      queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value] = node;
      queue->last[queue->active_bitmap ^ xor_flag][value] = node;
      node->prev = NULL;
      node->next = NULL;
    }
    else { // One or several nodes in this index...
      temp = queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value];
      queue->bitmap_extension[queue->active_bitmap ^ xor_flag][value] = node;
      node->next = temp;
      node->prev = NULL;
      temp->prev = node;
    }

  }

}

int ReschRelPq_peek(ReschRelPq *queue, int *value) {

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

ReschRelNode *ReschRelPq_peekNode(ReschRelPq *queue, int value) { // <CHANGE>

  if (value > (queue->bitmap_size*INT_SIZE_BIT) || value < 0) {
    return NULL;
  }

  // <CHANGE>
  if (queue->FIFOinsertion == 0) { // LIFO

    if ( queue->bitmap_extension[queue->active_bitmap][value] == NULL ) { // Empty...
      if ( queue->bitmap_extension[(queue->active_bitmap^1)][value] == NULL ) {
        return NULL;
      }
      return queue->bitmap_extension[(queue->active_bitmap^1)][value];
    }
    return queue->bitmap_extension[queue->active_bitmap][value];

  } // <CHANGE>
  else { // FIFO
    if ( queue->last[queue->active_bitmap][value] == NULL ) { // Empty...
      if ( queue->last[(queue->active_bitmap^1)][value] == NULL ) {
        return NULL;
      }
      return queue->last[(queue->active_bitmap^1)][value];
    }
    return queue->last[queue->active_bitmap][value];
  }
  return NULL;

}

// Fetch the smallest value in the queue structure.
// If value==0, then an index of the smallest value is returned (in variable 'index'), 'ReschRelNode' is NULL.
// If value>0, then it will return a node ('ReschRelNode') in 'bitmap_extension' at index 'value'.
// If no more nodes exists at that index, then 'ReschRelNode'=NULL is returned.
ReschRelNode *ReschRelPq_retrieve(ReschRelPq *queue, int *value) {

  ReschRelNode *temp;
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

    // <CHANGE>
    if (queue->FIFOinsertion == 0) { // LIFO

      temp = queue->bitmap_extension[queue->active_bitmap][*value];

      if ( queue->bitmap_extension[queue->active_bitmap][*value]->next != NULL )
        queue->bitmap_extension[queue->active_bitmap][*value] = queue->bitmap_extension[queue->active_bitmap][*value]->next;
      else
        queue->bitmap_extension[queue->active_bitmap][*value] = NULL;

      temp->next = NULL;

    } // <CHANGE>
    else { // FIFO
      temp = queue->bitmap_extension[queue->active_bitmap][*value];
      if (temp->next == NULL) {
        queue->last[queue->active_bitmap][*value] = NULL;
        queue->bitmap_extension[queue->active_bitmap][*value] = NULL;
        temp->next = NULL;
        temp->prev = NULL;
      }
      else {
        temp = queue->last[queue->active_bitmap][*value];
        queue->last[queue->active_bitmap][*value] = queue->last[queue->active_bitmap][*value]->prev;
        queue->last[queue->active_bitmap][*value]->next = NULL;
        temp->next = NULL;
        temp->prev = NULL;
      }
    }

    return temp;
  }

  return NULL;

}

/*
void printqueue(ReschRelPq *queue) {

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

void printlist(ReschRelPq *queue) {

  int j;
  ReschRelNode *temp;

  printf("\n");
  printf("--------------------------------------\n");

  for (j = 0; j < (queue->bitmap_size*INT_SIZE_BIT); j++) {
    if (queue->bitmap_extension[0][j] != NULL) {
      temp = queue->bitmap_extension[0][j];
      while (1) {
        printf("[active=0] [bitmap-index=%d] Period:%d\n", j, temp->index);
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
        printf("[active=1] [bitmap-index=%d] Period:%d\n", j, temp->index);
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
*/

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

