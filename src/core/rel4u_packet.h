#ifndef REL4U_PACKET_H
#define REL4U_PACKET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define REL4U_MAGIC         0x3455
#define REL4U_VERSION       1
#define REL4U_HEADER_LEN    24

/* Packet Types */
typedef enum rel4u_pkt_type {
    REL4U_PKT_CONNECT_REQ   = 0x01,  /* Client -> Server: Connection Request (SYN) */
    REL4U_PKT_CONNECT_RESP  = 0x02,  /* Server -> Client: Connection Response with Session Token / Cookie (SYN-ACK) */
    REL4U_PKT_CONNECT_ACK   = 0x03,  /* Client -> Server: Connection Established ACK (ACK) */
    REL4U_PKT_DATA          = 0x04,  /* Bidirectional: Application Data Payload */
    REL4U_PKT_ACK           = 0x05,  /* Bidirectional: Standalone ACK / SACK packet */
    REL4U_PKT_HEARTBEAT     = 0x06,  /* Bidirectional: Ping / Keepalive */
    REL4U_PKT_HEARTBEAT_ACK = 0x07,  /* Bidirectional: Pong / Keepalive response */
    REL4U_PKT_DISCONNECT    = 0x08,  /* Bidirectional: Graceful Disconnect (FIN) */
    REL4U_PKT_REJECT        = 0x09,  /* Server -> Client: Connection Rejected (Server Full / Policy) */
    REL4U_PKT_RESET         = 0x0A   /* Server -> Client: Session Reset / Unrecognized Session */
} rel4u_pkt_type_t;

/* Header Flags */
#define REL4U_FLAG_ACK_IMMEDIATE 0x01
#define REL4U_FLAG_PROBE         0x02

typedef struct rel4u_header {
    uint16_t magic;
    uint8_t  version;
    uint8_t  packet_type;
    uint8_t  delivery_mode;
    uint8_t  flags;
    uint16_t payload_len;
    uint32_t session_id;
    uint32_t seq_num;
    uint32_t ack_num;
    uint32_t sack_mask;
} rel4u_header_t;

/**
 * @brief Serialize packet header into byte buffer in Network Byte Order.
 * @param hdr Pointer to header struct.
 * @param out_buf Buffer of at least REL4U_HEADER_LEN (24 bytes).
 * @param buf_size Size of out_buf.
 * @return REL4U_HEADER_LEN on success, negative on error.
 */
int rel4u_packet_encode_header(const rel4u_header_t* hdr, uint8_t* out_buf, size_t buf_size);

/**
 * @brief Parse and validate packet header from raw byte buffer.
 * @param in_buf Raw buffer received from socket.
 * @param buf_len Length of received data.
 * @param out_hdr Pointer to header struct to populate.
 * @return 0 on success, negative if invalid magic, version, or truncated packet.
 */
int rel4u_packet_decode_header(const uint8_t* in_buf, size_t buf_len, rel4u_header_t* out_hdr);

#endif /* REL4U_PACKET_H */
