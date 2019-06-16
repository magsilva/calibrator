/*
 * Calibrator v0.9e
 * by Stefan.Manegold@cwi.nl, http://www.cwi.nl/~manegold/Calibrator/
 *
 * All rights reserved.
 * No warranties.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above notice, this list
 *    of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above notice, this
 *    list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by
 *	Stefan.Manegold@cwi.nl, http://www.cwi.nl/~manegold/.
 * 4. Any publication of result obtained by use of this software must
 *    display a reference as follows:
 *	Results produced by Calibrator v0.9e
 *	(Stefan.Manegold@cwi.nl, http://www.cwi.nl/~manegold/)
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS `AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
Commented by Marco Aurélio Graciotto Silva <magsilva@gmail.com>
(some minor improvements were also done)
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#define VERSION "0.9e"

#define NUMLOADS	100000
#define REDUCE	10
#define NUMTRIES	3
#define MINRANGE	1024
#define MAXLEVELS	9
#define LENPLATEAU	3

#define EPSILON1 0.1
#define EPSILON4 1.0

#define MIN(a,b)	(a<b?a:b)
#define MAX(a,b)	(a>b?a:b)
#define NOABS(x)	((dbl)x)

long MINTIME = 10000;

#define NanosecondsPerIteration(t)	(((double)(t)) / (((double)NUMLOADS) / 1000.0))
#define ClocksPerIteration(t)		(((double)((t) * MHz)) / ((double)NUMLOADS))

#define MEMINFO_FILE "/proc/meminfo"

typedef struct mem_table_struct {
	const char *name;     /* memory type name */
	unsigned long *slot; /* slot in return struct */
} mem_table_struct;

#define MAX_LINE_LEN 255

struct timeval oldtp = { 0 };

typedef struct {
	long	levels;
	long	size[MAXLEVELS];
	long	linesize[MAXLEVELS];
	long	latency1[MAXLEVELS];
	long	latency2[MAXLEVELS];
} cacheInfo;

typedef struct {
	long	levels;
	long	shift;
	long	mincachelines;
	long	entries[MAXLEVELS];
	long	pagesize[MAXLEVELS];
	long	latency1[MAXLEVELS];
	long	latency2[MAXLEVELS];
} TLBinfo;

long use_result_dummy;	/* !static for optimizers. */

/*
 * Returns the number of microseconds from the latest time it was executed till now.
 */
long now( void ) {
	struct timeval tp;
	
	gettimeofday( &tp, 0 );
	/* the code right below is executed just one time, it's the initialization of the oldtp */
	if (oldtp.tv_sec == 0 && oldtp.tv_usec == 0) {
		oldtp = tp;
	}

	/* Question: is useful watch out the seconds spent? */
	return (long)( (long)( tp.tv_sec  - oldtp.tv_sec ) * (long)1000000 +	(long)( tp.tv_usec - oldtp.tv_usec ) );
}

int compare_mem_table_structs(const void *a, const void *b) {
	return strcmp(((const mem_table_struct*)a)->name,((const mem_table_struct*)b)->name);
}


unsigned long proc_get_freq_kernel(unsigned int cpu) {
	FILE *fp;
	char value[MAX_LINE_LEN];
	char file[MAX_LINE_LEN];
	unsigned long value2;

	snprintf(file, MAX_LINE_LEN, "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", cpu);
	fp = fopen(file,"r");
	if (!fp)
		return 0;
	fgets(value, MAX_LINE_LEN, fp);
	fclose(fp);
	if (strlen(value) > (MAX_LINE_LEN - 10)) {
		return 0;
	}
	if (sscanf(value, "%lu", &value2) != 1)
		return 0;

	return value2;
}

/**
 * Get the CPU frequency.
 */
unsigned long guess_cpu_frequency( void ) {
	long ret;
	ret = proc_get_freq_kernel(0);
	return ret / 1000;
}
	
/**
 * Get the ammount of free memory.
 */
long guess_free_memory( void ) {
	/* obsolete */
	unsigned long kb_main_shared;
	/* old but still kicking -- the important stuff */
	unsigned long kb_main_buffers;
	unsigned long kb_main_cached;
	unsigned long kb_main_free;
	unsigned long kb_main_total;
	unsigned long kb_swap_free;
	unsigned long kb_swap_total;
	/* recently introduced */
	unsigned long kb_high_free;
	unsigned long kb_high_total;
	unsigned long kb_low_free;
	unsigned long kb_low_total;
	/* 2.4.xx era */
	unsigned long kb_active;
	unsigned long kb_inact_laundry;
	unsigned long kb_inact_dirty;
	unsigned long kb_inact_clean;
	unsigned long kb_inact_target;
	unsigned long kb_swap_cached;  /* late 2.4 and 2.6+ only */
	/* derived values */
	unsigned long kb_swap_used;
	unsigned long kb_main_used;
	/* 2.5.41+ */
	unsigned long kb_writeback;
	unsigned long kb_slab;
	unsigned long nr_reversemaps;
	unsigned long kb_committed_as;
	unsigned long kb_dirty;
	unsigned long kb_inactive;
	unsigned long kb_mapped;
	unsigned long kb_pagetables;
	// seen on a 2.6.x kernel:
	unsigned long kb_vmalloc_chunk;
	unsigned long kb_vmalloc_total;
	unsigned long kb_vmalloc_used;

	int meminfo_fd = -1;
	char buf[1024];
	int buf_size;


	char namebuf[16]; /* big enough to hold any row name */
	mem_table_struct findme = { namebuf, NULL};
	mem_table_struct *found;
	char *head;
	char *tail;
	const mem_table_struct mem_table[] = {
		{"Active",       &kb_active},       // important
		{"Buffers",      &kb_main_buffers}, // important
		{"Cached",       &kb_main_cached},  // important
		{"Committed_AS", &kb_committed_as},
		{"Dirty",        &kb_dirty},        // kB version of vmstat nr_dirty
		{"HighFree",     &kb_high_free},
		{"HighTotal",    &kb_high_total},
		{"Inact_clean",  &kb_inact_clean},
		{"Inact_dirty",  &kb_inact_dirty},
		{"Inact_laundry",&kb_inact_laundry},
		{"Inact_target", &kb_inact_target},
		{"Inactive",     &kb_inactive},     // important
		{"LowFree",      &kb_low_free},
		{"LowTotal",     &kb_low_total},
		{"Mapped",       &kb_mapped},       // kB version of vmstat nr_mapped
		{"MemFree",      &kb_main_free},    // important
		{"MemShared",    &kb_main_shared},  // important, but now gone!
	        {"MemTotal",     &kb_main_total},   // important
		{"PageTables",   &kb_pagetables},   // kB version of vmstat nr_page_table_pages
		{"ReverseMaps",  &nr_reversemaps},  // same as vmstat nr_page_table_pages
		{"Slab",         &kb_slab},         // kB version of vmstat nr_slab
	        {"SwapCached",   &kb_swap_cached},
		{"SwapFree",     &kb_swap_free},    // important
		{"SwapTotal",    &kb_swap_total},   // important
		{"VmallocChunk", &kb_vmalloc_chunk},
		{"VmallocTotal", &kb_vmalloc_total},
		{"VmallocUsed",  &kb_vmalloc_used},
		{"Writeback",    &kb_writeback},    // kB version of vmstat nr_writeback
	};
	const int mem_table_count = sizeof(mem_table)/sizeof(mem_table_struct);

	/* This macro opens filename only if necessary and seeks to 0 so
	* that successive calls to the functions are more efficient.
	* It also reads the current contents of the file into the global buf.
	*/
	meminfo_fd = open(MEMINFO_FILE, O_RDONLY);
	if (meminfo_fd == -1) {
		return -1;
	}
	lseek(meminfo_fd, 0L, SEEK_SET);
	buf_size = read(meminfo_fd, buf, sizeof buf - 1);
	if (buf_size < 0) {
		return -1;
	}
	buf[buf_size] = '\0';

	kb_inactive = ~0UL;
	head = buf;
	for(;;){
		tail = strchr(head, ':');
		if(!tail) break;
		*tail = '\0';
		if(strlen(head) >= sizeof(namebuf)){
			head = tail+1;
			goto nextline;
		}
		strcpy(namebuf,head);
		found = (mem_table_struct  *) bsearch(&findme, mem_table, mem_table_count, sizeof(mem_table_struct), compare_mem_table_structs);
		head = tail+1;
		if(!found) goto nextline;
		*(found->slot) = strtoul(head,&tail,10);
nextline:
		tail = strchr(head, '\n');
		if(!tail) break;
		head = tail+1;
	}
	if(!kb_low_total){  /* low==main except with large-memory support */
		kb_low_total = kb_main_total;
		kb_low_free  = kb_main_free;
	}
	if(kb_inactive==~0UL){
		kb_inactive = kb_inact_dirty + kb_inact_clean + kb_inact_laundry;
	}
	kb_swap_used = kb_swap_total - kb_swap_free;
	kb_main_used = kb_main_total - kb_main_free;
	
	return kb_main_free * 1024;
}

/*
 * Handles the errors found at runtime (just printout a string with the error message).
 */
void ErrXit( char *format, ... ) {
	va_list	ap;
	char	s[ 1024 ];

	va_start( ap, format );
	vsprintf( s, format, ap );
 	va_end(ap);
	fprintf( stderr, "\n! %s !\n", s );
	fflush( stderr );
	exit( 1 );
}

/*
 * Coverts the memory command line parameter to bytes.
 */
long bytes( char *s ) {
	long	n = atoi( s );
	char 	lastchar = s[ strlen( s ) - 1 ];
	
	if ( ( lastchar == 'k' ) || ( lastchar == 'K' ) )
		n *= 1024;
	if ( ( lastchar == 'm' ) || ( lastchar == 'M' ) )
		n *= ( 1024 * 1024 );
	if ( ( lastchar == 'g' ) || ( lastchar == 'G' ) )
		n *= ( 1024 * 1024 * 1024 );
	return ( n );
}

/*
 * Just reads a memory position. As you can see, the use_result_dummy() isn't declared
 * inside the function, being global instead. This is because this function will the called
 * several times and the overhead to alocate a long variable would impact in the results
 * of the program.
 */
void use_pointer( void *result ) {
	use_result_dummy += (long)result;
}

/*
 *  Reads the contents into the array. This is the most called part of the program.
 */
long loads( char *array, long range, long stride, long MHz, int delay ) {
	register char **p = 0;	// p contains a memory address
	long	best = 2000000000,	// this is an upper limit for best
		i,
		j = 1,
		time,
		tries;

	fprintf(stderr, "\r%11ld %11ld %11ld", range, stride, range/stride);
	fflush(stderr);

	/* makes i become the largest value possible within the range given the stride */
	i = stride * (range / stride);
	
	/* p will receive the address of the ??*/	
	for (; i >= 0; i -= stride) {
		char	*next;

		p = (char **)&array[i];
		if (i < stride) {
			next = &array[range - stride];
		} else {
			next = &array[i - stride];
		}
		*p = next;
	}

	/*
	calibrator results on a thunderbird and some other hints triggered me to
	slightly modify the calibrator. I stick to the pointer-chasing approach, but
	in addition to the "continuous" version (where each load is immediate
	followed by the next load), the calibrator now also runs a test, where I
	inserted a certain delay (~100 cycles) between two subsequent loads to let
	the cache/memory system "cool down". the point is, that the original version
	rather measures something like the maximum delay caused by latency and
	bandwidth limits --- I call this "replace-time" --- while the new version
	should measure the actual (effective) miss-latency. I hope the new version
	shows some diffrences, especially on the thunderbird and/or duron with their
	extended bus-traffic due to the exclusive caches.
	*/

	/*
	The following code (the HUNDRED more specificaly) do the 100 operations that 
	we talked just above (just memory-register transfer operations)
	*/
	#define	ONE		p = (char **)*p;
	#define	TEN		ONE ONE ONE ONE ONE ONE ONE ONE ONE ONE
	#define	HUNDRED	TEN TEN TEN TEN TEN TEN TEN TEN TEN TEN
	

	/*
	The following code (the HUNDRED more specificaly) do the 100 operations that 
	we talked just above (just simple and fast instructions).
	*/
	#define	FILL	 p++; p--;  p++; p--;  p++; p--;  p++; p--;  p++; p--;
	#define	ONEx	 p = (char **)*p; \
			 FILL FILL FILL FILL FILL FILL FILL FILL FILL FILL

	/*
	If delay is 1, we are looking at analyzing cache throughput, otherwise it´s all about
	cache latency
	*/

	/*
	Analyzing cache throughput...
	We take NUMTRIES tries. A try will be valid only if the total time of the ONEx execution
	is above a MINTIME. If it´s below that limit, we increase the number of loads (fold by
	2). If the time is sufficiently big, we calculate the medium time of each instruction
	*/
	if (delay) {
		for (tries = 0; tries < NUMTRIES; ++tries) {
			i = (j * NUMLOADS) / REDUCE;
			time = now();
			while (i > 0) {
				ONEx
				i -= 1;
    			}
			time = now() - time;
			use_pointer((void *)p);
			if (time <= MINTIME) {
				j *= 2;
				tries--;
			} else {
				time *= REDUCE;
				time /= j;
				if (time < best) {
					best = time;
				}
			}
		}
	}
	/*
	Analyzing cache latency...
	We take NUMTRIES tries. A try will be valid only if the total time of the ONEx execution
	is above a MINTIME. If it´s below that limit, we increase the number of loads (fold by
	2). If the time is sufficiently big, we calculate the medium time of each instruction
	*/
	else {
		for (tries = 0; tries < NUMTRIES; ++tries) {
			i = (j * NUMLOADS);
			time = now();
			/* execute a bulk of memory access instruction */
			while (i > 0) {
				HUNDRED
				i -= 100;
			}
			time = now() - time;
			use_pointer((void *)p);
			if (time <= MINTIME) {
				j *= 2;
				tries--;
			} else {
				time /= j;
				if (time < best) {
					best = time;
				}
			}
		}
	}
        	
	fprintf(stderr, " %11ld %11ld", best*j, best);
	fflush(stderr);

	return best;
}

long** runCache(char *array, long maxrange, long minstride, long MHz, long *maxstride)
{
	long	i,
		last,
		r,
		range = maxrange,
		stride = minstride / 2,
		time = 0,
		x,
		y,
		z,
		**result;
	double	f = 0.25;
	int	delay;

	if (*maxstride) {
		fprintf(stderr, "analyzing cache latency...\n");
	} else {
		fprintf(stderr, "analyzing cache throughput...\n");
	}
	fprintf(stderr, "      range      stride       spots     brutto-  netto-time\n");
	fflush(stderr);

	if (!(*maxstride)) {
		do {
			stride *= 2;
			last = time;
			time = loads(array, range, stride, MHz, 0);
			if (!time)
				ErrXit("runCache: 'loads(%x(array), %ld(range), %ld(stride), %ld(MHz), 0(fp), 0(delay))` returned elapsed time of 0us",
					array, range, stride, MHz);
		} while (((labs(time - last) / (double)time) > EPSILON1) && (stride <= (maxrange / 2)));
		*maxstride = stride;
		delay = 0;
	} else {
		if (*maxstride < 0) {
			*maxstride *= -1;
			delay = 0;
		} else {
			delay = 1;
		}
	}

	for (r = MINRANGE, y = 1; r <= maxrange; r *= 2) {
		for (i = 3; i <= 5; i++) {
			range = r * f * i;
			if ((*maxstride <= range) && (range <= maxrange)) {
				for (stride = *maxstride, x = 1; stride >= minstride; stride /= 2, x++);
				y++;
			}
		}
	}

	if (!(result = (long**)malloc(y * sizeof(long*))))
		ErrXit("runCache: 'result = malloc(%ld)` failed", y * sizeof(long*));
	for (z = 0; z < y; z++) {
		if (!(result[z] = (long*)malloc(x * sizeof(long))))
			ErrXit("runCache: 'result[%ld] = malloc(%ld)` failed", z, x * sizeof(long));
		memset(result[z], 0, x * sizeof(long));
	}
	result[0][0] = (y << 24) | x;

	for (r = MINRANGE, y = 1; r <= maxrange; r *= 2) {
		for (i = 3; i <= 5; i++) {
			range = r * f * i;
			if ((*maxstride <= range) && (range <= maxrange)) {
				result[y][0] = range;
				for (stride = *maxstride, x = 1; stride >= minstride; stride /= 2, x++) {
					if (!result[0][x]) {
						result[0][x] = stride;
					}
					result[y][x] = loads(array, range, stride, MHz, delay);
				}
				y++;
			}
		}
	}

	fprintf(stderr, "\n\n");
	fflush(stderr);

	return result;
}

long** runTLB(char *array, long maxrange, long minstride, long shift, long mincachelines, long MHz, long *maxstride)
{
	long	i,
		last,
		maxspots,
		minspots,
		p,
		pgsz = getpagesize(),
		range = maxrange,
		s = minstride / 2,
		smin,
		spots = mincachelines / 2,
		stride,
		time = 0,
		tmax,
		x,
		xmin,
		y,
		z,
		**result;
	double	f = 0.25;
	int	delay;

	fprintf(stderr, "analyzing TLB latency...\n");
	fprintf(stderr, "      range      stride       spots     brutto-  netto-time\n");
	fflush(stderr);

	if (!(*maxstride)) {
		do {
			s *= 2;
			stride = s + shift;
			range = stride * spots;
			last = time;
			time = loads(array, range, stride, MHz, 0);
			if (!time)
				ErrXit("runTLB: 'loads(%x(array), %ld(range), %ld(stride), %ld(MHz), 0(fp), 0(delay))` returned elapsed time of 0us",
					array, range, stride, MHz);
		} while ((((labs(time - last) / (double)time) > EPSILON1) || (stride < (pgsz / 1))) && (range <= (maxrange / 2)));	
		*maxstride = s;
		delay = 0;
	} else {
		delay = 1;
	}
	minspots = MAX(MINRANGE / (minstride + shift), 4);
	maxspots = maxrange / (*maxstride + shift);	

	for (p = minspots, y = 2; p <= maxspots; p *= 2) {
		for (i = 3; i <= 5; i++) {
			spots = p * f * i;
			if ((spots * (*maxstride + shift)) <= maxrange) {
				for (s = *maxstride, x = 2; s >= minstride; s /= 2, x++);
				y++;
			}
		}
	}
	if (!(result = (long**)malloc(y * sizeof(long*))))
		ErrXit("runTLB: 'result = malloc(%ld)` failed", y * sizeof(long*));
	for (z = 0; z < y; z++) {
		if (!(result[z] = (long*)malloc(x * sizeof(long))))
			ErrXit("runTLB: 'result[%ld] = malloc(%ld)` failed", z, x * sizeof(long*));
		memset(result[z], 0, x * sizeof(long));
	}
	result[0][0] = (y << 24) | x;

	for (p = minspots, y = 2; p <= maxspots; p *= 2) {
		for (i = 3; i <= 5; i++) {
			spots = p * f * i;
			if ((spots * (*maxstride + shift)) <= maxrange) {
				result[y][0] = spots;
				tmax = 0;
				smin = *maxstride + shift;
				xmin = 2;
				for (s = *maxstride, x = 2; s >= minstride; s /= 2, x++) {
					stride = s + shift;
					if (!result[0][x]) {
						result[0][x] = stride;
					}
					range = stride * spots;
					result[y][x] = loads(array, range, stride, MHz, delay);
					if (result[y][x] > tmax) {
						tmax = result[y][x];
						if (stride < smin) {
							smin = stride;
							xmin = x;
						}
					}
				}
				result[y][1] = tmax;
				result[1][xmin]++;
				y++;
			}
		}
	}
	xmin = --x;
	for (--x; x >= 2; x--) {
		if (result[1][x] > result[1][xmin]) {
			xmin = x;
		}
	}
	result[0][1] = result[0][xmin];

	fprintf(stderr, "\n\n");
	fflush(stderr);
	        
	return result;
}

/* Extract information about the cache memory */
cacheInfo* analyzeCache(long **result1, long **result2, long MHz)
{
	long	a,
		diff,
		l,
		lastrange,
		lasttime1,
		lasttime2,
		last[LENPLATEAU],
		level,
		n,
		range,
		stride,
		time1,
		time2,
		x,
		xx,
		y,
		yy;
	cacheInfo	*draft,
			*cache;

	if (!(draft = (cacheInfo*)malloc(4 * sizeof(cacheInfo))))
		ErrXit("analyzeCache: 'draft = malloc(%ld)` failed", 4 * sizeof(cacheInfo));
	if (!(cache = (cacheInfo*)malloc(sizeof(cacheInfo))))
		ErrXit("analyzeCache: 'cache = malloc(%ld)` failed", sizeof(cacheInfo));
	memset(draft, 0, 4 * sizeof(cacheInfo));
	memset(cache, 0, sizeof(cacheInfo));
	
	xx = (result1[0][0] & 0xffffff) - 1;
	yy = (result1[0][0] >> 24) - 1;
	level = 0;
	memset(last, 0, LENPLATEAU * sizeof(last[0]));
	a = LENPLATEAU;
	lastrange = 0;
	lasttime1 = 0;
	lasttime2 = 0;
	for (y = 1; y <= yy ; y++) {
		range = result1[y][0];
		for (x = 1; x <= xx; x++) {
			stride = result1[0][x];
			time1 = result1[y][x];
			time2 = result2[y][x];
			if (draft[1].linesize[level] && last[a] && (range == draft[1].size[level])) {
				if ((labs(time1 - last[a]) / (double)time1) < EPSILON1) {
					draft[0].linesize[level] = stride;
					draft[1].linesize[level] = stride;
				}
			}
			if (draft[2].linesize[level] && last[0] && lastrange && (lastrange == draft[2].size[level])) {
				if ((labs(time1 - last[0]) / (double)time1) < EPSILON1) {
					draft[2].linesize[level] = stride;
					draft[3].linesize[level] = stride;
				} else {
					level++;
					memset(last, 0, LENPLATEAU * sizeof(last[0]));
					a = LENPLATEAU;
				}
			}
			if ((x == 1) && (!draft[2].linesize[level]) && ((last[0] && (fabs(ClocksPerIteration(time1) - ClocksPerIteration(last[LENPLATEAU - 1])) >= EPSILON4)) || (y == yy))) {
				draft[2].linesize[level] = draft[1].linesize[level];
				draft[2].size[level] = lastrange;
				draft[2].latency1[level] = lasttime1;
				draft[2].latency2[level] = lasttime2;
				draft[3].linesize[level] = stride;
				draft[3].size[level] = range;
				draft[3].latency1[level] = time1;
				draft[3].latency2[level] = time2;
				last[0] = time1;
			}
			if ((x == 1) && (a < LENPLATEAU) && (!last[0])) {
				if (fabs(ClocksPerIteration(time1) - ClocksPerIteration(last[LENPLATEAU - 1])) <= EPSILON4) {
					last[--a] = time1;
				} else {
					memset(last, 0, LENPLATEAU * sizeof(last[0]));
					a = LENPLATEAU;
				}
			}
			if ((x == 1) && (a == LENPLATEAU)) {
				last[--a] = time1;
				draft[0].linesize[level] = stride;
				draft[0].size[level] = lastrange;
				draft[0].latency1[level] = lasttime1;
				draft[0].latency2[level] = lasttime2;
				draft[1].linesize[level] = stride;
				draft[1].size[level] = range;
				draft[1].latency1[level] = time1;
				draft[1].latency2[level] = time2;
			}
			if (x == 1) {
				lasttime1 = time1;
				lasttime2 = time2;
			}
		}
		lastrange = range;
	}

	for (l = n = 0 ; n < level; n++) {
		cache->latency1[l] = ((double)(draft[2].latency1[n] + draft[1].latency1[n]) / 2.0);
		cache->latency2[l] = ((double)(draft[2].latency2[n] + draft[1].latency2[n]) / 2.0);
		if ((l == 0) || ((log10(cache->latency1[l]) - log10(cache->latency1[l - 1])) > 0.3)) {
			cache->linesize[l] = draft[1].linesize[n];
			diff = -1;
			for (range = 1; range < result1[1][0]; range *= 2);
			for (y = 1; result1[y][0] < range; y++);
			if (l) {
				int yyy = 1;
				for (; y <= yy; y += yyy) {
					range = result1[y][0];
					if ((draft[2].size[n - 1] <= range) && (range < draft[1].size[n])) {
						if ((y > yyy) && (((result1[y][1]) - (result1[y - yyy][1])) > diff)) {
							diff = (result1[y][1]) - (result1[y - yyy][1]);
							cache->size[l - 1] = range;
						}
						if (((y + yyy) <= yy) && (((result1[y + yyy][1]) - (result1[y][1])) > diff)) {
							diff = (result1[y + yyy][1]) - (result1[y][1]);
							cache->size[l - 1] = range;
						}
					}
				}
			}
			l++;
		}
	}
	cache->size[--l] = draft[3].size[--n];
	cache->levels = l;
	
	free(draft);
	draft = 0;

	return cache;
}



/* Sends some pretty resumed information about the CPU to stdout */
void printCPU( cacheInfo *cache, long MHz, long delay ) {
	FILE	*fp = stdout;
		
	fprintf(fp, "CPU loop + L1 access:    ");
	fprintf(fp, " %6.2f ns = %3f cy\n", NanosecondsPerIteration( cache->latency1[0] ), round(ClocksPerIteration(cache->latency1[0])));
	fprintf(fp, "             ( delay:    ");
	fprintf(fp, " %6.2f ns = %3f cy )\n", NanosecondsPerIteration(delay), round(ClocksPerIteration( delay )));
	fprintf(fp, "\n");
	fflush(fp);
}

/* Extract data about the TLBs */
TLBinfo* analyzeTLB(long **result1, long **result2, long shift, long mincachelines, long MHz)
{
	long	a,
		l,
		lastspots,
		lasttime1,
		lasttime2,
		last[LENPLATEAU],
		level,
		limit = 0,
		n,
		spots,
		stride,
		time1,
		time2,
		x,
		y,
		xx,
		yy;
	double	diff;
	TLBinfo 	*draft,
			*TLB;

	if (!(draft = (TLBinfo*)malloc(4 * sizeof(TLBinfo)))) {
		ErrXit("analyzeCache: 'draft = malloc(%ld)` failed", 4 * sizeof(TLBinfo));
	}
	if (!(TLB = (TLBinfo*)malloc(sizeof(TLBinfo)))) {
		ErrXit("analyzeCache: 'TLB = malloc(%ld)` failed", sizeof(TLBinfo));
	}
	memset(draft, 0, 4 * sizeof(TLBinfo));
	memset(TLB, 0, sizeof(TLBinfo));
	TLB->shift = shift;
	TLB->mincachelines = mincachelines;
	
	xx = (result1[0][0] & 0xffffff) - 1;
	yy = (result1[0][0] >> 24) - 1;
	level = 0;
	memset(last, 0, LENPLATEAU * sizeof(last[0]));
	a = LENPLATEAU;
	lastspots = 0;
	lasttime1 = 0;
	lasttime2 = 0;
	for (y = 2; !limit; y++) {
		spots = result1[y][0];
		limit = (y >= yy) || (spots >= (TLB->mincachelines * 1.25));
		for (x = 1; x <= xx; x++) {
			stride = result1[0][x];
			time1 = result1[y][x];
			time2 = result2[y][x];
			if (draft[1].pagesize[level] && last[a] && (spots == draft[1].entries[level])) {
				if (((labs(time1 - last[a]) / (double)time1) < EPSILON1) || (stride >= result1[0][1])) {
					draft[0].pagesize[level] = stride;
					draft[1].pagesize[level] = stride;
				}
			}
			if (draft[2].pagesize[level] && last[0] && lastspots && (lastspots == draft[2].entries[level])) {
				if (((labs(time1 - last[0]) / (double)time1) < EPSILON1) || (stride >= result1[0][1])) {
					draft[2].pagesize[level] = stride;
					draft[3].pagesize[level] = stride;
					if ( x == xx ) {
						level++;
						memset( last, 0, LENPLATEAU * sizeof( last[0]));
						a = LENPLATEAU;
					}
				} else {
					level++;
					memset(last, 0, LENPLATEAU * sizeof(last[0]));
					a = LENPLATEAU;
				}
			}
			if ((x == 1) && (!draft[2].pagesize[level]) && ((last[0] && (fabs(ClocksPerIteration(time1) - ClocksPerIteration(last[LENPLATEAU - 1])) >= EPSILON4)) || limit)) {
				draft[2].pagesize[level] = draft[1].pagesize[level];
				draft[2].entries[level] = lastspots;
				draft[2].latency1[level] = lasttime1;
				draft[2].latency2[level] = lasttime2;
				draft[3].pagesize[level] = stride;
				draft[3].entries[level] = spots;
				draft[3].latency1[level] = time1;
				draft[3].latency2[level] = time2;
				last[0] = time1;
			}
			if ((x == 1) && (a < LENPLATEAU) && (!last[0])) {
				if (fabs(ClocksPerIteration(time1) - ClocksPerIteration(last[LENPLATEAU - 1])) <= EPSILON4) {
					last[--a] = time1;
				} else {
					memset(last, 0, LENPLATEAU * sizeof(last[0]));
					a = LENPLATEAU;
				}
			}
			if ((x == 1) && (a == LENPLATEAU)) {
				last[--a] = time1;
				draft[0].pagesize[level] = stride;
				draft[0].entries[level] = lastspots;
				draft[0].latency1[level] = lasttime1;
				draft[0].latency2[level] = lasttime2;
				draft[1].pagesize[level] = stride;
				draft[1].entries[level] = spots;
				draft[1].latency1[level] = time1;
				draft[1].latency2[level] = time2;
			}
			if (x == 1) {
				lasttime1 = time1;
				lasttime2 = time2;
			}
		}
		lastspots = spots;
	}

	for (l = n = 0; n < level; n++) {
		TLB->latency1[l] = ((double)(draft[2].latency1[n] + draft[1].latency1[n]) / 2.0);
		TLB->latency2[l] = ((double)(draft[2].latency2[n] + draft[1].latency2[n]) / 2.0);
		if ((l == 0) || (((log10(TLB->latency1[l]) - log10(TLB->latency1[l - 1])) > 0.3) && (draft[2].entries[l] > draft[1].entries[l]))) {
			TLB->pagesize[l] = draft[1].pagesize[n];
			diff = -1.0;
			for (spots = 1; spots < result1[2][0]; spots *= 2);
			for (y = 2; result1[y][0] < spots; y++);
			if (l) {
				int yyy = 1;
				for (; y <= yy; y += yyy) {
					spots = result1[y][0];
					if ((draft[2].entries[n - 1] <= spots) && (spots < draft[1].entries[n])) {
						if ((y > 4) && ((log(result1[y][1]) - log(result1[y - yyy][1])) > diff)) {
							diff = log(result1[y][1]) - log(result1[y - yyy][1]);
							TLB->entries[l - 1] = spots;
						}
						if (((y + yyy) <= yy) && ((log(result1[y + yyy][1]) - log(result1[y][1])) > diff)) {
							diff = log(result1[y + yyy][1]) - log(result1[y][1]);
							TLB->entries[l - 1] = spots;
						}
					}
				}
			}
			l++;
		}
	}
	TLB->entries[--l] = draft[3].entries[--n];
	TLB->levels = l;
	
	free(draft);
	draft = 0;

	return TLB;
}

/* Sends some pretty resumed information about the cache to stdout */
void printCache(cacheInfo *cache, long MHz)
{
	long	l;
	FILE	*fp = stdout;
		
	fprintf(fp, "caches:\n");
	fprintf(fp, "level  size    linesize   miss-latency        replace-time\n");
	/* prints the info about each cache of the system */
	for (l = 0; l < cache->levels; l++) {
		fprintf(fp, "  %1ld   ", l+1);
		/* These 'if-else' are just for beauty */
		if (cache->size[l] >= (1024 * 1024 * 1024)) {
			fprintf(fp, " %3ld GB ", cache->size[l] / (1024 * 1024 * 1024));
		} else {
			if (cache->size[l] >= (1024 * 1024)) {
				fprintf(fp, " %3ld MB ", cache->size[l] / (1024 * 1024));
			} else {
				fprintf(fp, " %3ld KB ", cache->size[l] / 1024);
			}
		}
		fprintf(fp, " %3ld bytes ", cache->linesize[l + 1]);
		fprintf(fp, " %6.2f ns = %3f cy " , NanosecondsPerIteration(cache->latency2[l + 1] - cache->latency2[l]), round(ClocksPerIteration(cache->latency2[l + 1] - cache->latency2[l])));
		fprintf(fp, " %6.2f ns = %3f cy\n", NanosecondsPerIteration(cache->latency1[l + 1] - cache->latency1[l]), round(ClocksPerIteration(cache->latency1[l + 1] - cache->latency1[l])));
	}
	fprintf(fp, "\n");
	fflush(fp);
}

/* Sends some pretty resumed information about the TLBs to stdout */
void printTLB(TLBinfo *TLB, long MHz)
{
	long	l;
	FILE	*fp = stdout;
		
	fprintf(fp, "TLBs:\n");
	fprintf(fp, "level #entries  pagesize  miss-latency");
	fprintf(fp, "\n");
	/* prints the info about each TLB of the system */
	for (l = 0; l < TLB->levels; l++) {
		fprintf(fp, "  %1ld   ", l+1);
		fprintf(fp, "   %3ld   ", TLB->entries[l]);
		/* These 'if-else' are just for beauty */
		if (TLB->pagesize[l + 1] >= (1024 * 1024 * 1024)) {
			fprintf(fp, "  %3ld GB  ", TLB->pagesize[l + 1] / (1024 * 1024 * 1024));
		} else {
			if (TLB->pagesize[l + 1] >= (1024 * 1024)) {
				fprintf(fp, "  %3ld MB  ", TLB->pagesize[l + 1] / (1024 * 1024));
			} else {
				fprintf(fp, "  %3ld KB  ", TLB->pagesize[l + 1] / 1024);
			}
		}
		fprintf(fp, " %6.2f ns = %3.0f cy ", NanosecondsPerIteration(TLB->latency2[l + 1] - TLB->latency2[l]), round(ClocksPerIteration(TLB->latency2[l + 1] - TLB->latency2[l])));
		fprintf(fp, "\n");
	}
	fprintf(fp, "\n");
	fflush(fp);
}

long getMINTIME () {
	long t0 = 0, t1 = 0;
	t0 = t1 = now();
	while ( t0 == t1 ){
		t1 = now();
	}
	return ( t1-t0 );
}

int main(int ac, char **av) {
	long	align = 0,
			delayC,
			delayT,
			maxlinesize,
			maxrange,	// max memory used
			maxCstride = 0,
			maxTstride = 0,
			mincachelines,
			minstride = (long)sizeof(char*),
			MHz,	// processor frequency
			pgsz = getpagesize(),
			y,
			yy,
			**result1,
			**result2;
	char	*array0,
			*array;
	cacheInfo 	*cache;
	TLBinfo 	*TLB;

	fprintf(stdout,"\nCalibrator v%s\n(by Stefan.Manegold@cwi.nl, http://www.cwi.nl/~manegold/)\n", VERSION);
		
	/* Setting up the variables based on command line arguments */
	if (ac == 1) {
		MHz = guess_cpu_frequency();
		maxrange = guess_free_memory();
	} else if (ac < 3) {
		ErrXit("usage: '%s <MHz> <size>[k|M|G]", av[0]);
	} else {
		MHz = atol(av[1]);
		// Why using 25% more memory?
		maxrange = bytes(av[2]) * 1.25;
	}
	fprintf(stdout,"\nMemory to be used: %ld", maxrange);
	fprintf(stdout,"\nCPU frequency: %ld", MHz);
	fprintf(stdout,"\n");

	/* Allocating memory for our test array */
	// Why are we allocating this pgsz extra memory?
	if (!(array0 = (char *)malloc(maxrange+pgsz)))
		ErrXit("main: 'array0 = malloc(%ld)` failed", maxrange+pgsz);

	array = array0;
	/* Prints the array address and size, the page size and how much we are desaligned */
	fprintf(stderr,"%p %ld %ld %5ld\n",array, (long)array, pgsz, (long)array%pgsz);
	
	/* We need to start at the address of a page */
	while (((long)array % pgsz) != align) {
		/* Prints the array address and size, the page size and how much we are desaligned*/
		fprintf(stderr,"\r%p %ld %ld %5ld",array,(long)array,pgsz,(long)array%pgsz);
		fflush(stderr);
		array++;
	}
	/* Prints the array address and size, the page size and how much we are desaligned (0) */
	fprintf(stderr,"\n%p %ld %ld %5ld\n",array,(long)array,pgsz,(long)array % pgsz);
	fprintf(stderr,"\n");

	MINTIME = MAX( MINTIME, 10*getMINTIME() );
	fprintf(stderr,"MINTIME = %ld\n\n",MINTIME);
	fflush(stderr);
	

	/* Gathers data about the cache replace time */
	result1 = runCache(array, maxrange, minstride, MHz, &maxCstride);

	/* Gathers data about the cache miss latency */
	result2 = runCache(array, maxrange, minstride, MHz, &maxCstride);

	/* Given our replace time, miss latency and known processor processor, finds out
	the cache info */
	cache = analyzeCache(result1, result2, MHz);
	mincachelines = ( cache->size[0] && cache->linesize[1] ? cache->size[0] / cache->linesize[1] : 1024 );
	maxlinesize = ( cache->linesize[cache->levels] ? cache->linesize[cache->levels] : maxCstride / 2 );
	delayC = cache->latency2[0] - cache->latency1[0];

	/* Memory is expensive, let's use it rationality */	
	yy = (result1[0][0] >> 24) - 1;
	for (y = 0; y <= yy; y++) {
		free(result1[y]);
		result1[y] = 0;
	}
	free(result1);
	result1 = 0;
	yy = (result2[0][0] >> 24) - 1;
	for (y = 0; y <= yy; y++) {
		free(result2[y]);
		result2[y] = 0;
	}
	free(result2);
	result2 = 0;

	/* Gathering data about the TLB miss latency */
	result1 = runTLB(array, maxrange, 1024, maxlinesize, mincachelines, MHz, &maxTstride);
	result2 = result1;

	
	TLB = analyzeTLB(result1, result2, maxlinesize, mincachelines, MHz);
	delayT = TLB->latency2[0] - TLB->latency1[0];

	/* freeing memory */
	yy = (result1[0][0] >> 24) - 1;
	for (y = 0; y <= yy; y++) {
		free(result1[y]);
		result1[y] = 0;
	}
	free(result1);
	result1 = 0;
	
	fprintf(stdout,"\n");

	/* Prints some useful information */
	printCPU(cache, MHz, delayC);
	printCache(cache, MHz);
	printTLB(TLB, MHz);

	/* once again (and for the last time), freeing memory */
	free(cache);
	cache = 0;
	free(TLB);
	TLB = 0;

	return (0);
}


