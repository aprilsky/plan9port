/*
 * No idea whether this will work.  It does compile.
 */

#include <u.h>
#include <kvm.h>
#include <nlist.h>
#include <sys/types.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/dkstat.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <libc.h>
#include <bio.h>
#include "dat.h"

void xloadavg(int);
void xcpu(int);
void xswap(int);
void xsysctl(int);
void xnet(int);
void xkvm(int);

void (*statfn[])(int) =
{
	xkvm,
	xloadavg,
	xswap,
	xcpu,
	xsysctl,
	xnet,
	0
};

static kvm_t *kvm;

static struct nlist nl[] = {
	{ "_ifnet" },
	{ "_cp_time" },
	{ "" },
};

void
kvminit(void)
{
	char buf[_POSIX2_LINE_MAX];

	if(kvm)
		return;
	kvm = kvm_openfiles(nil, nil, nil, OREAD, buf);
	if(kvm == nil)
		return;
	if(kvm_nlist(kvm, nl) < 0 || nl[0].n_type == 0){
		kvm = nil;
		return;
	}
}

void
xkvm(int first)
{
	if(first)
		kvminit();
}

int
kread(ulong addr, char *buf, int size)
{
	if(kvm_read(kvm, addr, buf, size) != size){
		memset(buf, 0, size);
		return -1;
	}
	return size;
}

void
xnet(int first)
{
	ulong out, in, outb, inb, err;
	static ulong ifnetaddr;
	ulong addr;
	struct ifnet ifnet;
	struct ifnethead ifnethead;
	char name[16];

	if(first)
		return;

	if(ifnetaddr == 0){
		ifnetaddr = nl[0].n_value;
		if(ifnetaddr == 0)
			return;
	}

	if(kread(ifnetaddr, (char*)&ifnethead, sizeof ifnethead) < 0)
		return;

	out = in = outb = inb = err = 0;
	addr = (ulong)TAILQ_FIRST(&ifnethead);
	while(addr){
		if(kread(addr, (char*)&ifnet, sizeof ifnet) < 0
		|| kread((ulong)ifnet.if_name, name, 16) < 0)
			return;
		name[15] = 0;
		addr = (ulong)TAILQ_NEXT(&ifnet, if_link);
		out += ifnet.if_opackets;
		in += ifnet.if_ipackets;
		outb += ifnet.if_obytes;
		inb += ifnet.if_ibytes;
		err += ifnet.if_oerrors+ifnet.if_ierrors;
	}
	Bprint(&bout, "etherin %lud\n", in);
	Bprint(&bout, "etherout %lud\n", out);
	Bprint(&bout, "etherinb %lud\n", inb);
	Bprint(&bout, "etheroutb %lud\n", outb);
	Bprint(&bout, "ethererr %lud\n", err);
	Bprint(&bout, "ether %lud\n", in+out);
	Bprint(&bout, "etherb %lud\n", inb+outb);
}


int
rsys(char *name, char *buf, int len)
{
	size_t l;

	l = len;
	if(sysctlbyname(name, buf, &l, nil, 0) < 0)
		return -1;
	buf[l] = 0;
	return l;
}

vlong
isys(char *name)
{
	ulong u;
	size_t l;

	l = sizeof u;
	if(sysctlbyname(name, &u, &l, nil, 0) < 0)
		return 0;
	return u;
}

void
xsysctl(int first)
{
	static int pgsize;
	vlong t;

	if(first){
		pgsize = isys("vm.stats.vm.v_page_size");
		if(pgsize == 0)
			pgsize = 4096;
	}
	if((t = isys("vm.stats.vm.v_page_count")) != 0)
		Bprint(&bout, "mem %lld %lld\n", 
			isys("vm.stats.vm.v_active_count")*pgsize, 
			t*pgsize);
	Bprint(&bout, "context %lld 1000\n", isys("vm.stats.sys.v_swtch"));
	Bprint(&bout, "syscall %lld 1000\n", isys("vm.stats.sys.v_syscall"));
	Bprint(&bout, "intr %lld 1000\n", isys("vm.stats.sys.v_intr")+isys("vm.stats.sys.v_trap"));
	Bprint(&bout, "fault %lld 1000\n", isys("vm.stats.vm.v_vm_faults"));
	Bprint(&bout, "fork %lld 1000\n", isys("vm.stats.vm.v_forks")
		+isys("vm.stats.vm.v_rforks")
		+isys("vm.stats.vm.v_vforks"));
}

void
xcpu(int first)
{
#if 0
	static int stathz;
	ulong x[20];
	struct clockinfo *ci;
	int n;

	if(first){
		if(rsys("kern.clockrate", (char*)&x, sizeof x) < sizeof ci)
			stathz = 128;
		else{
			ci = (struct clockinfo*)x;
			stathz = ci->stathz;
		}
		return;
	}

	if((n=rsys("kern.cp_time", (char*)x, sizeof x)) < 5*sizeof(ulong))
		return;

	Bprint(&bout, "user %lud %d\n", x[CP_USER]+x[CP_NICE], stathz);
	Bprint(&bout, "sys %lud %d\n", x[CP_SYS], stathz);
	Bprint(&bout, "cpu %lud %d\n", x[CP_USER]+x[CP_NICE]+x[CP_SYS], stathz);
	Bprint(&bout, "idle %lud %d\n", x[CP_IDLE], stathz);
#endif
}

void
xloadavg(int first)
{
	double l[3];

	if(first)
		return;

	if(getloadavg(l, 3) < 0)
		return;
	Bprint(&bout, "load %d 1000\n", (int)(l[0]*1000.0));
}

void
xswap(int first)
{
#if 0
	static struct kvm_swap s;
	static ulong pgin, pgout;
	int i, o;
	static int pgsize;

	if(first){
		pgsize = getpagesize();
		if(pgsize == 0)
			pgsize = 4096;
		return;
	}

	if(kvm == nil)
		return;

	i = isys("vm.stats.vm.v_swappgsin");
	o = isys("vm.stats.vm.v_swappgsout");
	if(i != pgin || o != pgout){
		pgin = i;
		pgout = o;
		kvm_getswapinfo(kvm, &s, 1, 0);
	}


	Bprint(&bout, "swap %lld %lld\n", s.ksw_used*(vlong)pgsize, s.ksw_total*(vlong)pgsize);
#endif
}

