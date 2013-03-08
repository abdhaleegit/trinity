#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "trinity.h"	// page_size
#include "files.h"
#include "arch.h"
#include "sanitise.h"
#include "syscall.h"
#include "net.h"
#include "log.h"
#include "maps.h"
#include "shm.h"

/*
 * This function gets called before we call ->sanitise routines.
 */
unsigned long get_reg(void)
{
	if ((rand() % 2) == 0)
		return random();

	return get_interesting_value();
}

static unsigned int get_cpu(void)
{
	int i;
	i = rand() % 3;

	switch (i) {
	case 0: return -1;
	case 1: return rand() & 4095;
	case 2: return rand() & 15;
	default:
		BUG("unreachable!\n");
		break;
	}
	return 0;
}


static unsigned long fill_arg(int childno, int call, int argnum)
{
	unsigned long i;
	unsigned long mask = 0;
	unsigned long low = 0, high = 0;
	unsigned long addr = 0;
	unsigned int bits;
	unsigned int num = 0;
	const unsigned int *values = NULL;
	enum argtype argtype = 0;
	unsigned long sockaddr = 0, sockaddrlen = 0;

	switch (argnum) {
	case 1:	argtype = syscalls[call].entry->arg1type;
		break;
	case 2:	argtype = syscalls[call].entry->arg2type;
		break;
	case 3:	argtype = syscalls[call].entry->arg3type;
		break;
	case 4:	argtype = syscalls[call].entry->arg4type;
		break;
	case 5:	argtype = syscalls[call].entry->arg5type;
		break;
	case 6:	argtype = syscalls[call].entry->arg6type;
		break;
	default:
		BUG("unreachable!\n");
		return 0;
	}

	switch (argtype) {
	case ARG_UNDEFINED:
	case ARG_RANDOM_INT:
		return (unsigned long)rand();

	case ARG_FD:
		return get_random_fd();
	case ARG_LEN:
		return (unsigned long)get_len();

	case ARG_ADDRESS:
		if ((rand() % 2) == 0)
			return (unsigned long)get_address();

		/* Half the time, we look to see if earlier args were also ARG_ADDRESS,
		 * and munge that instead of returning a new one from get_address() */

		addr = find_previous_arg_address(argnum, call, childno);

		switch (rand() % 4) {
		case 0:	break;	/* return unmodified */
		case 1:	addr++;
			break;
		case 2:	addr+= sizeof(int);
			break;
		case 3:	addr+= sizeof(long);
			break;
		default: BUG("unreachable!\n");
			break;
		}

		return addr;

	case ARG_NON_NULL_ADDRESS:
		return (unsigned long)get_non_null_address();
	case ARG_PID:
		return (unsigned long)get_pid();
	case ARG_RANGE:
		switch (argnum) {
		case 1:	low = syscalls[call].entry->low1range;
			high = syscalls[call].entry->hi1range;
			break;
		case 2:	low = syscalls[call].entry->low2range;
			high = syscalls[call].entry->hi2range;
			break;
		case 3:	low = syscalls[call].entry->low3range;
			high = syscalls[call].entry->hi3range;
			break;
		case 4:	low = syscalls[call].entry->low4range;
			high = syscalls[call].entry->hi4range;
			break;
		case 5:	low = syscalls[call].entry->low5range;
			high = syscalls[call].entry->hi5range;
			break;
		case 6:	low = syscalls[call].entry->low6range;
			high = syscalls[call].entry->hi6range;
			break;
		default:
			BUG("Should never happen.\n");
			break;
		}

		if (high == 0) {
			printf("%s forgets to set hirange!\n", syscalls[call].entry->name);
			BUG("Fix syscall definition!\n");
			return 0;
		}

		i = random() % high;
		if (i < low) {
			i += low;
			i &= high;
		}
		return i;

	case ARG_OP:	/* Like ARG_LIST, but just a single value. */
		switch (argnum) {
		case 1:	num = syscalls[call].entry->arg1list.num;
			values = syscalls[call].entry->arg1list.values;
			break;
		case 2:	num = syscalls[call].entry->arg2list.num;
			values = syscalls[call].entry->arg2list.values;
			break;
		case 3:	num = syscalls[call].entry->arg3list.num;
			values = syscalls[call].entry->arg3list.values;
			break;
		case 4:	num = syscalls[call].entry->arg4list.num;
			values = syscalls[call].entry->arg4list.values;
			break;
		case 5:	num = syscalls[call].entry->arg5list.num;
			values = syscalls[call].entry->arg5list.values;
			break;
		case 6:	num = syscalls[call].entry->arg6list.num;
			values = syscalls[call].entry->arg6list.values;
			break;
		default: break;
		}
		mask |= values[rand() % num];
		return mask;

	case ARG_LIST:
		switch (argnum) {
		case 1:	num = syscalls[call].entry->arg1list.num;
			values = syscalls[call].entry->arg1list.values;
			break;
		case 2:	num = syscalls[call].entry->arg2list.num;
			values = syscalls[call].entry->arg2list.values;
			break;
		case 3:	num = syscalls[call].entry->arg3list.num;
			values = syscalls[call].entry->arg3list.values;
			break;
		case 4:	num = syscalls[call].entry->arg4list.num;
			values = syscalls[call].entry->arg4list.values;
			break;
		case 5:	num = syscalls[call].entry->arg5list.num;
			values = syscalls[call].entry->arg5list.values;
			break;
		case 6:	num = syscalls[call].entry->arg6list.num;
			values = syscalls[call].entry->arg6list.values;
			break;
		default: break;
		}
		bits = rand() % num;	/* num of bits to OR */
		for (i=0; i<bits; i++)
			mask |= values[rand() % num];
		return mask;

	case ARG_RANDPAGE:
		if ((rand() % 2) == 0)
			return (unsigned long) page_allocs;
		else
			return (unsigned long) page_rand;

	case ARG_CPU:
		return (unsigned long) get_cpu();

	case ARG_PATHNAME:
		if ((rand() % 100) > 10) {
fallback:
			return (unsigned long) get_filename();
		} else {
			/* Create a bogus filename with junk at the end of an existing one. */
			char *pathname = get_filename();
			char *suffix;
			int len = strlen(pathname);

			suffix = malloc(page_size);
			if (suffix == NULL)
				goto fallback;

			generate_random_page(suffix);

			(void) strcat(pathname, suffix);
			if ((rand() % 2) == 0)
				pathname[len] = '/';
			return (unsigned long) pathname;
		}

	case ARG_IOVEC:
		i = (rand() % 4) + 1;

		switch (argnum) {
		case 1:	if (syscalls[call].entry->arg2type == ARG_IOVECLEN)
				shm->a2[childno] = i;
			break;
		case 2:	if (syscalls[call].entry->arg3type == ARG_IOVECLEN)
				shm->a3[childno] = i;
			break;
		case 3:	if (syscalls[call].entry->arg4type == ARG_IOVECLEN)
				shm->a4[childno] = i;
			break;
		case 4:	if (syscalls[call].entry->arg5type == ARG_IOVECLEN)
				shm->a5[childno] = i;
			break;
		case 5:	if (syscalls[call].entry->arg6type == ARG_IOVECLEN)
				shm->a6[childno] = i;
			break;
		case 6:
		default: BUG("impossible\n");
		}
		return (unsigned long) alloc_iovec(i);

	case ARG_IOVECLEN:
	case ARG_SOCKADDRLEN:
		switch (argnum) {
		case 1:	return(shm->a1[childno]);
		case 2:	return(shm->a2[childno]);
		case 3:	return(shm->a3[childno]);
		case 4:	return(shm->a4[childno]);
		case 5:	return(shm->a5[childno]);
		case 6:	return(shm->a6[childno]);
		default: break;
		}
		;; // fallthrough

	case ARG_SOCKADDR:
		generate_sockaddr(&sockaddr, &sockaddrlen, PF_NOHINT);

		switch (argnum) {
		case 1:	if (syscalls[call].entry->arg2type == ARG_SOCKADDRLEN)
				shm->a2[childno] = sockaddrlen;
			break;
		case 2:	if (syscalls[call].entry->arg3type == ARG_SOCKADDRLEN)
				shm->a3[childno] = sockaddrlen;
			break;
		case 3:	if (syscalls[call].entry->arg4type == ARG_SOCKADDRLEN)
				shm->a4[childno] = sockaddrlen;
			break;
		case 4:	if (syscalls[call].entry->arg5type == ARG_SOCKADDRLEN)
				shm->a5[childno] = sockaddrlen;
			break;
		case 5:	if (syscalls[call].entry->arg6type == ARG_SOCKADDRLEN)
				shm->a6[childno] = sockaddrlen;
			break;
		case 6:
		default: BUG("impossible\n");
		}
		return (unsigned long) sockaddr;


	default:
		BUG("unreachable!\n");
		return 0;
	}

	BUG("unreachable!\n");
	return 0x5a5a5a5a;	/* Should never happen */
}

void generic_sanitise(int childno)
{
	unsigned int call = shm->syscallno[childno];

	if (syscalls[call].entry->arg1type != 0)
		shm->a1[childno] = fill_arg(childno, call, 1);
	if (syscalls[call].entry->arg2type != 0)
		shm->a2[childno] = fill_arg(childno, call, 2);
	if (syscalls[call].entry->arg3type != 0)
		shm->a3[childno] = fill_arg(childno, call, 3);
	if (syscalls[call].entry->arg4type != 0)
		shm->a4[childno] = fill_arg(childno, call, 4);
	if (syscalls[call].entry->arg5type != 0)
		shm->a5[childno] = fill_arg(childno, call, 5);
	if (syscalls[call].entry->arg6type != 0)
		shm->a6[childno] = fill_arg(childno, call, 6);
}
