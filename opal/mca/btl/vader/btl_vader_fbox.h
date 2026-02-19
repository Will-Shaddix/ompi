/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011-2018 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2018      Triad National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#if !defined(MCA_BTL_VADER_FBOX_H)
#define MCA_BTL_VADER_FBOX_H

#include <stdio.h>
#include "btl_vader.h"

#define MCA_BTL_VADER_POLL_COUNT 31

typedef union mca_btl_vader_fbox_hdr_t {
    struct {
        /* NTH: on 32-bit platforms loading/unloading the header may be completed
         * in multiple instructions. To ensure that seq is never loaded before tag
         * and the tag is never read before seq put them in the same 32-bits of the
         * header. */
        /** message size */
        uint32_t  size;
        /** message tag */
        uint16_t  tag;
        /** sequence number */
        uint16_t  seq;
    } data;
    struct {
        uint32_t value0;
        uint32_t value1;
    } data_i32;
    uint64_t ival;
} mca_btl_vader_fbox_hdr_t;

#define MCA_BTL_VADER_FBOX_HDR(x) ((mca_btl_vader_fbox_hdr_t *) (x))

#define MCA_BTL_VADER_FBOX_OFFSET_MASK 0x7fffffff
#define MCA_BTL_VADER_FBOX_HB_MASK     0x80000000

/* if the two offsets are equal and the high bit matches the buffer is empty else the buffer is full.
 * note that start will never be end - 1 so this simplified conditional will always produce the correct
 * result */
#define BUFFER_FREE(s,e,hbm,size) (((s + !hbm) > (e)) ? (s) - (e) : (size - (e)))

/** macro for checking if the high bit is set */
#define MCA_BTL_VADER_FBOX_OFFSET_HBS(v) (!!((v) & MCA_BTL_VADER_FBOX_HB_MASK))

void mca_btl_vader_poll_handle_frag (mca_btl_vader_hdr_t *hdr, mca_btl_base_endpoint_t *ep);

static inline void mca_btl_vader_fbox_set_header (mca_btl_vader_fbox_hdr_t *hdr, uint16_t tag,
                                                  uint16_t seq, uint32_t size)
{
    mca_btl_vader_fbox_hdr_t tmp = {.data = {.tag = tag, .seq = seq, .size = size}};
    /* clear out existing tag/seq */
    hdr->data_i32.value1 = 0;
    opal_atomic_wmb ();
    hdr->data_i32.value0 = size;
    opal_atomic_wmb ();
    hdr->data_i32.value1 = tmp.data_i32.value1;
}

static inline mca_btl_vader_fbox_hdr_t mca_btl_vader_fbox_read_header (mca_btl_vader_fbox_hdr_t *hdr)
{
    mca_btl_vader_fbox_hdr_t tmp = {.data_i32 = {.value1 = hdr->data_i32.value1}};;
    opal_atomic_rmb ();
    tmp.data_i32.value0 = hdr->data_i32.value0;
    return tmp;
}

/* attempt to reserve a contiguous segment from the remote ep */
static inline bool mca_btl_vader_fbox_sendi (mca_btl_base_endpoint_t *ep, unsigned char tag,
                                             void * restrict header, const size_t header_size,
                                             void * restrict payload, const size_t payload_size)
{
    /* Ring Buffer Configuration */
    /* 4 slots of 32 bytes = 128 bytes total */
    const unsigned int SLOT_SIZE = 32;
    const unsigned int RING_MASK = 0x3; 
    
    if (OPAL_UNLIKELY(NULL == ep->fbox_out.buffer)) {
        return false;
    }

    unsigned int base_offset = MCA_BTL_VADER_FBOX_ALIGNMENT;
    /* Calculate Ring Offset based on Sequence Number */
    unsigned int ring_offset = (ep->fbox_out.seq & RING_MASK) * SLOT_SIZE;
    
    /* Destination Address */
    void *dst = ep->fbox_out.buffer + base_offset + ring_offset;

    /* Non-Blocking / Infinite Buffer Simulation */
    /* We do NOT check if buffer is full. We overwrite. */
    /* We do NOT use atomic barriers. */

    const size_t data_size = header_size + payload_size;
    /* Write data */
    if (header_size) {
        memcpy ((void *)((uintptr_t)dst + sizeof(mca_btl_vader_fbox_hdr_t)), header, header_size);
    }

    if (payload_size) {
        memcpy ((void *)((uintptr_t)dst + sizeof(mca_btl_vader_fbox_hdr_t) + header_size), payload, payload_size);
    }

    /* No Atomic Write Memory Barrier */
    opal_atomic_wmb ();

    mca_btl_vader_fbox_set_header (MCA_BTL_VADER_FBOX_HDR(dst), tag, ep->fbox_out.seq++, data_size);

    return true;
}

static inline bool mca_btl_vader_check_fboxes (void)
{
    /* const unsigned int fbox_size = mca_btl_vader_component.fbox_size; */
    bool processed = false;

    for (unsigned int i = 0 ; i < mca_btl_vader_component.num_fbox_in_endpoints ; ++i) {
        mca_btl_base_endpoint_t *ep = mca_btl_vader_component.fbox_in_endpoints[i];
        
        /* Ring Buffer Configuration */
        const unsigned int SLOT_SIZE = 32;
        const unsigned int RING_MASK = 0x3;
        unsigned int base_offset = MCA_BTL_VADER_FBOX_ALIGNMENT;

        /* We poll the slot where we EXPECT the next sequence number */
        unsigned int ring_offset = (ep->fbox_in.seq & RING_MASK) * SLOT_SIZE;
        unsigned int current_offset = base_offset + ring_offset;

        const mca_btl_vader_fbox_hdr_t *hdr = (mca_btl_vader_fbox_hdr_t *)(ep->fbox_in.buffer + current_offset);
        /* volatile mca_btl_vader_fbox_hdr_t *hdr_mutable = ...; No longer needed for clearing */

        /* check for a valid tag */
        if (0 == hdr->data.tag) {
            continue; /* No message in this slot yet */
        }

        /* Check Sequence Number */
        /* Since slots are reused, we might see:
           1. The EXPECTED sequence (Normal)
           2. A NEWER sequence (Overwrite happened, we missed intermediate)
           3. An OLD sequence (Sender hasn't written this slot since we last read it)
        */
        
        int16_t diff = (int16_t)(hdr->data.seq - ep->fbox_in.seq);
        
        if (diff < 0) {
             /* Old sequence number - we already processed this slot's content roughly RING_SIZE messages ago. */
             /* Move strictly to next endpoint, nothing new here. */
             continue;
        }
        
        /* If diff >= 0, we have new data! */
        /* Note: If diff > 0, we skipped messages. That's allowed/expected in this model. */

        /* Update our expected sequence to the NEXT one after this message. */
        ep->fbox_in.seq = hdr->data.seq + 1;

        /* No Atomic Read Barrier */
        opal_atomic_rmb ();

        BTL_VERBOSE(("got frag from %d with header {.tag = %d, .size = %d, .seq = %u} from Ring Offset %u",
                         ep->peer_smp_rank, hdr->data.tag, hdr->data.size, hdr->data.seq, ring_offset));

        /* Process the message */
        if (OPAL_LIKELY((0xfe & hdr->data.tag) != 0xfe)) {
            mca_btl_base_segment_t segment;
            mca_btl_base_descriptor_t desc = {.des_segments = &segment, .des_segment_count = 1};
            mca_btl_active_message_callback_t *reg = mca_btl_base_active_message_trigger + hdr->data.tag;

            segment.seg_len = hdr->data.size;
            /* Payload is after the header */
            segment.seg_addr.pval = (void *) (ep->fbox_in.buffer + current_offset + sizeof (mca_btl_vader_fbox_hdr_t));

            /* call the registered callback function */
            reg->cbfunc(&mca_btl_vader.super, hdr->data.tag, &desc, reg->cbdata);
        } else if (OPAL_LIKELY(0xfe == hdr->data.tag)) {
            /* process fragment header */
            fifo_value_t *value = (fifo_value_t *)(ep->fbox_in.buffer + current_offset + sizeof (mca_btl_vader_fbox_hdr_t));
            mca_btl_vader_hdr_t *vhdr = relative2virtual(*value);
            mca_btl_vader_poll_handle_frag (vhdr, ep);
        }
        
        /* No Clearing of Tag needed for Ring Buffer */
        /* Overwrite handles validity. */

        processed = true;
     }

    /* return the number of fragments processed */
    return processed;
}

static inline void mca_btl_vader_try_fbox_setup (mca_btl_base_endpoint_t *ep, mca_btl_vader_hdr_t *hdr)
{
    if (OPAL_UNLIKELY(NULL == ep->fbox_out.buffer && mca_btl_vader_component.fbox_threshold == OPAL_THREAD_ADD_FETCH_SIZE_T (&ep->send_count, 1))) {
        /* protect access to mca_btl_vader_component.segment_offset */
        OPAL_THREAD_LOCK(&mca_btl_vader_component.lock);

        /* verify the remote side will accept another fbox */
        if (0 <= opal_atomic_add_fetch_32 (&ep->fifo->fbox_available, -1)) {
            opal_free_list_item_t *fbox = opal_free_list_get (&mca_btl_vader_component.vader_fboxes);

            if (NULL != fbox) {
                /* zero out the fast box */
                memset (fbox->ptr, 0, mca_btl_vader_component.fbox_size);
                mca_btl_vader_endpoint_setup_fbox_send (ep, fbox);

                hdr->flags |= MCA_BTL_VADER_FLAG_SETUP_FBOX;
                hdr->fbox_base = virtual2relative((char *) ep->fbox_out.buffer);
            } else {
                opal_atomic_add_fetch_32 (&ep->fifo->fbox_available, 1);
            }

            opal_atomic_wmb ();
        }

        OPAL_THREAD_UNLOCK(&mca_btl_vader_component.lock);
    }
}

#endif /* !defined(MCA_BTL_VADER_FBOX_H) */
