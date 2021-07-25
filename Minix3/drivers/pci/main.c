/*
main.c
*/

#include "../drivers.h"

#include <ibm/pci.h>
#include <minix/rs.h>
#include <minix/endpoint.h>

#include "pci.h"

#define NR_DRIVERS	16

PRIVATE struct acl
{
	int inuse;
	struct rs_pci acl;
} acl[NR_DRIVERS];

FORWARD _PROTOTYPE( void do_init, (message *mp)				);
FORWARD _PROTOTYPE( void do_first_dev, (message *mp)			);
FORWARD _PROTOTYPE( void do_next_dev, (message *mp)			);
FORWARD _PROTOTYPE( void do_find_dev, (message *mp)			);
FORWARD _PROTOTYPE( void do_ids, (message *mp)				);
FORWARD _PROTOTYPE( void do_dev_name, (message *mp)			);
FORWARD _PROTOTYPE( void do_dev_name_s, (message *mp)			);
FORWARD _PROTOTYPE( void do_slot_name_s, (message *mp)			);
FORWARD _PROTOTYPE( void do_set_acl, (message *mp)			);
FORWARD _PROTOTYPE( void do_del_acl, (message *mp)			);
FORWARD _PROTOTYPE( void do_reserve, (message *mp)			);
FORWARD _PROTOTYPE( void do_attr_r8, (message *mp)			);
FORWARD _PROTOTYPE( void do_attr_r16, (message *mp)			);
FORWARD _PROTOTYPE( void do_attr_r32, (message *mp)			);
FORWARD _PROTOTYPE( void do_attr_w8, (message *mp)			);
FORWARD _PROTOTYPE( void do_attr_w16, (message *mp)			);
FORWARD _PROTOTYPE( void do_attr_w32, (message *mp)			);
FORWARD _PROTOTYPE( void do_rescan_bus, (message *mp)			);
FORWARD _PROTOTYPE( void reply, (message *mp, int result)		);
FORWARD _PROTOTYPE( struct rs_pci *find_acl, (int endpoint)		);

extern int debug;

int main(void)
{
	int i, r;
	message m;

	pci_init();

	for(;;)
	{
		r= receive(ANY, &m);
		if (r < 0)
		{
			printf("PCI: receive from ANY failed: %d\n", r);
			break;
		}

		if (is_notify(m.m_type)) {
			switch (_ENDPOINT_P(m.m_source)) {
				case PM_PROC_NR:
					break;
				default:
					printf("PCI: got notify from %d\n",
								m.m_source);
					break;
			}

			/* done, get a new message */
			continue;
		}

		switch(m.m_type)
		{
		case BUSC_PCI_INIT: do_init(&m); break;
		case BUSC_PCI_FIRST_DEV: do_first_dev(&m); break;
		case BUSC_PCI_NEXT_DEV: do_next_dev(&m); break;
		case BUSC_PCI_FIND_DEV: do_find_dev(&m); break;
		case BUSC_PCI_IDS: do_ids(&m); break;
		case BUSC_PCI_DEV_NAME: do_dev_name(&m); break;
		case BUSC_PCI_RESERVE: do_reserve(&m); break;
		case BUSC_PCI_ATTR_R8: do_attr_r8(&m); break;
		case BUSC_PCI_ATTR_R16: do_attr_r16(&m); break;
		case BUSC_PCI_ATTR_R32: do_attr_r32(&m); break;
		case BUSC_PCI_ATTR_W8: do_attr_w8(&m); break;
		case BUSC_PCI_ATTR_W16: do_attr_w16(&m); break;
		case BUSC_PCI_ATTR_W32: do_attr_w32(&m); break;
		case BUSC_PCI_RESCAN: do_rescan_bus(&m); break;
		case BUSC_PCI_DEV_NAME_S: do_dev_name_s(&m); break;
		case BUSC_PCI_SLOT_NAME_S: do_slot_name_s(&m); break;
		case BUSC_PCI_SET_ACL: do_set_acl(&m); break;
		case BUSC_PCI_DEL_ACL: do_del_acl(&m); break;
		default:
			printf("PCI: got message from %d, type %d\n",
				m.m_source, m.m_type);
			break;
		}
	}

	return 0;
}

PRIVATE void do_init(mp)
message *mp;
{
	int r;

#if DEBUG
	printf("PCI: pci_init: called by '%d'\n", mp->m_source);
#endif

	mp->m_type= 0;
	r= send(mp->m_source, mp);
	if (r != 0)
		printf("PCI: do_init: unable to send to %d: %d\n",
			mp->m_source, r);
}

PRIVATE void do_first_dev(mp)
message *mp;
{
	int i, r, devind;
	u16_t vid, did;
	struct rs_pci *aclp;

	aclp= find_acl(mp->m_source);

	if (!aclp && debug)
		printf("PCI: do_first_dev: no acl for caller %d\n",
			mp->m_source);

	r= pci_first_dev_a(aclp, &devind, &vid, &did);
	if (r == 1)
	{
		mp->m1_i1= devind;
		mp->m1_i2= vid;
		mp->m1_i3= did;
	}
	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_first_dev: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_next_dev(mp)
message *mp;
{
	int r, devind;
	u16_t vid, did;
	struct rs_pci *aclp;

	devind= mp->m1_i1;
	aclp= find_acl(mp->m_source);

	r= pci_next_dev_a(aclp, &devind, &vid, &did);
	if (r == 1)
	{
		mp->m1_i1= devind;
		mp->m1_i2= vid;
		mp->m1_i3= did;
	}
	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_next_dev: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_find_dev(mp)
message *mp;
{
	int r, devind;
	u8_t bus, dev, func;

	bus= mp->m1_i1;
	dev= mp->m1_i2;
	func= mp->m1_i3;

	r= pci_find_dev(bus, dev, func, &devind);
	if (r == 1)
		mp->m1_i1= devind;
	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_find_dev: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_ids(mp)
message *mp;
{
	int r, devind;
	u16_t vid, did;

	devind= mp->m1_i1;

	r= pci_ids_s(devind, &vid, &did);
	if (r != OK)
	{
		printf("pci:do_ids: failed for devind %d: %d\n",
			devind, r);
	}

	mp->m1_i1= vid;
	mp->m1_i2= did;
	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_ids: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_dev_name(mp)
message *mp;
{
	int r, name_len, len;
	u16_t vid, did;
	char *name_ptr, *name;

	vid= mp->m1_i1;
	did= mp->m1_i2;
	name_len= mp->m1_i3;
	name_ptr= mp->m1_p1;

	name= pci_dev_name(vid, did);
	if (name == NULL)
	{
		/* No name */
		r= ENOENT;
	}
	else
	{
		len= strlen(name)+1;
		if (len > name_len)
			len= name_len;
		printf("PCI: pci`do_dev_name: calling do_vircopy\n");
		r= sys_vircopy(SELF, D, (vir_bytes)name, mp->m_source, D,
			(vir_bytes)name_ptr, len);
	}

	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_dev_name: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_dev_name_s(mp)
message *mp;
{
	int r, name_len, len;
	u16_t vid, did;
	cp_grant_id_t name_gid;
	char *name;

	vid= mp->m7_i1;
	did= mp->m7_i2;
	name_len= mp->m7_i3;
	name_gid= mp->m7_i4;

	name= pci_dev_name(vid, did);
	if (name == NULL)
	{
		/* No name */
		r= ENOENT;
	}
	else
	{
		len= strlen(name)+1;
		if (len > name_len)
			len= name_len;
		r= sys_safecopyto(mp->m_source, name_gid, 0, (vir_bytes)name,
			len, D);
	}

	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_dev_name: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_slot_name_s(mp)
message *mp;
{
	int r, devind, name_len, len;
	cp_grant_id_t gid;
	char *name;

	devind= mp->m1_i1;
	name_len= mp->m1_i2;
	gid= mp->m1_i3;

	r= pci_slot_name_s(devind, &name);
	if (r != OK)
	{
		printf("pci:do_slot_name_s: failed for devind %d: %d\n",
			devind, r);
	}

	if (r == OK)
	{
		len= strlen(name)+1;
		if (len > name_len)
			len= name_len;
		r= sys_safecopyto(mp->m_source, gid, 0,
			(vir_bytes)name, len, D);
	}

	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("PCI: do_slot_name: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_set_acl(mp)
message *mp;
{
	int i, r, gid;

	if (mp->m_source != RS_PROC_NR)
	{
		printf("PCI: do_set_acl: not from RS\n");
		reply(mp, EPERM);
		return;
	}

	for (i= 0; i<NR_DRIVERS; i++)
	{
		if (!acl[i].inuse)
			break;
	}
	if (i >= NR_DRIVERS)
	{
		printf("PCI: do_set_acl: table is full\n");
		reply(mp, ENOMEM);
		return;
	}

	gid= mp->m1_i1;

	r= sys_safecopyfrom(mp->m_source, gid, 0, (vir_bytes)&acl[i].acl,
		sizeof(acl[i].acl), D);
	if (r != OK)
	{
		printf("PCI: do_set_acl: safecopyfrom failed\n");
		reply(mp, r);
		return;
	}
	acl[i].inuse= 1;
	if(debug)
	  printf("PCI: do_acl: setting ACL for %d ('%s') at entry %d\n",
		acl[i].acl.rsp_endpoint, acl[i].acl.rsp_label,
		i);

	reply(mp, OK);
}

PRIVATE void do_del_acl(mp)
message *mp;
{
	int i, r, proc_nr;

	if (mp->m_source != RS_PROC_NR)
	{
		printf("do_del_acl: not from RS\n");
		reply(mp, EPERM);
		return;
	}

	proc_nr= mp->m1_i1;

	for (i= 0; i<NR_DRIVERS; i++)
	{
		if (!acl[i].inuse)
			continue;
		if (acl[i].acl.rsp_endpoint == proc_nr)
			break;
	}

	if (i >= NR_DRIVERS)
	{
		printf("do_del_acl: nothing found for %d\n", proc_nr);
		reply(mp, EINVAL);
		return;
	}

	acl[i].inuse= 0;
#if 0
	printf("do_acl: deleting ACL for %d ('%s') at entry %d\n",
		acl[i].acl.rsp_endpoint, acl[i].acl.rsp_label, i);
#endif

	/* Also release all devices held by this process */
	pci_release(proc_nr);

	reply(mp, OK);
}

PRIVATE void do_reserve(mp)
message *mp;
{
	int i, r, devind;

	devind= mp->m1_i1;
	
	mp->m_type= pci_reserve2(devind, mp->m_source);
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_reserve: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_attr_r8(mp)
message *mp;
{
	int r, devind, port;
	u8_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;

	r= pci_attr_r8_s(devind, port, &v);
	if (r != OK)
	{
		printf(
		"pci:do_attr_r8: pci_attr_r8_s(%d, %d, ...) failed: %d\n",
			devind, port, r);
	}
	mp->m2_l1= v;
	mp->m_type= r;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_r8: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_attr_r16(mp)
message *mp;
{
	int r, devind, port;
	u32_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;

	v= pci_attr_r16(devind, port);
	mp->m2_l1= v;
	mp->m_type= OK;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_r16: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_attr_r32(mp)
message *mp;
{
	int r, devind, port;
	u32_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;

	r= pci_attr_r32_s(devind, port, &v);
	if (r != OK)
	{
		printf(
		"pci:do_attr_r32: pci_attr_r32_s(%d, %d, ...) failed: %d\n",
			devind, port, r);
	}
	mp->m2_l1= v;
	mp->m_type= OK;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_r32: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_attr_w8(mp)
message *mp;
{
	int r, devind, port;
	u8_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;
	v= mp->m2_l1;

	pci_attr_w8(devind, port, v);
	mp->m_type= OK;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_w8: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_attr_w16(mp)
message *mp;
{
	int r, devind, port;
	u16_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;
	v= mp->m2_l1;

	pci_attr_w16(devind, port, v);
	mp->m_type= OK;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_w16: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_attr_w32(mp)
message *mp;
{
	int r, devind, port;
	u32_t v;

	devind= mp->m2_i1;
	port= mp->m2_i2;
	v= mp->m2_l1;

	pci_attr_w32(devind, port, v);
	mp->m_type= OK;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_attr_w32: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}

PRIVATE void do_rescan_bus(mp)
message *mp;
{
	int r, busnr;

	busnr= mp->m2_i1;

	pci_rescan_bus(busnr);
	mp->m_type= OK;
	r= send(mp->m_source, mp);
	if (r != 0)
	{
		printf("do_rescan_bus: unable to send to %d: %d\n",
			mp->m_source, r);
	}
}


PRIVATE void reply(mp, result)
message *mp;
int result;
{
	int r;
	message m;

	m.m_type= result;
	r= send(mp->m_source, &m);
	if (r != 0)
		printf("reply: unable to send to %d: %d\n", mp->m_source, r);
}


PRIVATE struct rs_pci *find_acl(endpoint)
int endpoint;
{
	int i;

	/* Find ACL entry for caller */
	for (i= 0; i<NR_DRIVERS; i++)
	{
		if (!acl[i].inuse)
			continue;
		if (acl[i].acl.rsp_endpoint == endpoint)
			return &acl[i].acl;
	}
	return NULL;
}
