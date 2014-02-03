/* 
 * Copyright (c) 2014 MDH.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Public License v3.0
 * which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/gpl.html
 */
#ifndef _INTSIZEBIT_
#define INT_SIZE_BIT (sizeof(int)<<3) // Integer size in bits (size in bytes multiplied by 8).
#endif

#ifndef _SETBIT_
#define SET_BIT(x) (1 << x) // The value x represents the bit-index of an integer, that should be set.
#endif

/* This is the server ready queue, implemented as a bitmap */
typedef struct pq_struct {

	int bitmap;
	server_t *bitmap_extension[INT_SIZE_BIT+1]; // This queue is mapped to the bitmap, it holds server structs.

} pq;

void init_pq(pq *queue);
int bitmap_insert(pq *queue, server_t *node);
server_t *bitmap_get(pq *queue);
server_t *bitmap_retrieve(pq *queue);


// Initialize the queue structure, e.g., reset memory.
void init_pq(pq *queue) {

	int i;

	queue->bitmap = 0;

	for ( i = 0; i <= INT_SIZE_BIT; i++ ) {
		queue->bitmap_extension[i] = NULL;
	}

}

// Insert value 'value' in queue 'queue'. 'the_task' is the node to be inserted in 'bitmap_extension[value]'.
int bitmap_insert(pq *queue, server_t *node) {

	server_t *traverse;

	if (node->priority > (INT_SIZE_BIT-1)) {
		printk(KERN_WARNING "(bitmap_insert) Node priority exceeded %d!!!\n", (INT_SIZE_BIT-1));
		return FALSE;
	}

	queue->bitmap |= SET_BIT( node->priority );

	// Also insert a node in the bitmap extension structure...
	if ( queue->bitmap_extension[node->priority] == NULL ) // Empty...
		queue->bitmap_extension[node->priority] = node;
	else { // One or several nodes in this index...
		traverse = queue->bitmap_extension[node->priority];
		while (1) {
			if (traverse->next == NULL) {
				traverse->next = node;
				break;
			}
			traverse = traverse->next;
		}
	}
	node->next = NULL;

	return TRUE;
}

server_t *bitmap_get(pq *queue) {

	int lsb;

	if ( (lsb = ffs(queue->bitmap)) == 0 ) {
		//printk(KERN_WARNING "(bitmap_get) Bitmap is empty!!!\n");
		return NULL;
	}

	lsb--; // We must decrement because ffs gives index, starting with one...

	if ( queue->bitmap_extension[lsb] == NULL ) { // Empty...
		printk(KERN_WARNING "(bitmap_get) Bitmap extension is empty!!!\n");
		return NULL;
	}

	return queue->bitmap_extension[lsb];
}

// Fetch (and delete) the smallest value in the queue structure.
server_t *bitmap_retrieve(pq *queue) {

	server_t *temp;
	int lsb;

	if ( (lsb = ffs(queue->bitmap)) == 0 ) {
		//printk(KERN_WARNING "(bitmap_retrieve) Bitmap is empty!!!\n");
		return NULL;
	}

	lsb--; // We must decrement because ffs gives index, starting with one...

	if ( queue->bitmap_extension[lsb] == NULL ) { // Empty...
		printk(KERN_WARNING "(bitmap_retrieve) Bitmap extension is empty!!!\n");
		return NULL;
	}

	temp = queue->bitmap_extension[lsb];

	if ( queue->bitmap_extension[lsb]->next != NULL )
		queue->bitmap_extension[lsb] = queue->bitmap_extension[lsb]->next;
	else {
		queue->bitmap_extension[lsb] = NULL;
		queue->bitmap ^= SET_BIT( lsb ); // Zero the bit we are retrieving...
	}

	temp->next = NULL;

	return temp;

}

