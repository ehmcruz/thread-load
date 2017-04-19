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

typedef struct papi_native_thread_local_t {
	int EventSet;
	long long values[PAPI_MAX_EVENTS];
	int event_count;
} papi_native_thread_local_t;

static papi_native_thread_local_t papi_per_thread[MAX_THREADS];
static uint32_t total_event_count;
static char *fname, *fname_results = NULL;
static int start, last;
static char native_counters_list[PAPI_MAX_EVENTS][64];
static int papi_enabled = 0;

static int papi_thread_id()
{
	return libtload_get_current_thread()->order_id;
}

void libtload_papi_thread_init (thread_t *t)
{
	static int exclusion = 0;
	int retval, i, native;
	uint32_t id;
	
	if (!papi_enabled)
		return;
	
	id = t->order_id;
	
	ASSERT_PRINTF(( retval = PAPI_create_eventset( &(papi_per_thread[id].EventSet) ) ) == PAPI_OK, "PAPI_create_eventset id %i error %i\n", id, retval)

	papi_per_thread[id].event_count = 0;
	for (i=start; i<total_event_count; i++) {
		if (papi_per_thread[id].event_count >= PAPI_MAX_EVENTS) {
			dprintf("Maximum number of PAPI events (%u) reached for thread %u\n", PAPI_MAX_EVENTS, id);
			break;
		}
		retval = PAPI_event_name_to_code( native_counters_list[i], &native );
		ASSERT( retval == PAPI_OK )
		dprintf( "Trying to add event %i (%s)\n", i, native_counters_list[i] );
		if ( ( retval = PAPI_add_event( papi_per_thread[id].EventSet, native ) ) != PAPI_OK ) {
			dprintf("\tfail %i\n", retval);
			break;
		}
		else {
			papi_per_thread[id].event_count++;
			dprintf("\tok\n");
		}
	}
	
	if (! __sync_lock_test_and_set(&exclusion, 1) ) {
		last += papi_per_thread[id].event_count - 1;
		__sync_synchronize();
		ASSERT(last >= start);
	}

	for (i=0; i<papi_per_thread[id].event_count; i++)
		papi_per_thread[id].values[i] = 0;
	
	ASSERT_PRINTF( ( retval = PAPI_start( papi_per_thread[id].EventSet ) ) == PAPI_OK, "PAPI_start %i\n", retval);
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
		lm_printf("papi env var %s undefined\n", libtload_envname(ENV_libtload_PAPI_COUNTER_LIST));
		return;
	}
	
	fname = libtload_env_get_str("PAPI_FNAME_LOCK");
	if (fname == NULL) {
		lm_printf("papi env var %s undefined\n", libtload_envname(ENV_libtload_PAPI_FNAME_LOCK));
		return;
	}
	
	fname_results = libtload_env_get_str("PAPI_FNAME_RESULTS");
	
	papi_enabled = 1;
	
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
		papi_per_thread[i].EventSet = PAPI_NULL;
		papi_per_thread[i].event_count = 0;
	}
	
	fp = fopen(fname, "r");
	if (!fp)
		start = 0;
	else {
		fscanf(fp, "%i", &start);
		fclose(fp);
		start++;
	}
	
	if (start == (total_event_count)) {
		lm_printf("papi finish\n");
		fp = fopen(fname, "w");
		ASSERT(fp != NULL);
		fprintf(fp, "finish");
		fclose(fp);
		libtload_panic(0);
	}
	else {	
		dprintf("starting from event %i of %i (%s)\n", start, total_event_count-1, native_counters_list[start]);
	}
	
	ASSERT_PRINTF( ( retval = PAPI_library_init( PAPI_VER_CURRENT ) ) == PAPI_VER_CURRENT, "PAPI_library_init %i\n", retval);

	ASSERT_PRINTF( ( hwinfo = PAPI_get_hardware_info(  ) ) != NULL, "PAPI_get_hardware_info %i\n", PAPI_EMISC);

	retval = PAPI_thread_init( ( unsigned long ( * )( void ) ) ( papi_thread_id ) );
	ASSERT(retval == PAPI_OK);
	
	for (i=0; i<MAX_THREADS; i++) {
		for (j=0; j<PAPI_MAX_EVENTS; j++)
			papi_per_thread[i].values[j] = 0;
	}

	dprintf("Architecture %s, %d\n", hwinfo->model_string, hwinfo->model);
}

void libtload_papi_thread_finish (thread_t *t)
{
	int retval;
	uint32_t id;
	
	if (!papi_enabled)
		return;
	
	id = t->order_id;
	
	ASSERT_PRINTF( ( retval = PAPI_stop( papi_per_thread[id].EventSet, papi_per_thread[id].values ) ) == PAPI_OK, "PAPI_stop %i\n", retval);

	retval = PAPI_cleanup_eventset( papi_per_thread[id].EventSet );
	ASSERT( retval == PAPI_OK )
	retval = PAPI_destroy_eventset( &(papi_per_thread[id].EventSet) );
	ASSERT( retval == PAPI_OK )
}

void libtload_papi_finish ()
{
	int i, j, id;
	FILE *fp;
	static char buffer1[200], buffer2[200];
	
	if (!papi_enabled)
		return;

	fp = fopen(fname, "w");
	ASSERT(fp != NULL);
	fprintf(fp, "%i", last);
	fclose(fp);

	for (id=0; id<libtload_get_total_nthreads(); id++) {
		j = 0;
		for ( i = start; i<=last; i++ ) {
			sprintf(buffer1, "%-30s: %llu", native_counters_list[i], papi_per_thread[id].values[j] );
			sprintf(buffer2, "papi == %i == %s == %llu\n", id, native_counters_list[i], papi_per_thread[id].values[j] );
/*			libtload_statistics_add(buffer1, buffer2);*/
			j++;
		}
	}
}

