/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011-2018 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2018      Triad National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2020      Google, LLC. All rights reserveed.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_BTL_SM_FBOX_H
#define MCA_BTL_SM_FBOX_H

#include <stdio.h>
#include "opal/util/proc.h"

#include "opal/mca/btl/sm/btl_sm_types.h"
#include "opal/mca/btl/sm/btl_sm_virtual.h"

#define MCA_BTL_SM_POLL_COUNT          31
#define MCA_BTL_SM_FBOX_ALIGNMENT      32
#define MCA_BTL_SM_FBOX_ALIGNMENT_MASK (MCA_BTL_SM_FBOX_ALIGNMENT - 1)

/**
 *  An abstraction that represents a connection to a endpoint process.
 *  An instance of mca_ptl_base_endpoint_t is associated w/ each process
 *  and BTL pair at startup.
 */

static inline void mca_btl_sm_endpoint_setup_fbox_recv(struct mca_btl_base_endpoint_t *endpoint,
                                                       void *base)
{
    endpoint->fbox_in.startp = (uint32_t *) base;
    endpoint->fbox_in.start = MCA_BTL_SM_FBOX_ALIGNMENT;
    endpoint->fbox_in.seq = 0;
    endpoint->fbox_in.buffer = base;
}

static inline void mca_btl_sm_endpoint_setup_fbox_send(struct mca_btl_base_endpoint_t *endpoint,
                                                       opal_free_list_item_t *fbox)
{
    void *base = fbox->ptr;

    endpoint->fbox_out.start = MCA_BTL_SM_FBOX_ALIGNMENT;
    endpoint->fbox_out.end = MCA_BTL_SM_FBOX_ALIGNMENT;
    endpoint->fbox_out.startp = (uint32_t *) base;
    endpoint->fbox_out.startp[0] = MCA_BTL_SM_FBOX_ALIGNMENT;
    endpoint->fbox_out.seq = 0;
    endpoint->fbox_out.fbox = fbox;

    /* zero out the first header in the fast box */
    memset((char *) base + MCA_BTL_SM_FBOX_ALIGNMENT, 0, MCA_BTL_SM_FBOX_ALIGNMENT);

    endpoint->fbox_out.buffer = base;
}

typedef union mca_btl_sm_fbox_hdr_t {
    struct {
        /* NTH: on 32-bit platforms loading/unloading the header may be completed
         * in multiple instructions. To ensure that seq is never loaded before tag
         * and the tag is never read before seq put them in the same 32-bits of the
         * header. */
        /** message size */
        uint32_t size;
        /** message tag */
        uint16_t tag;
        /** sequence number */
        uint16_t seq;
    } data;
    struct {
        uint32_t value0;
        uint32_t value1;
    } data_i32;
    uint64_t ival;
} mca_btl_sm_fbox_hdr_t;

#define MCA_BTL_SM_FBOX_HDR(x) ((mca_btl_sm_fbox_hdr_t *) (x))

#define MCA_BTL_SM_FBOX_OFFSET_MASK 0x7fffffff
#define MCA_BTL_SM_FBOX_HB_MASK     0x80000000

/* if the two offsets are equal and the high bit matches the buffer is empty else the buffer is
 * full. note that start will never be end - 1 so this simplified conditional will always produce
 * the correct result */
#define BUFFER_FREE(s, e, hbm, size) (((s + !hbm) > (e)) ? (s) - (e) : (size - (e)))

/** macro for checking if the high bit is set */
#define MCA_BTL_SM_FBOX_OFFSET_HBS(v) (!!((v) &MCA_BTL_SM_FBOX_HB_MASK))

void mca_btl_sm_poll_handle_frag(mca_btl_sm_hdr_t *hdr, mca_btl_base_endpoint_t *endpoint);

static inline void mca_btl_sm_fbox_set_header(mca_btl_sm_fbox_hdr_t *hdr, uint16_t tag,
                                              uint16_t seq, uint32_t size)
{
    mca_btl_sm_fbox_hdr_t tmp = {.data = {.tag = tag, .seq = seq, .size = size}};
    /* clear out existing tag/seq */
    hdr->data_i32.value1 = 0;
    hdr->data_i32.value0 = size;
    hdr->data_i32.value1 = tmp.data_i32.value1;
}

static inline mca_btl_sm_fbox_hdr_t mca_btl_sm_fbox_read_header(mca_btl_sm_fbox_hdr_t *hdr)
{
    mca_btl_sm_fbox_hdr_t tmp = {.data_i32 = {.value1 = hdr->data_i32.value1}};
    tmp.data_i32.value0 = hdr->data_i32.value0;
    return tmp;
}

/* attempt to reserve a contiguous segment from the remote ep */
static inline bool mca_btl_sm_fbox_sendi(mca_btl_base_endpoint_t *ep, unsigned char tag,
                                         void *restrict header, const size_t header_size,
                                         void *restrict payload, const size_t payload_size)
{
    const unsigned int fbox_size = mca_btl_sm_component.fbox_size;
    size_t size = header_size + payload_size;
    unsigned char *dst, *data;

    /* don't try to use the per-peer buffer for messages that will fill up more than 25% of the
     * buffer */
    if (OPAL_UNLIKELY(NULL == ep->fbox_out.buffer || size > (fbox_size >> 2))) {
        return false;
    }

    dst = ep->fbox_out.buffer + MCA_BTL_SM_FBOX_ALIGNMENT;

    data = dst + sizeof(mca_btl_sm_fbox_hdr_t);

    memcpy(data, header, header_size);
    if (payload) {
        /* inline sends are typically just pml headers (due to MCA_BTL_FLAGS_SEND_INPLACE) */
        memcpy(data + header_size, payload, payload_size);
    }

    /* write out part of the header now. the tag will be written when the data is available */
    mca_btl_sm_fbox_set_header(MCA_BTL_SM_FBOX_HDR(dst), tag, ep->fbox_out.seq++, size);

    // DEBUG:
    fprintf(stderr, "Rank %d: Wrote to Fast Box for tag %d size %lu\n", opal_process_info.my_name.vpid, tag, size);

    return true;
}

static inline bool mca_btl_sm_check_fboxes(void)
{
    const unsigned int fbox_size = mca_btl_sm_component.fbox_size;
    bool processed = false;

    for (unsigned int i = 0; i < mca_btl_sm_component.num_fbox_in_endpoints; ++i) {
        mca_btl_base_endpoint_t *ep = mca_btl_sm_component.fbox_in_endpoints[i];
        unsigned int start = MCA_BTL_SM_FBOX_ALIGNMENT;

        int poll_count;

        for (poll_count = 0; poll_count <= MCA_BTL_SM_POLL_COUNT; ++poll_count) {
            const mca_btl_sm_fbox_hdr_t hdr = mca_btl_sm_fbox_read_header(
                MCA_BTL_SM_FBOX_HDR(ep->fbox_in.buffer + start));

            /* check for a valid tag a sequence number */
            if (0 == hdr.data.tag || hdr.data.seq < ep->fbox_in.seq) {
                break;
            }

            // Catch-up: set expected seq to next one
            ep->fbox_in.seq = hdr.data.seq + 1;

            BTL_VERBOSE(
                ("got frag from %d with header {.tag = %d, .size = %d, .seq = %u} from offset %u",
                 ep->peer_smp_rank, hdr.data.tag, hdr.data.size, hdr.data.seq, start));

            /* the 0xff tag indicates we should skip the rest of the buffer */
            if (OPAL_LIKELY((0xfe & hdr.data.tag) != 0xfe)) {
                mca_btl_base_segment_t segment;
                const mca_btl_active_message_callback_t *reg = mca_btl_base_active_message_trigger
                                                               + hdr.data.tag;
                mca_btl_base_receive_descriptor_t desc = {.endpoint = ep,
                                                          .des_segments = &segment,
                                                          .des_segment_count = 1,
                                                          .tag = hdr.data.tag,
                                                          .cbdata = reg->cbdata};

                /* fragment fits entirely in the remaining buffer space. some
                 * btl users do not handle fragmented data so we can't split
                 * the fragment without introducing another copy here. this
                 * limitation has not appeared to cause any performance
                 * degradation. */
                segment.seg_len = hdr.data.size;
                segment.seg_addr.pval = (void *) (ep->fbox_in.buffer + start + sizeof(hdr));

                /* call the registered callback function */
                reg->cbfunc(&mca_btl_sm.super, &desc);
            } else if (OPAL_LIKELY(0xfe == hdr.data.tag)) {
                /* process fragment header */
                fifo_value_t *value = (fifo_value_t *) (ep->fbox_in.buffer + start + sizeof(hdr));
                mca_btl_sm_hdr_t *sm_hdr = relative2virtual(*value);
                mca_btl_sm_poll_handle_frag(sm_hdr, ep);
            }
        }

        if (poll_count) {
             processed = true;
        }
    }

    /* return the number of fragments processed */
    return processed;
}

static inline void mca_btl_sm_try_fbox_setup(mca_btl_base_endpoint_t *ep, mca_btl_sm_hdr_t *hdr)
{
    if (OPAL_UNLIKELY(NULL == ep->fbox_out.buffer
                      && mca_btl_sm_component.fbox_threshold
                             == OPAL_THREAD_ADD_FETCH_SIZE_T(&ep->send_count, 1))) {

        if (ep->fifo->fbox_available > 0) {
            ep->fifo->fbox_available--; // Non-atomic decrement
            opal_free_list_item_t *fbox = opal_free_list_get(&mca_btl_sm_component.sm_fboxes);

            if (NULL != fbox) {
                /* zero out the fast box */
                memset(fbox->ptr, 0, mca_btl_sm_component.fbox_size);
                mca_btl_sm_endpoint_setup_fbox_send(ep, fbox);

                hdr->flags |= MCA_BTL_SM_FLAG_SETUP_FBOX;
                hdr->fbox_base = virtual2relative((char *) ep->fbox_out.buffer);
                fprintf(stderr, "Rank %d: Setup Fast Box triggered\n", opal_process_info.my_name.vpid);
            } else {
                ep->fifo->fbox_available++; // Non-atomic increment
            }
        }
    }
}

#endif /* MCA_BTL_SM_FBOX_H */
