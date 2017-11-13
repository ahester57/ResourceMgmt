#ifndef DEADLOCK_H_
#define DEADLOCK_H_


int requestavailable(const resource_table* restable, const int req,
			 const int pnum, const int resnum);
int deadlock(const resource_table* restable, int m, int n);
int requestresource(resource_table* table, int reqnum, unsigned int pnum);
int findavailableslot(unsigned int* list);

#endif
