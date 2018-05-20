#include <stdio.h>
#include <string.h> // *** Πηγα να το δοκιμασω με το memcpy (να προστιθενται τα sorted στοιχεια το ενα μετα το αλλο σε ένα πίνακα) αλλα δεν τα κατάφερα και το άφησα έτσι.
#include <stdlib.h>
#include <pthread.h>

#define N 	    10          /* Queue Size (= Mhnymata Paketwn-Ergasias) */
#define THREAD_POOL 4		/* Number of threads */
#define SIZE        200		/* Array Size */
#define CUTOFF 	    10		/* Sorting limit */

/* struct of info passed to each thread */
struct info_msg {
	/* Paketo ergasias. */
	double *arr;				// Partition-Array 
	int size;				// Partition's-Array size
};

struct info_msg queue_msg[N];	// Oura-Ergasiwn twn Messages
int head=0;
int tail = 0;

int sorted_elems = 0;		// Arithmos twn taksinomhmenwn stoixeiwn

// Mhnyma termatismou.
int shutdown_status=0;
// Position 
int pos=0;

int check_pos =0;
// mutex protecting common resources
pthread_mutex_t queue_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t check_mtx = PTHREAD_MUTEX_INITIALIZER;

// Condition Variables
pthread_cond_t full_queue = PTHREAD_COND_INITIALIZER;	   // signals when queue is not full (receiver waits on this)
pthread_cond_t unsorted = PTHREAD_COND_INITIALIZER;	   // signals when sorting-algorithm is finishhed.
pthread_cond_t empty_queue = PTHREAD_COND_INITIALIZER;	   // signals when just added a package in queue.
pthread_cond_t avail_partition = PTHREAD_COND_INITIALIZER; // signals when has available partition.

void inssort(double *a,int n) {
	int i,j;
	double t;

	for (i=1;i<n;i++) {
		j = i;
		while ((j>0) && (a[j-1]>a[j])) {
			t = a[j-1];  a[j-1] = a[j];  a[j] = t;
			j--;
		}
	}
}
int partition(double *a,int n){
	int i,j; 				// counters  
	int first,middle,last;	// take first, last and middle positions
	double t,p;

	first = 0;
	middle = n-1;
	last = n/2;  

	// put median-of-3 in the middle
	if (a[middle]<a[first]) { t = a[middle]; a[middle] = a[first]; a[first] = t; }
	if (a[last]<a[middle]) { t = a[last]; a[last] = a[middle]; a[middle] = t; }
	if (a[middle]<a[first]) { t = a[middle]; a[middle] = a[first]; a[first] = t; }

	// partition (first and last are already in correct half)
	p = a[middle]; // pivot
	for (i=1,j=n-2;;i++,j--) {
   		while (a[i]<p){  i++;}
    	while (p<a[j]) j--;
    	if (i>=j) break;

    	t = a[i]; a[i] = a[j]; a[j] = t;      
    }

	return i;
}

/*NOTE: Put packages into queue  */
void *producer(void *args){
	int i;
	double *init_arr = (double *)args; 
	// Arxikopoihsh ths ouras-ergasiwn
	for(i=0; i<N; i++){
		queue_msg[i].arr  = NULL;
	}
	pthread_mutex_lock(&queue_mtx);
	printf("(P) thread_id: %ld, FIRST\n", pthread_self());
	// Topothethsh sthn oura tou PRWTOU paketou-ergasias:
	queue_msg[tail].arr  = init_arr;			// Pass array init_arr 
	queue_msg[tail].size = SIZE;
	// Move forward tail to next empty slot.
	tail++;
	if(tail>N){						// Reset tail back at start of FIFO.
		tail=0;                       
	}
	//signal stous consumers na ksekinisoun thn douleia tous
	pthread_cond_signal(&empty_queue);	// signal sthn empty_queue.
	pthread_mutex_unlock(&queue_mtx);

	
	//Trexei sunexws gia na topothetei sthn oura ta packages kai otan lavei mhnhma shutdown_status termatizei
	while(shutdown_status!=1){
	    pthread_mutex_lock(&queue_mtx);
		
		while(check_pos==0){
			printf("(P) waits in avail_partition\n");
			pthread_cond_wait(&avail_partition, &queue_mtx);
		}
		if(shutdown_status==1){
			pthread_exit(NULL); //exit and let be joined
		}
		printf("(P) New partition came (pos= %d)\n", pos);

	    // Dhmiourgia 2 newn paketwn ergasias
	    // NOTE: Me to paragomeno 'pos', dhmiourgountai 2 nea partitions (ypo-pinakes)
	    // Insert package to queue:
    	
    	//1st package:
    	while((tail+1)%N == head){					// When queue is FULL. NOTE: we use while instead of if!
			printf("(P) waits in full_queue 1 \th/t=%d\n", tail);
			pthread_cond_wait(&full_queue, &queue_mtx);
		}
	
	    queue_msg[tail].arr   = init_arr;
	    queue_msg[tail].size  = pos;
	    // Move forward tail to next empty slot.
		tail++;
		if(tail>N){				// Reset tail back at start of FIFO.
			tail=0;                       
		}	
		pthread_cond_signal(&empty_queue);	// signal sthn empty_queue.

	    //2nd package:
	    while((tail+1)%N == head){					// When queue is FULL. NOTE: we use while instead of if!
			printf("(P) waits in full_queue 2\n");
			pthread_cond_wait(&full_queue, &queue_mtx);	// wait sthn full_queue
		}
	    queue_msg[tail].arr   = init_arr+pos;
	    queue_msg[tail].size  = SIZE-pos;
	    // Move forward tail to next empty slot.
		tail++;
		if(tail>N){						// Reset tail back at start of FIFO.
			tail=0;                       
		}

		check_pos=0;

		printf("(P) just added 2 head = %d tail = %d\n",head,tail);
		pthread_cond_signal(&empty_queue);	// signal sthn empty_queue.
	    pthread_mutex_unlock(&queue_mtx);	    
		
	}

		pthread_exit(NULL); //exit and let be joined
} 
/*NOTE: Get packages from queue*/
void *consumer(){
	double *a; 		//A partition array to be sorted.
	int n;			//Size of partition array (a).
	int i;
	double *total;
	/* NOTE: Synexhs anazhthsh newn paketwn ergasias. 
			Ta nhmata den ftanoun sto 'return' gia na termatisoun.
			Ayto symbainei mono otan h synthhkh ginei alhthhs (Otan parei timh).*/
	while(shutdown_status!=1){
		//printf("Thread %ld is running function.\n", pthread_self());
		/* Anazhthsh-Eksagwgh neou paketou ergasias sthn oura. */

		printf("(C) thread %ld before lock\n",pthread_self());
		pthread_mutex_lock(&queue_mtx);
		printf("(C) thread %ld running\n",pthread_self());

		pthread_mutex_lock(&check_mtx);
		if(sorted_elems==SIZE){

			printf("$$$$$$$$$$ sorted_elems=%d\n", sorted_elems);
			pthread_cond_signal(&unsorted);		// signal thn 'unsorted' sto wait ths main.
		}
		pthread_mutex_unlock(&check_mtx);

		if(shutdown_status==1){
			printf("Consumers finished, shuting down, exiting...\n");
			break;
		}
		while(head==tail){			// Enw h oura einai ADEIA, wait sthn (metablhth-synthhkhs 'empty_queue'), mexri na erthei shma sthn 'empty_queue'.
			printf("(C) thread %ld waits in empty_queue check_pos = %d tail= %d head=%d\n",pthread_self(),check_pos,tail,head);
			check_pos=1;
			if(shutdown_status==1){
				pthread_cond_signal(&empty_queue);
				pthread_exit(NULL); //exit and let be joined
			}
			pthread_cond_wait(&empty_queue, &queue_mtx);	
		}
	
		// Extract package from queue:
		a = queue_msg[head].arr;
	  	n = queue_msg[head].size;

	  	// Move forward FIFO's Head, after extraction:
		head++;
		if(head>N){            // Reset head back at start of FIFO Queue.
		  head=0;
		}

		// check if below cutoff limit
		if (n<=CUTOFF) {
			inssort(a,n);
			memcpy(total + n, a, sizeof(a));
			for(i=0;i<n+n;i++){
				printf("total is  %f \n",total);
			}
			// Enhmerwsh ths "sorted_elems" (pou epeksergazetai h main)
			sorted_elems += n;
			check_pos = 1;
		    pthread_cond_signal(&avail_partition);	
			printf("##### sorted_elems=%d, \t", sorted_elems);

	    	printf("n=%d\n", n);
	    }else{
	  		
			pos = partition(a,n);
			check_pos = 1;

			printf("(C) just extracted (new pos= %d)\n", pos);
		    pthread_cond_signal(&avail_partition);		//signal oti uparxei available partition ston producer
	    }

		pthread_cond_signal(&full_queue);			// singal thn full_queue
	    
	    pthread_mutex_unlock(&queue_mtx);
	}
	return NULL;
}
int main() {
	double *arr;
	pthread_t thr_id[THREAD_POOL];
	pthread_t producer_id;

	int i;

	arr = (double *)malloc(SIZE*sizeof(double));
	if (arr==NULL) {
		printf("error in malloc\n");
		exit(1);
	}

	// fill array with random numbers
	srand(time(NULL));
	for (i=0;i<SIZE;i++) {
		arr[i] = (double)rand()/RAND_MAX;
	}

	//Create producer thread
	pthread_create(&producer_id, NULL, producer,(void *) arr);

	//Create consumer threads
	for(i=0; i<THREAD_POOL; i++){
		pthread_create(&thr_id[i], NULL, consumer, NULL);	
	}

	// Parakolouthhsh ths ouras gia mhnymata oloklhrwshs. (px Athroizontas ton arithmo twn taksinomhmenwn stoixeiwn (sorted_elems))
	pthread_mutex_lock(&check_mtx);
	while(sorted_elems != SIZE){
		pthread_cond_wait(&unsorted, &check_mtx);		// wait sthn unsorted, mexri na erthei signal.
	}
	shutdown_status = 1;
	pthread_mutex_unlock(&check_mtx);

	// join consumer
	for(i=0; i<THREAD_POOL; i++){
		pthread_join(thr_id[i], NULL);	
		printf("Thread (%d/%d) is joined. \n", i, THREAD_POOL);
	}
	//join producer
	pthread_join(producer_id,NULL);

	// destroy mutex - should be unlocked
	pthread_mutex_destroy(&queue_mtx);
	pthread_mutex_destroy(&check_mtx);

	// destroy cvs - no process should be waiting on these
	pthread_cond_destroy(&full_queue);
	pthread_cond_destroy(&unsorted);

	// free array's memory
	free(arr);

	return 0;
}
