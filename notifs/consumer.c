

#include <assert.h>
#include <sel4/sel4.h>
#include <stdio.h>
#include <utils/util.h>
#include <sel4utils/util.h>

// notification object
extern seL4_CPtr buf1_empty;
extern seL4_CPtr buf2_empty;
extern seL4_CPtr full;
extern seL4_CPtr endpoint;

extern seL4_CPtr buf1_frame_cap;
extern const char buf1_frame[4096];
extern seL4_CPtr buf2_frame_cap;
extern const char buf2_frame[4096];

// cslot containing a capability to the cnode of the server
extern seL4_CPtr consumer_vspace;
extern seL4_CPtr producer_1_vspace;
extern seL4_CPtr producer_2_vspace;

extern seL4_CPtr cnode;
extern seL4_CPtr mapping_1;
extern seL4_CPtr mapping_2;

#define BUF_VADDR 0x5FF000

int main(int c, char *argv[]) {
    seL4_Error error = seL4_NoError;
    seL4_Word badge;

    
    /* set up shared memory for consumer 1 */
    /* first duplicate the cap */
    error = seL4_CNode_Copy(cnode, mapping_1, seL4_WordBits, 
                          cnode, buf1_frame_cap, seL4_WordBits, seL4_AllRights);
    ZF_LOGF_IFERR(error, "Failed to copy cap");
    /* now do the mapping */
    error = seL4_ARCH_Page_Map(mapping_1, producer_1_vspace, BUF_VADDR, 
                               seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    ZF_LOGF_IFERR(error, "Failed to map frame");
    
    // TODO share buf2_frame_cap with producer_2
    /* first duplicate the cap */
    error = seL4_CNode_Copy(cnode, mapping_2, seL4_WordBits, 
                          cnode, buf2_frame_cap, seL4_WordBits, seL4_AllRights);
    ZF_LOGF_IFERR(error, "Failed to copy cap");
    /* now do the mapping */
    error = seL4_ARCH_Page_Map(mapping_2, producer_2_vspace, BUF_VADDR, 
                               seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    ZF_LOGF_IFERR(error, "Failed to map frame");
    /* send IPCs with the buffer address to both producers */
    seL4_SetMR(0, BUF_VADDR);
    seL4_Send(endpoint, seL4_MessageInfo_new(0, 0, 0, 1));
    seL4_SetMR(0, BUF_VADDR);
    seL4_Send(endpoint, seL4_MessageInfo_new(0, 0, 0, 1));
    
    /* start single buffer producer consumer */
    volatile long *buf1 = (long *) buf1_frame;
    volatile long *buf2 = (long *) buf2_frame;

    *buf1 = 0;
    *buf2 = 0;

    // TODO signal both producers
    seL4_Signal(buf1_empty);
    seL4_Signal(buf2_empty);

    printf("Waiting for producer\n");
    for (int i = 0; i < 10; i++) {
        // yield timeslice 5 times, causing some wait
        for (int i = 0; i < 5; ++i) {
            printf("Yielding timeslice...\n");
            seL4_Yield();
        }

        seL4_Wait(full, &badge);
        printf("Got badge: %lx\n", badge);
        if (badge & 1) {
            seL4_Signal(buf1_empty);
            printf("From 1: %d\n", *((long*)buf1_frame));
        }
        if (badge & 2) {
            seL4_Signal(buf2_empty);
            printf("From 2: %d\n", *((long*)buf2_frame));
        }
    // TODO, use the badge to check which producer has signalled you, and signal it back. Note that you 
    // may recieve more than 1 signal at a time.
   }

    printf("Success!\n");
    return 0;
}