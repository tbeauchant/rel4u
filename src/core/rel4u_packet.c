#include "rel4u_packet.h"
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

int rel4u_packet_encode_header(const rel4u_header_t* hdr, uint8_t* out_buf, size_t buf_size) {
    if (!hdr || !out_buf || buf_size < REL4U_HEADER_LEN) {
        return -1;
    }

    uint16_t magic_net   = htons(hdr->magic);
    uint16_t len_net     = htons(hdr->payload_len);
    uint32_t session_net = htonl(hdr->session_id);
    uint32_t seq_net     = htonl(hdr->seq_num);
    uint32_t ack_net     = htonl(hdr->ack_num);
    uint32_t sack_net    = htonl(hdr->sack_mask);

    memcpy(out_buf + 0,  &magic_net,   2);
    out_buf[2] = hdr->version;
    out_buf[3] = hdr->packet_type;
    out_buf[4] = hdr->delivery_mode;
    out_buf[5] = hdr->flags;
    memcpy(out_buf + 6,  &len_net,     2);
    memcpy(out_buf + 8,  &session_net, 4);
    memcpy(out_buf + 12, &seq_net,     4);
    memcpy(out_buf + 16, &ack_net,     4);
    memcpy(out_buf + 20, &sack_net,    4);

    return REL4U_HEADER_LEN;
}

int rel4u_packet_decode_header(const uint8_t* in_buf, size_t buf_len, rel4u_header_t* out_hdr) {
    if (!in_buf || !out_hdr || buf_len < REL4U_HEADER_LEN) {
        return -1;
    }

    uint16_t magic_net, len_net;
    uint32_t session_net, seq_net, ack_net, sack_net;

    memcpy(&magic_net,   in_buf + 0,  2);
    out_hdr->version       = in_buf[2];
    out_hdr->packet_type   = in_buf[3];
    out_hdr->delivery_mode = in_buf[4];
    out_hdr->flags         = in_buf[5];
    memcpy(&len_net,     in_buf + 6,  2);
    memcpy(&session_net, in_buf + 8,  4);
    memcpy(&seq_net,     in_buf + 12, 4);
    memcpy(&ack_net,     in_buf + 16, 4);
    memcpy(&sack_net,    in_buf + 20, 4);

    out_hdr->magic       = ntohs(magic_net);
    out_hdr->payload_len = ntohs(len_net);
    out_hdr->session_id  = ntohl(session_net);
    out_hdr->seq_num     = ntohl(seq_net);
    out_hdr->ack_num     = ntohl(ack_net);
    out_hdr->sack_mask   = ntohl(sack_net);

    if (out_hdr->magic != REL4U_MAGIC) {
        return -2; /* Invalid Magic */
    }
    if (out_hdr->version != REL4U_VERSION) {
        return -3; /* Unsupported Version */
    }
    if (buf_len < (size_t)(REL4U_HEADER_LEN + out_hdr->payload_len)) {
        return -4; /* Incomplete Payload */
    }

    return 0;
}
