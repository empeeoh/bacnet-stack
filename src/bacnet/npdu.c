/**
 * @file
 * @brief API for Network Protocol Data Unit (NPDU) encode and decode functions
 * @author Steve Karg <skarg@users.sourceforge.net>
 * @date 2005
 * @copyright SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0
 */
#include <stdbool.h>
#include <stdint.h>
/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bacdcode.h"
#include "bacnet/bacint.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"

/** @file npdu.c  Encode/Decode NPDUs - Network Protocol Data Units */

/** Copy the npdu_data structure information from src to dest.
 * @param dest [out] The 'to' structure
 * @param src   [in] The 'from' structure
 */
void npdu_copy_data(BACNET_NPDU_DATA *dest, BACNET_NPDU_DATA *src)
{
    if (dest && src) {
        dest->protocol_version = src->protocol_version;
        dest->data_expecting_reply = src->data_expecting_reply;
        dest->network_layer_message = src->network_layer_message;
        dest->priority = src->priority;
        dest->network_message_type = src->network_message_type;
        dest->vendor_id = src->vendor_id;
        dest->hop_count = src->hop_count;
    }

    return;
}

/*

The following ICI parameters are exchanged with the
various service primitives across an API:

'destination_address' (DA): the address of the device(s)
intended to receive the service primitive. Its format (device name,
network address, etc.) is a local matter. This address
may also be a multicast, local broadcast or global broadcast type.

'source_address' (SA): the address of the device from which
the service primitive was received. Its format (device name,
network address, etc.) is a local matter.

'network_priority' (NP): a four-level network priority parameter
described in 6.2.2.

'data_expecting_reply' (DER): a Boolean parameter that indicates
whether (TRUE) or not (FALSE) a reply service primitive
is expected for the service being issued.


Table 5-1. Applicability of ICI parameters for abstract service primitives
     Service Primitive         DA           SA         NP        DER
CONF_SERV.request              Yes          No         Yes       Yes
CONF_SERV.indication           Yes         Yes         Yes       Yes
CONF_SERV.response             Yes          No         Yes       Yes
CONF_SERV.confirm              Yes         Yes         Yes        No
UNCONF_SERV.request            Yes          No         Yes        No
UNCONF_SERV.indication         Yes         Yes         Yes        No
REJECT.request                 Yes          No         Yes        No
REJECT.indication              Yes         Yes         Yes        No
SEGMENT_ACK.request            Yes          No         Yes        No
SEGMENT_ACK.indication         Yes         Yes         Yes        No
ABORT.request                  Yes          No         Yes        No
ABORT.indication               Yes         Yes         Yes        No
*/

/** Encode the NPDU portion of a message to be sent, based on the npdu_data
 *  and associated data.
 *  If this is to be a Network Layer Control Message, there are probably
 *  more bytes which will need to be encoded following the ones encoded here.
 *  The Network Layer Protocol Control Information byte is described
 *  in section 6.2.2 of the BACnet standard.
 * @param npdu [out] Buffer which will hold the encoded NPDU header bytes.
 * 	The size isn't given, but it must be at least 2 bytes for the simplest
 *  case, and should always be at least 24 bytes to accommodate the maximal
 *  case (all fields loaded). If the buffer is NULL, the number of bytes
 *  the buffer would have held is returned.
 * @param dest [in] The routing destination information if the message must
 *  be routed to reach its destination. If dest->net and dest->len are 0,
 *  there is no routing destination information.
 * @param src  [in] The routing source information if the message was routed
 *  from another BACnet network. If src->net and src->len are 0, there is no
 *  routing source information. This src describes the original source of the
 *  message when it had to be routed to reach this BACnet Device.
 * @param npdu_data [in] The structure which describes how the NCPI and other
 *  NPDU bytes should be encoded.
 * @return On success, returns the number of bytes which were encoded into
 *  the NPDU section, or 0 if there were problems with the data or encoding.
 */
int npdu_encode_pdu(uint8_t *npdu,
    BACNET_ADDRESS *dest,
    BACNET_ADDRESS *src,
    BACNET_NPDU_DATA *npdu_data)
{
    int len = 0; /* return value - number of octets loaded in this function */
    uint8_t i = 0; /* counter  */

    if (npdu_data) {
        /* protocol version */
        if (npdu) {
            npdu[0] = npdu_data->protocol_version;
        }
        /* initialize the control octet */
        if (npdu) {
            npdu[1] = 0;
            /* Bit 7: 1 indicates that the NSDU conveys a network layer message.
             */
            /*          Message Type field is present. */
            /*        0 indicates that the NSDU contains a BACnet APDU. */
            /*          Message Type field is absent. */
            if (npdu_data->network_layer_message) {
                npdu[1] |= BIT(7);
            }
            /*Bit 6: Reserved. Shall be zero. */
            /*Bit 5: Destination specifier where: */
            /* 0 = DNET, DLEN, DADR, and Hop Count absent */
            /* 1 = DNET, DLEN, and Hop Count present */
            /* DLEN = 0 denotes broadcast MAC DADR and DADR field is absent */
            /* DLEN > 0 specifies length of DADR field */
            if (dest && dest->net) {
                npdu[1] |= BIT(5);
            }
            /* Bit 4: Reserved. Shall be zero. */
            /* Bit 3: Source specifier where: */
            /* 0 =  SNET, SLEN, and SADR absent */
            /* 1 =  SNET, SLEN, and SADR present */
            /* SLEN = 0 Invalid */
            /* SLEN > 0 specifies length of SADR field */
            if (src && src->net && src->len) {
                npdu[1] |= BIT(3);
            }
            /* Bit 2: The value of this bit corresponds to the */
            /* data_expecting_reply parameter in the N-UNITDATA primitives. */
            /* 1 indicates that a BACnet-Confirmed-Request-PDU, */
            /* a segment of a BACnet-ComplexACK-PDU, */
            /* or a network layer message expecting a reply is present. */
            /* 0 indicates that other than a BACnet-Confirmed-Request-PDU, */
            /* a segment of a BACnet-ComplexACK-PDU, */
            /* or a network layer message expecting a reply is present. */
            if (npdu_data->data_expecting_reply) {
                npdu[1] |= BIT(2);
            }
            /* Bits 1,0: Network priority where: */
            /* B'11' = Life Safety message */
            /* B'10' = Critical Equipment message */
            /* B'01' = Urgent message */
            /* B'00' = Normal message */
            npdu[1] |= (npdu_data->priority & 0x03);
        }
        len = 2;
        if (dest && dest->net) {
            if (npdu) {
                encode_unsigned16(&npdu[len], dest->net);
            }
            len += 2;
            if (dest->len > MAX_MAC_LEN) {
                dest->len = MAX_MAC_LEN;
            }
            if (npdu) {
                npdu[len] = dest->len;
            }
            len++;
            /* DLEN = 0 denotes broadcast MAC DADR and DADR field is absent */
            /* DLEN > 0 specifies length of DADR field */
            if (dest->len) {
                for (i = 0; i < dest->len; i++) {
                    if (npdu) {
                        npdu[len] = dest->adr[i];
                    }
                    len++;
                }
            }
        }
        if (src && src->net && src->len) {
            /* Only insert if valid */
            if (npdu) {
                encode_unsigned16(&npdu[len], src->net);
            }
            len += 2;
            if (src->len > MAX_MAC_LEN) {
                src->len = MAX_MAC_LEN;
            }
            if (npdu) {
                npdu[len] = src->len;
            }
            len++;
            /* SLEN = 0 denotes broadcast MAC SADR and SADR field is absent */
            /* SLEN > 0 specifies length of SADR field */
            if (src->len) {
                for (i = 0; i < src->len; i++) {
                    if (npdu) {
                        npdu[len] = src->adr[i];
                    }
                    len++;
                }
            }
        }
        /* The Hop Count field shall be present only if the message is */
        /* destined for a remote network, i.e., if DNET is present. */
        /* This is a one-octet field that is initialized to a value of 0xff. */
        if (dest && dest->net) {
            if (npdu) {
                npdu[len] = npdu_data->hop_count;
            }
            len++;
        }
        if (npdu_data->network_layer_message) {
            if (npdu) {
                npdu[len] = npdu_data->network_message_type;
            }
            len++;
            /* Message Type field contains a value in the range 0x80 - 0xFF, */
            /* then a Vendor ID field shall be present */
            if (npdu_data->network_message_type >= 0x80) {
                if (npdu) {
                    encode_unsigned16(&npdu[len], npdu_data->vendor_id);
                }
                len += 2;
            }
        }
    }

    return len;
}

/**
 * @brief Encode the NPDU portion of a message to be sent
 *  based on the npdu_data and associated data.
 *  If this is to be a Network Layer Control Message, there are probably
 *  more bytes which will need to be encoded following the ones encoded here.
 *  The Network Layer Protocol Control Information byte is described
 *  in section 6.2.2 of the BACnet standard.
 * @param pdu [out] Buffer which will hold the encoded NPDU header bytes.
 *  If pdu is NULL, the number of bytes the buffer would have held
 *  is returned.
 * @param pdu_size Number of bytes in the buffer to hold the encoded data.
 *  If the size is zero, the number of bytes the buffer would have held
 *  is returned.
 * 	The size isn't given, but it must be at least 2 bytes for the simplest
 *  case, and should always be at least 24 bytes to accommodate the maximal
 *  case (all fields loaded). Can be NULL to determine length of buffer.
 * @param dest [in] The routing destination information if the message must
 *  be routed to reach its destination. If dest->net and dest->len are 0,
 *  there is no routing destination information.
 * @param src  [in] The routing source information if the message was routed
 *  from another BACnet network. If src->net and src->len are 0, there is no
 *  routing source information. This src describes the original source of the
 *  message when it had to be routed to reach this BACnet Device.
 * @param npdu_data [in] The structure which describes how the NCPI and other
 *  NPDU bytes should be encoded.
 * @return On success, returns the number of bytes which were encoded into
 *  the NPDU section, or 0 if there were problems with the data or encoding.
 */
int bacnet_npdu_encode_pdu(uint8_t *pdu,
    uint16_t pdu_size,
    BACNET_ADDRESS *dest,
    BACNET_ADDRESS *src,
    BACNET_NPDU_DATA *npdu_data)
{
    int pdu_len = 0;

    pdu_len = npdu_encode_pdu(NULL, dest, src, npdu_data);
    if ((pdu != NULL) && (pdu_size > 0) && (pdu_len <= pdu_size)) {
        pdu_len = npdu_encode_pdu(pdu, dest, src, npdu_data);
    }

    return pdu_len;
}

/* Configure the NPDU portion of the packet for an APDU */
/* This function does not handle the network messages, just APDUs. */
/* From BACnet 5.1:
Applicability of ICI parameters for abstract service primitives
Service Primitive      DA  SA  NP  DER
-----------------      --- --- --- ---
CONF_SERV.request      Yes No  Yes Yes
CONF_SERV.indication   Yes Yes Yes Yes
CONF_SERV.response     Yes No  Yes Yes
CONF_SERV.confirm      Yes Yes Yes No
UNCONF_SERV.request    Yes No  Yes No
UNCONF_SERV.indication Yes Yes Yes No
REJECT.request         Yes No  Yes No
REJECT.indication      Yes Yes Yes No
SEGMENT_ACK.request    Yes No  Yes No
SEGMENT_ACK.indication Yes Yes Yes No
ABORT.request          Yes No  Yes No
ABORT.indication       Yes Yes Yes No

Where:
'destination_address' (DA): the address of the device(s) intended
to receive the service primitive. Its format (device name,
network address, etc.) is a local matter. This address may
also be a multicast, local broadcast or global broadcast type.
'source_address' (SA): the address of the device from which
the service primitive was received. Its format (device name,
network address, etc.) is a local matter.
'network_priority' (NP): a four-level network priority parameter
described in 6.2.2.
'data_expecting_reply' (DER): a Boolean parameter that indicates
whether (TRUE) or not (FALSE) a reply service primitive
is expected for the service being issued.
*/

/** Initialize an npdu_data structure to good defaults.
 * The name is a misnomer, as it doesn't do any actual encoding here.
 * @see npdu_encode_npdu_network if you need to set a network layer msg.
 *
 * @param npdu_data [out] Returns a filled-out structure with information
 * 					 provided by the other arguments and
 * good defaults.
 * @param data_expecting_reply [in] True if message should have a reply.
 * @param priority [in] One of the 4 priorities defined in section 6.2.2,
 *                      like B'11' = Life Safety message
 */
void npdu_encode_npdu_data(BACNET_NPDU_DATA *npdu_data,
    bool data_expecting_reply,
    BACNET_MESSAGE_PRIORITY priority)
{
    if (npdu_data) {
        npdu_data->data_expecting_reply = data_expecting_reply;
        npdu_data->protocol_version = BACNET_PROTOCOL_VERSION;
        npdu_data->network_layer_message = false; /* false if APDU */
        npdu_data->network_message_type =
            NETWORK_MESSAGE_INVALID; /* optional */
        npdu_data->vendor_id = 0; /* optional, if net message type is > 0x80 */
        npdu_data->priority = priority;
        npdu_data->hop_count = HOP_COUNT_DEFAULT;
    }
}

/** Initialize an npdu_data structure with given parameters and good defaults,
 * and add the Network Layer Message fields.
 * The name is a misnomer, as it doesn't do any actual encoding here.
 * @see npdu_encode_npdu_data for a simpler version to use when sending an
 *           APDU instead of a Network Layer Message.
 *
 * @param npdu_data [out] Returns a filled-out structure with information
 * 					 provided by the other arguments and
 * good defaults.
 * @param network_message_type [in] The type of Network Layer Message.
 * @param data_expecting_reply [in] True if message should have a reply.
 * @param priority [in] One of the 4 priorities defined in section 6.2.2,
 *                      like B'11' = Life Safety message
 */
void npdu_encode_npdu_network(BACNET_NPDU_DATA *npdu_data,
    BACNET_NETWORK_MESSAGE_TYPE network_message_type,
    bool data_expecting_reply,
    BACNET_MESSAGE_PRIORITY priority)
{
    if (npdu_data) {
        npdu_data->data_expecting_reply = data_expecting_reply;
        npdu_data->protocol_version = BACNET_PROTOCOL_VERSION;
        npdu_data->network_layer_message = true; /* false if APDU */
        npdu_data->network_message_type = network_message_type; /* optional */
        npdu_data->vendor_id = 0; /* optional, if net message type is > 0x80 */
        npdu_data->priority = priority;
        npdu_data->hop_count = HOP_COUNT_DEFAULT;
    }
}

/** Decode the NPDU portion of a received message, particularly the NCPI byte.
 *  The Network Layer Protocol Control Information byte is described
 *  in section 6.2.2 of the BACnet standard.
 * @param npdu [in] Buffer holding the received NPDU header bytes (must be at
 * least 2)
 * @param dest [out] Returned with routing destination information if the NPDU
 *                   has any and if this points to non-null storage for it.
 *                   If dest->net and dest->len are 0 on return, there is no
 *                   routing destination information.
 * @param src  [out] Returned with routing source information if the NPDU
 *                   has any and if this points to non-null storage for it.
 *                   If src->net and src->len are 0 on return, there is no
 *                   routing source information.
 *                   This src describes the original source of the message when
 *                   it had to be routed to reach this BACnet Device.
 * @param npdu_data [out] Returns a filled-out structure with information
 *                   decoded from the NCPI and other NPDU
 * bytes.
 * @return On success, returns the number of bytes which were decoded from the
 *         NPDU section; if this is a  network layer message, there may
 * be more bytes left in the NPDU; if not a network msg, the APDU follows. If 0
 * or negative, there were problems with the data or arguments.
 */
int npdu_decode(uint8_t *npdu,
    BACNET_ADDRESS *dest,
    BACNET_ADDRESS *src,
    BACNET_NPDU_DATA *npdu_data)
{
    return bacnet_npdu_decode(npdu, MAX_NPDU, dest, src, npdu_data);
}

/** Decode the NPDU portion of a received message, particularly the NCPI byte.
 *  The Network Layer Protocol Control Information byte is described
 *  in section 6.2.2 of the BACnet standard.
 * @param npdu [in] Buffer holding the received NPDU header bytes (must be at
 * least 2)
 * @param pdu_len [in] Length of the received data to prevent overruns.
 * @param dest [out] Returned with routing destination information if the NPDU
 *                   has any and if this points to non-null storage for it.
 *                   If dest->net and dest->len are 0 on return, there is no
 *                   routing destination information.
 * @param src  [out] Returned with routing source information if the NPDU
 *                   has any and if this points to non-null storage for it.
 *                   If src->net and src->len are 0 on return, there is no
 *                   routing source information.
 *                   This src describes the original source of the message when
 *                   it had to be routed to reach this BACnet Device.
 * @param npdu_data [out] Returns a filled-out structure with information
 * 					 decoded from the NCPI and other NPDU
 * bytes.
 * @return On success, returns the number of bytes which were decoded from the
 * 		   NPDU section; if this is a  network layer message, there may
 * be more bytes left in the NPDU; if not a network msg, the APDU follows. If 0
 * or negative, there were problems with the data or arguments.
 */
int bacnet_npdu_decode(uint8_t *npdu,
    uint16_t pdu_len,
    BACNET_ADDRESS *dest,
    BACNET_ADDRESS *src,
    BACNET_NPDU_DATA *npdu_data)
{
    int len = 0; /* return value - number of octets loaded in this function */
    uint8_t i = 0; /* counter */
    uint16_t src_net = 0;
    uint16_t dest_net = 0;
    uint8_t slen = 0;
    uint8_t dlen = 0;
    uint8_t mac_octet = 0;

    if (npdu && npdu_data && (pdu_len >= 2)) {
        /* Protocol Version */
        npdu_data->protocol_version = npdu[0];
        /* control octet */
        /* Bit 7: 1 indicates that the NSDU conveys a network layer message. */
        /*          Message Type field is present. */
        /*        0 indicates that the NSDU contains a BACnet APDU. */
        /*          Message Type field is absent. */
        npdu_data->network_layer_message = (npdu[1] & BIT(7)) ? true : false;
        /*Bit 6: Reserved. Shall be zero. */
        /* Bit 4: Reserved. Shall be zero. */
        /* Bit 2: The value of this bit corresponds to data expecting reply */
        /* parameter in the N-UNITDATA primitives. */
        /* 1 indicates that a BACnet-Confirmed-Request-PDU, */
        /* a segment of a BACnet-ComplexACK-PDU, */
        /* or a network layer message expecting a reply is present. */
        /* 0 indicates that other than a BACnet-Confirmed-Request-PDU, */
        /* a segment of a BACnet-ComplexACK-PDU, */
        /* or a network layer message expecting a reply is present. */
        npdu_data->data_expecting_reply = (npdu[1] & BIT(2)) ? true : false;
        /* Bits 1,0: Network priority where: */
        /* B'11' = Life Safety message */
        /* B'10' = Critical Equipment message */
        /* B'01' = Urgent message */
        /* B'00' = Normal message */
        npdu_data->priority = (BACNET_MESSAGE_PRIORITY)(npdu[1] & 0x03);
        /* set the offset to where the optional stuff starts */
        len = 2;
        /*Bit 5: Destination specifier where: */
        /* 0 = DNET, DLEN, DADR, and Hop Count absent */
        /* 1 = DNET, DLEN, and Hop Count present */
        /* DLEN = 0 denotes broadcast MAC DADR and DADR field is absent */
        /* DLEN > 0 specifies length of DADR field */
        if (npdu[1] & BIT(5)) {
            if (pdu_len >= (len + 3)) {
                len += decode_unsigned16(&npdu[len], &dest_net);
                /* DLEN = 0 denotes broadcast MAC DADR and DADR field is absent
                 */
                /* DLEN > 0 specifies length of DADR field */
                dlen = npdu[len++];
                if (dest) {
                    dest->net = dest_net;
                    dest->len = dlen;
                }
                if (dlen) {
                    if ((dlen > MAX_MAC_LEN) || (pdu_len < (len + dlen))) {
                        /* address is too large could be a malformed message */
                        return -1;
                    }

                    for (i = 0; i < dlen; i++) {
                        mac_octet = npdu[len++];
                        if (dest) {
                            dest->adr[i] = mac_octet;
                        }
                    }
                }
            }
        }
        /* zero out the destination address */
        else if (dest) {
            dest->net = 0;
            dest->len = 0;
            for (i = 0; i < MAX_MAC_LEN; i++) {
                dest->adr[i] = 0;
            }
        }
        /* Bit 3: Source specifier where: */
        /* 0 =  SNET, SLEN, and SADR absent */
        /* 1 =  SNET, SLEN, and SADR present */
        if (npdu[1] & BIT(3)) {
            if (pdu_len >= (len + 3)) {
                len += decode_unsigned16(&npdu[len], &src_net);
                /* SLEN = 0 denotes broadcast MAC SADR and SADR field is absent
                 */
                /* SLEN > 0 specifies length of SADR field */
                slen = npdu[len++];
                if (src) {
                    src->net = src_net;
                    src->len = slen;
                }
                if (slen) {
                    if ((slen > MAX_MAC_LEN) || (pdu_len < (len + slen))) {
                        /* address is too large could be a malformed message */
                        return -1;
                    }

                    for (i = 0; i < slen; i++) {
                        mac_octet = npdu[len++];
                        if (src) {
                            src->adr[i] = mac_octet;
                        }
                    }
                }
            }
        } else if (src) {
            /* Clear the net number, with one exception: if the receive()
             * function set it to BACNET_BROADCAST_NETWORK, (eg, for
             * BVLC_ORIGINAL_BROADCAST_NPDU) then don't stomp on that.
             */
            if (src->net != BACNET_BROADCAST_NETWORK) {
                src->net = 0;
            }
            src->len = 0;
            for (i = 0; i < MAX_MAC_LEN; i++) {
                src->adr[i] = 0;
            }
        }
        /* The Hop Count field shall be present only if the message is */
        /* destined for a remote network, i.e., if DNET is present. */
        /* This is a one-octet field that is initialized to a value of 0xff. */
        if (dest_net) {
            if (pdu_len > len) {
                npdu_data->hop_count = npdu[len++];
            } else {
                npdu_data->hop_count = 0;
            }
        } else {
            npdu_data->hop_count = 0;
        }
        /* Indicates that the NSDU conveys a network layer message. */
        /* Message Type field is present. */
        if (npdu_data->network_layer_message) {
            if (pdu_len > len) {
                npdu_data->network_message_type =
                    (BACNET_NETWORK_MESSAGE_TYPE)npdu[len++];
                /* Message Type field contains a value in the range 0x80 - 0xFF,
                 */
                /* then a Vendor ID field shall be present */
                if (npdu_data->network_message_type >= 0x80) {
                    if (pdu_len >= (len + 2)) {
                        len += decode_unsigned16(
                            &npdu[len], &npdu_data->vendor_id);
                    }
                }
            }
        } else {
            /* Since npdu_data->network_layer_message is false,
             * it doesn't much matter what we set here; this is safe: */
            npdu_data->network_message_type = NETWORK_MESSAGE_INVALID;
        }
    }

    return len;
}

/**
 * @brief Helper for datalink detecting an application confirmed service
 * @param pdu [in]  Buffer containing the NPDU and APDU of the received packet.
 * @param pdu_len [in] The size of the received message in the pdu[] buffer.
 * @return true if the PDU is a confirmed APDU
 */
bool npdu_confirmed_service(uint8_t *pdu, uint16_t pdu_len)
{
    bool status = false;
    int apdu_offset = 0;
    BACNET_NPDU_DATA npdu_data = { 0 };

    if (pdu_len > 0) {
        if (pdu[0] == BACNET_PROTOCOL_VERSION) {
            /* only handle the version that we know how to handle */
            apdu_offset =
                bacnet_npdu_decode(&pdu[0], pdu_len, NULL, NULL, &npdu_data);
            if ((!npdu_data.network_layer_message) && (apdu_offset > 0) &&
                (apdu_offset < pdu_len)) {
                if ((pdu[apdu_offset] & 0xF0) ==
                    PDU_TYPE_CONFIRMED_SERVICE_REQUEST) {
                    status = true;
                }
            }
        }
    }

    return status;
}
