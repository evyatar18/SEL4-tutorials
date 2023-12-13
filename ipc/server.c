

#include <assert.h>
#include <sel4/sel4.h>
#include <stdio.h>
#include <utils/util.h>

// cslot containing IPC endpoint capability
extern seL4_CPtr endpoint;
// cslot containing a capability to the cnode of the server
extern seL4_CPtr cnode;
// empty cslot
extern seL4_CPtr free_slot;

void copy_message_from_ipcbuffer(seL4_MessageInfo_t message_info, seL4_Word buffer[]) {
     seL4_Word message_length = seL4_MessageInfo_get_length(message_info);
     // printf("Copying msg length: %d, msg: ");
     for (seL4_Word i = 0; i < message_length; ++i) {
          buffer[i] = seL4_GetMR(i);
          // printf("%c", buffer[i]);
     }
     // printf("\n");
}

int main(int c, char *argv[]) {
	seL4_Word sender;
    seL4_MessageInfo_t info = seL4_Recv(endpoint, &sender);
    seL4_Word last_sender = -1;
    seL4_Word message[seL4_MsgMaxLength];
    while (1) {
        seL4_Error error;
        if (sender == 0) {
            /* No badge! give this sender a badged copy of the endpoint */
            seL4_Word badge = seL4_GetMR(0);
            seL4_Error error = seL4_CNode_Mint(cnode, free_slot, seL4_WordBits,
                                                cnode, endpoint, seL4_WordBits,
                                                seL4_AllRights, badge);
            ZF_LOGF_IF(error, "Error badging: %d", error);

            printf("Badged %lu\n", badge);

            // TODO use cap transfer to send the badged cap in the reply
            info = seL4_MessageInfo_new(
                0, // send label of 0
                0, // send 0 unwrapped caps
                1, // send 1 capability
                0  // send 0 MRs
            ); 
            seL4_SetCap(0, free_slot); // put the cap

            /* reply to the sender and wait for the next message */
            seL4_Reply(info);

            /* now delete the transferred cap */
            error = seL4_CNode_Delete(cnode, free_slot, seL4_WordBits);
            assert(error == seL4_NoError);

            /* wait for the next message */
            info = seL4_Recv(endpoint, &sender);
        } else {
            // we assume `sender` contains the sender id
            // also that endpoint has just been received upon
            seL4_MessageInfo_t postponed_info = {0};
            seL4_Word postponed_sender = 0;
            seL4_Word postponed_message[seL4_MsgMaxLength] = {0};
            // if we just received from the last sender
            if (sender == last_sender) {
                // save info from sender that will be postponed
                postponed_info = info;
                postponed_sender = sender;

                copy_message_from_ipcbuffer(postponed_info, postponed_message);

                // save the caller (reply cap) to free_slot
                seL4_CNode_SaveCaller(cnode, free_slot, seL4_WordBits);

                // and try receiving from the other sender (which will be the only one to send, since the postponed sender is blocked)
                // so it is assumed we can ignore the value of sender because it must be the second sender
                info = seL4_Recv(endpoint, &sender);
                ZF_LOGF_IF(sender == postponed_sender, "Received message from sender expected to be blocked!");
            } 
               
            // we assume `info` contains the info about the most recently received message
            // and thus we copy the message from the IPC buffer
            copy_message_from_ipcbuffer(info, message);
            
            // printing the contents from "message"
            seL4_Word message_length = seL4_MessageInfo_get_length(info);
            for (seL4_Word i = 0; i < message_length; ++i) {
                printf("%c", message[i]);
            }
            printf("\n");
               
            // updating last_sender to be the sender we just printed from
            last_sender = sender;

            // await a new message from the endpoint
            info = seL4_ReplyRecv(endpoint, info, &sender);

            if (postponed_sender) {
                // reply to the postponed thread
                // doing this after ReplyRecv means we just got a message from the non-postponed thread
                // this makes sense because we're about to print a message from the postponed thread
                // making it the last thread to have its message printed
                seL4_Send(free_slot, postponed_info);

                // remove the reply cap at free_slot
                error = seL4_CNode_Delete(cnode, free_slot, seL4_WordBits);
                assert(error == seL4_NoError);

                // print the postponed message
                seL4_Word message_length = seL4_MessageInfo_get_length(postponed_info);
                for (seL4_Word i = 0; i < message_length; ++i) {
                    printf("%c", postponed_message[i]);
                }
                printf("\n");

                // update last_sender, because the latest message was printed from the postponed thread
                last_sender = postponed_sender;
            }
        }
    }

    return 0;
}