#ifndef DEADLOCK_H_
#define DEADLOCK_H_


int requestavailable(resource_table* restable, const int req,
			 const int pnum, const int resnum);
int deadlock(resource_table* restable, const int m, const int n);
int requestresource(resource_table* table, const int reqnum,
			const unsigned int pnum);
int findinallocated(resource_table* table, const int reqnum,
			const unsigned int pnum);
int findavailableslot(unsigned int* list);

#endif
