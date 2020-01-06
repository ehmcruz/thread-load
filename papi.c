#include <papi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <papi.h>

#include "lib.h"
#include "papi.h"

/*
	rapl counters:
	
	"rapl:::PACKAGE_ENERGY:PACKAGE0",
	"rapl:::PACKAGE_ENERGY:PACKAGE1",
	"rapl:::DRAM_ENERGY:PACKAGE0",
	"rapl:::DRAM_ENERGY:PACKAGE1",
	"rapl:::PP0_ENERGY:PACKAGE0",
	"rapl:::PP0_ENERGY:PACKAGE1",
*/

extern thread_t libtload_threads[MAX_THREADS];
static uint32_t IPC=0;
static uint32_t total_event_count;
static char *fname, *fname_results = NULL;
static int start, last;
static char native_counters_list[PAPI_MAX_EVENTS][64];
static int papi_enabled = 0;

extern thread_t *libtload_threads_by_order[MAX_THREADS];

static int papi_thread_id()
{
	return libtload_get_current_thread()->order_id;
}

void libtload_papi_thread_init (thread_t *t)
{
	static int exclusion = 0;
	int retval, i, native;
	
	if (!papi_enabled)
		return;
		
	ASSERT_PRINTF(( retval = PAPI_create_eventset( &(t->EventSet) ) ) == PAPI_OK, "PAPI_create_eventset id %i error %i\n", t->order_id, retval)

	t->event_count = 0;
	for (i=start; i<total_event_count; i++) {
		if (t->event_count >= PAPI_MAX_EVENTS) {
			dprintf("Maximum number of PAPI events (%u) reached for thread %u\n", PAPI_MAX_EVENTS, t->order_id);
			break;
		}
		retval = PAPI_event_name_to_code( native_counters_list[i], &native );
		ASSERT( retval == PAPI_OK )
/*		dprintf( "Trying to add event %i (%s)\n", i, native_counters_list[i] );*/
		if ( ( retval = PAPI_add_event( t->EventSet, native ) ) != PAPI_OK ) {
			dprintf("fail to add event %i (%s) code %i\n", i, native_counters_list[i], retval);
			break;
		}
		else {
			t->event_count++;
/*			dprintf("\tok\n");*/
		}
	}
	
	if (! __sync_lock_test_and_set(&exclusion, 1) ) {
		last += t->event_count - 1;
		__sync_synchronize();
		ASSERT(last >= start);
	}

	for (i=0; i<t->event_count; i++)
		t->values[i] = 0;
	
	ASSERT_PRINTF( ( retval = PAPI_start( t->EventSet ) ) == PAPI_OK, "PAPI_start %i\n", retval);
}

void libtload_papi_init ()
{
	int i, retval, j;
	const PAPI_hw_info_t *hwinfo;
	FILE *fp;
	char *counter_list, *p;
	
	/* get counter list */
	
	counter_list = libtload_env_get_str("PAPI_COUNTER_LIST");
	if (counter_list == NULL) {
		dprintf("papi env var PAPI_COUNTER_LIST undefined, disabling PAPI\n");
		return;
	}
	if(strcmp(counter_list, "IPC") == 0){
		IPC = 1;
		counter_list = "PAPI_TOT_CYC,PAPI_TOT_INS";
	}

	fname = libtload_env_get_str("PAPI_FNAME_LOCK");
	if (fname == NULL) {
		dprintf("papi env var PAPI_FNAME_LOCK undefined, init from beginning\n");
	}
	
	fname_results = libtload_env_get_str("PAPI_FNAME_RESULTS");
	
	p = counter_list;
	i = 0;
	p = libtload_strtok(p, native_counters_list[i], ',', 64);
	while (p != NULL) {
		ASSERT(i < PAPI_MAX_EVENTS);
/*dprintf("token %s\n", tok);*/
		i++;
		p = libtload_strtok(p, native_counters_list[i], ',', 64);
	}
	total_event_count = i;
	
	for (i=0; i<MAX_THREADS; i++) {
		libtload_threads[i].EventSet = PAPI_NULL;
		libtload_threads[i].event_count = 0;
	}
	
	if (fname) {
		fp = fopen(fname, "r");
		if (!fp)
			start = 0;
		else {
			fscanf(fp, "%i", &start);
			fclose(fp);
			start++;
		}
	
		if (start == (total_event_count)) {
			dprintf("papi finish\n");
			fp = fopen(fname, "w");
			ASSERT(fp != NULL);
			fprintf(fp, "finish");
			fclose(fp);
			exit(0);
		}
		else {	
			dprintf("starting from event %i of %i (%s)\n", start, total_event_count-1, native_counters_list[start]);
		}
	}
	else
		start = 0;

	
	ASSERT_PRINTF( ( retval = PAPI_library_init( PAPI_VER_CURRENT ) ) == PAPI_VER_CURRENT, "PAPI_library_init %i\n", retval);

	ASSERT_PRINTF( ( hwinfo = PAPI_get_hardware_info(  ) ) != NULL, "PAPI_get_hardware_info %i\n", PAPI_EMISC);

	retval = PAPI_thread_init( ( unsigned long ( * )( void ) ) ( papi_thread_id ) );
	ASSERT(retval == PAPI_OK);
	
	for (i=0; i<MAX_THREADS; i++) {
		for (j=0; j<PAPI_MAX_EVENTS; j++)
			libtload_threads[i].values[j] = 0;
	}

	dprintf("Architecture %s, %d\n", hwinfo->model_string, hwinfo->model);

	__sync_synchronize();
	
	papi_enabled = 1;
}

void libtload_papi_thread_finish (thread_t *t)
{
	int retval;
	
	if (!papi_enabled)
		return;
	
	ASSERT_PRINTF( ( retval = PAPI_stop( t->EventSet, t->values ) ) == PAPI_OK, "PAPI_stop %i\n", retval);

	retval = PAPI_cleanup_eventset( t->EventSet );
	ASSERT( retval == PAPI_OK )
	retval = PAPI_destroy_eventset( &(t->EventSet) );
	ASSERT( retval == PAPI_OK )
}

void libtload_papi_finish ()
{
	int i, j, id;
	FILE *fp;
	thread_t *t;
	
	if (!papi_enabled)
		return;
	
	papi_enabled = 0;
	__sync_synchronize();

	if (fname) {
		fp = fopen(fname, "w");
		ASSERT(fp != NULL);
		fprintf(fp, "%i", last);
		fclose(fp);
	}

	for (id=0; id<libtload_get_total_nthreads(); id++) {
		t = libtload_threads_by_order[id];
		
		if (likely(t)) {
			if(IPC){
				if(t->order_id != 1)
                                stat_printf3(t->order_id, "PAPI_IPC", (double) t->values[1] / (double) t->values[0]);
			}

			j = 0;
			for ( i = start; i<=last; i++ ) {
				stat_printf(t->order_id, native_counters_list[i], t->values[j]);
				j++;
			}
		}
	}
}

