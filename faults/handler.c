
#include <assert.h>
#include <stdio.h>
#include <sel4/sel4.h>
#include <utils/util.h>
#include <autoconf.h>

#define FAULTER_BADGE_VALUE     (0xBEEF)
#define PROGNAME                "Handler: "
/* We signal on this notification to let the fauler know when we're ready to
 * receive its fault message.
 */
extern seL4_CPtr sequencing_ep_cap;

extern seL4_CPtr faulter_fault_ep_cap;

extern seL4_CPtr handler_cspace_root;
extern seL4_CPtr badged_faulter_fault_ep_cap;

extern seL4_CPtr faulter_tcb_cap;
extern seL4_CPtr faulter_vspace_root;
extern seL4_CPtr faulter_cspace_root;

int main(void)
{
    int error;
    seL4_Word tmp_badge;
    seL4_CPtr foreign_badged_faulter_empty_slot_cap;
    seL4_CPtr foreign_faulter_capfault_cap;
    seL4_MessageInfo_t seq_msginfo;

    printf(PROGNAME "Handler thread running!\n"
           PROGNAME "About to wait for empty slot from faulter.\n");


    seq_msginfo = seL4_Recv(sequencing_ep_cap, &tmp_badge);
    foreign_badged_faulter_empty_slot_cap = seL4_GetMR(0);
    printf(PROGNAME "Received init sequence msg: slot in faulter's cspace is "
           "%lu.\n",
           foreign_badged_faulter_empty_slot_cap);



    /* Mint the fault ep with a badge */

    error = seL4_CNode_Mint(
        handler_cspace_root,
        badged_faulter_fault_ep_cap,
        seL4_WordBits,
        handler_cspace_root,
        faulter_fault_ep_cap,
        seL4_WordBits,
        seL4_AllRights, 
        FAULTER_BADGE_VALUE
    );

    ZF_LOGF_IF(error != 0, PROGNAME "Failed to mint ep cap with badge!");
    printf(PROGNAME "Successfully minted fault handling ep into local cspace.\n");


    /* This step is only necessary on the master kernel. On the MCS kernel it
     * can be skipped because we do not need to copy the badged fault EP into
     * the faulting thread's cspace on the MCS kernel.
     */

    // put fault endpoint in faulter's cspace
    error = seL4_CNode_Copy(
        faulter_cspace_root,
        foreign_badged_faulter_empty_slot_cap,
        seL4_WordBits,
        handler_cspace_root,
        badged_faulter_fault_ep_cap,
        seL4_WordBits,
        seL4_AllRights
    );

    ZF_LOGF_IF(error != 0, PROGNAME "Failed to copy badged ep cap into faulter's cspace!");
    printf(PROGNAME "Successfully copied badged fault handling ep into "
           "faulter's cspace.\n"
           PROGNAME "(Only necessary on Master kernel.)\n");

    // set faulter TCB fault endpoint
    error = seL4_TCB_SetSpace(
        faulter_tcb_cap,
        foreign_badged_faulter_empty_slot_cap,
        faulter_cspace_root,
        0,
        faulter_vspace_root,
        0
    );
    ZF_LOGF_IF(error != 0, PROGNAME "Failed to set TCB fault endpoint!");
    printf(PROGNAME "Successfully set faulter's TCB fault endpoint\n");

    // resume faulter
    seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));
    printf(PROGNAME "Resumed faulter\n");

    // await a fault
    seq_msginfo = seL4_Recv(faulter_fault_ep_cap, &tmp_badge);
    printf(PROGNAME "Received a fault\n");
    
    // make sure it's from the badged faulter
    assert(tmp_badge == FAULTER_BADGE_VALUE);

    // ASSUMPTION - this was a capability fault which was caused by accessing an empty cap

    // get the empty cap slot
    foreign_faulter_capfault_cap = seL4_GetMR(seL4_CapFault_Addr);
    printf(PROGNAME "Got faulty cap slot: %p\n", foreign_faulter_capfault_cap);


    // put the endpoint cap into the faulty slot 
    // (must be an endpoint cap because seL4_NBRecv is used on it)
    // such that when the faulter retries it will succeed recieving
    error = seL4_CNode_Copy(
        faulter_cspace_root,
        foreign_faulter_capfault_cap,
        seL4_WordBits,
        handler_cspace_root,
        sequencing_ep_cap,
        seL4_WordBits,
        seL4_AllRights
    );
    ZF_LOGF_IF(error != 0, PROGNAME "Failed to copy ep cap into faulter's faulty cap slot!");
    printf(PROGNAME "Copied endpoint into faulter's faulty cap access slot\n");
    
    // wake up the faulter (causing it to retry cap access) by replying to the fault endpoint
    seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));
    printf(PROGNAME "Resumed faulter\n");

    return 0;
}