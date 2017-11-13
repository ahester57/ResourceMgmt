#include <stdlib.h>
#include <stdio.h>
#include "osstypes.h"
#include "deadlock.h"

// check if request from pnum <= available
int
requestavailable(resource_table* restable, const int req,
		 const int pnum, const int resnum)
{
	resource_dt res = restable->table[resnum];
	if (req <= res.available) {
		return 1;
	}
	return 0;
}

// check if deadlock state
int
deadlock(resource_table* restable, const int m, const int n)
{
	int work[m];	// m resources
	int finish[n];	// n processes

	int i;
	for (i = 0; i < m; i++) {
		resource_dt res = restable->table[i];
		work[i] = res.available;
	}
	for (i = 0; i < n; i++) {
		finish[i] = 0;
	}

	int p;
	for (p = 0; p < n; p++) {
		if (finish[p])
			continue;
		if (requestavailable(restable, 1, p, i) ) {
			finish[p] = 1;
			for (i = 0; i < m; i++) {
				resource_dt res = restable->table[i];
				work[i] += res.allocation[p];
			}
			p = -1;
		}
	}
	
	for (p = 0; p < n; p++) {
		if (!finish[p]) 
			break;
	}
	return (p != n);
}

// adds a request for a resource from pnum
int
requestresource(resource_table* table, const int reqnum,
		const unsigned int pnum)
{
	resource_dt* tab = table->table;
	resource_dt res = tab[reqnum]; 
	int index = findavailableslot(res.requests);
	if (index == -1)
		return -1;
	res.requests[index] = pnum;
	table->table[reqnum] = res;
	return 1;
}

// checks if it is allocated the resource
int
findinallocated(resource_table* table, const int reqnum,
		const unsigned int pnum)
{
	resource_dt* tab = table->table;
	resource_dt res = tab[reqnum]; 
	unsigned int* list = res.allocation;
	int i;
	for (i = 0; i < MAXPROCESSES; i++) {
		if (list[i] == pnum) {
			return 1;
		}
	}
	return -1;
}

// returns first available slot in given list
int
findavailableslot(unsigned int* list)
{
	int i;
	for (i = 0; i < MAXPROCESSES; i++) {
		if (list[i] == -1) {
			return i;
		}
	}
	return -1;
}
