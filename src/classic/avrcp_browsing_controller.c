/*
 * Copyright (C) 2016 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define __BTSTACK_FILE__ "avrcp_browsing_controller.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack.h"
#include "classic/avrcp.h"
#include "classic/avrcp_browsing_controller.h"

#define PSM_AVCTP_BROWSING              0x001b

void avrcp_browser_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size, avrcp_context_t * context);
static void avrcp_browsing_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static avrcp_connection_t * get_avrcp_connection_for_browsing_cid(uint16_t browsing_cid, avrcp_context_t * context){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *)  &context->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        avrcp_connection_t * connection = (avrcp_connection_t *)btstack_linked_list_iterator_next(&it);
        if (connection->avrcp_browsing_cid != browsing_cid) continue;
        return connection;
    }
    return NULL;
}

static avrcp_connection_t * get_avrcp_connection_for_browsing_l2cap_cid(uint16_t browsing_l2cap_cid, avrcp_context_t * context){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *)  &context->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        avrcp_connection_t * connection = (avrcp_connection_t *)btstack_linked_list_iterator_next(&it);
        if (connection->browsing_connection &&  connection->browsing_connection->l2cap_browsing_cid != browsing_l2cap_cid) continue;
        return connection;
    }
    return NULL;
}

static avrcp_browsing_connection_t * get_avrcp_browsing_connection_for_l2cap_cid(uint16_t l2cap_cid, avrcp_context_t * context){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *)  &context->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        avrcp_connection_t * connection = (avrcp_connection_t *)btstack_linked_list_iterator_next(&it);
        if (connection->browsing_connection && connection->browsing_connection->l2cap_browsing_cid != l2cap_cid) continue;
        return connection->browsing_connection;
    }
    return NULL;
}

static void avrcp_emit_browsing_connection_established(btstack_packet_handler_t callback, uint16_t browsing_cid, bd_addr_t addr, uint8_t status){
    if (!callback) return;
    uint8_t event[12];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVRCP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVRCP_SUBEVENT_BROWSING_CONNECTION_ESTABLISHED;
    event[pos++] = status;
    reverse_bd_addr(addr,&event[pos]);
    pos += 6;
    little_endian_store_16(event, pos, browsing_cid);
    pos += 2;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static void avrcp_emit_incoming_browsing_connection(btstack_packet_handler_t callback, uint16_t browsing_cid, bd_addr_t addr){
    if (!callback) return;
    uint8_t event[11];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVRCP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVRCP_SUBEVENT_INCOMING_BROWSING_CONNECTION;
    reverse_bd_addr(addr,&event[pos]);
    pos += 6;
    little_endian_store_16(event, pos, browsing_cid);
    pos += 2;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static void avrcp_emit_browsing_connection_closed(btstack_packet_handler_t callback, uint16_t browsing_cid){
    if (!callback) return;
    uint8_t event[5];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVRCP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVRCP_SUBEVENT_BROWSING_CONNECTION_RELEASED;
    little_endian_store_16(event, pos, browsing_cid);
    pos += 2;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static avrcp_browsing_connection_t * avrcp_browsing_create_connection(avrcp_connection_t * avrcp_connection){
    avrcp_browsing_connection_t * connection = btstack_memory_avrcp_browsing_connection_get();
    memset(connection, 0, sizeof(avrcp_browsing_connection_t));
    connection->state = AVCTP_CONNECTION_IDLE;
    connection->transaction_label = 0xFF;
    avrcp_connection->avrcp_browsing_cid = avrcp_get_next_cid();
    avrcp_connection->browsing_connection = connection;
    return connection;
}

static uint8_t avrcp_browsing_connect(bd_addr_t remote_addr, avrcp_context_t * context, uint8_t * ertm_buffer, uint32_t size, l2cap_ertm_config_t * ertm_config, uint16_t * browsing_cid){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_bd_addr(remote_addr, context);
    
    if (!avrcp_connection){
        log_error("avrcp: there is no previously established AVRCP controller connection.");
        return ERROR_CODE_COMMAND_DISALLOWED;
    }

    avrcp_browsing_connection_t * connection = avrcp_connection->browsing_connection;
    if (connection){
        log_error(" avrcp_browsing_connect connection exists.");
        return ERROR_CODE_SUCCESS;
    }
    
    connection = avrcp_browsing_create_connection(avrcp_connection);
    if (!connection){
        log_error("avrcp: could not allocate connection struct.");
        return BTSTACK_MEMORY_ALLOC_FAILED;
    }
    
    if (!browsing_cid) return L2CAP_LOCAL_CID_DOES_NOT_EXIST;
    
    *browsing_cid = avrcp_connection->avrcp_browsing_cid; 
    connection->ertm_buffer = ertm_buffer;
    connection->ertm_buffer_size = size;
    avrcp_connection->browsing_connection = connection;

    memcpy(&connection->ertm_config, ertm_config, sizeof(l2cap_ertm_config_t));

    return l2cap_create_ertm_channel(avrcp_browsing_controller_packet_handler, remote_addr, avrcp_connection->browsing_l2cap_psm, 
                    &connection->ertm_config, connection->ertm_buffer, connection->ertm_buffer_size, NULL);

}

void avrcp_browser_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size, avrcp_context_t * context){
    UNUSED(channel);
    UNUSED(size);
    bd_addr_t event_addr;
    uint16_t local_cid;
    uint8_t  status;
    avrcp_browsing_connection_t * browsing_connection = NULL;
    avrcp_connection_t * avrcp_connection = NULL;
    
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            avrcp_emit_browsing_connection_closed(context->browsing_avrcp_callback, 0);
            break;
        case L2CAP_EVENT_INCOMING_CONNECTION:
            l2cap_event_incoming_connection_get_address(packet, event_addr);
            local_cid = l2cap_event_incoming_connection_get_local_cid(packet);
            avrcp_connection = get_avrcp_connection_for_bd_addr(event_addr, context);
            if (!avrcp_connection) {
                log_error("No previously created AVRCP controller connections");
                l2cap_decline_connection(local_cid);
                break;
            }
            browsing_connection = avrcp_browsing_create_connection(avrcp_connection);
            browsing_connection->l2cap_browsing_cid = local_cid;
            browsing_connection->state = AVCTP_CONNECTION_W4_ERTM_CONFIGURATION;
            log_info("Emit AVRCP_SUBEVENT_INCOMING_BROWSING_CONNECTION browsing_cid 0x%02x, l2cap_signaling_cid 0x%02x\n", avrcp_connection->avrcp_browsing_cid, browsing_connection->l2cap_browsing_cid);
            avrcp_emit_incoming_browsing_connection(context->browsing_avrcp_callback, avrcp_connection->avrcp_browsing_cid, event_addr);
            break;
            
        case L2CAP_EVENT_CHANNEL_OPENED:
            l2cap_event_channel_opened_get_address(packet, event_addr);
            status = l2cap_event_channel_opened_get_status(packet);
            local_cid = l2cap_event_channel_opened_get_local_cid(packet);
            
            avrcp_connection = get_avrcp_connection_for_bd_addr(event_addr, context);
            if (!avrcp_connection){
                log_error("Failed to find AVRCP connection for bd_addr %s", bd_addr_to_str(event_addr));
                avrcp_emit_browsing_connection_established(context->browsing_avrcp_callback, local_cid, event_addr, L2CAP_LOCAL_CID_DOES_NOT_EXIST);
                l2cap_disconnect(local_cid, 0); // reason isn't used
                break;
            }

            browsing_connection = avrcp_connection->browsing_connection;
            if (status != ERROR_CODE_SUCCESS){
                log_info("L2CAP connection to connection %s failed. status code 0x%02x", bd_addr_to_str(event_addr), status);
                avrcp_emit_browsing_connection_established(context->browsing_avrcp_callback, avrcp_connection->avrcp_browsing_cid, event_addr, status);
                btstack_memory_avrcp_browsing_connection_free(browsing_connection);
                avrcp_connection->browsing_connection = NULL;
                break;
            }
            if (browsing_connection->state != AVCTP_CONNECTION_W4_L2CAP_CONNECTED) break;
            
            browsing_connection->l2cap_browsing_cid = local_cid;

            log_info("L2CAP_EVENT_CHANNEL_OPENED browsing cid 0x%02x, l2cap cid 0x%02x", avrcp_connection->avrcp_browsing_cid, browsing_connection->l2cap_browsing_cid);
            browsing_connection->state = AVCTP_CONNECTION_OPENED;
            avrcp_emit_browsing_connection_established(context->browsing_avrcp_callback, avrcp_connection->avrcp_browsing_cid, event_addr, ERROR_CODE_SUCCESS);
            break;
        
        case L2CAP_EVENT_CHANNEL_CLOSED:
            // data: event (8), len(8), channel (16)
            local_cid = l2cap_event_channel_closed_get_local_cid(packet);
            avrcp_connection = get_avrcp_connection_for_browsing_l2cap_cid(local_cid, context);
            
            if (avrcp_connection && avrcp_connection->browsing_connection){
                avrcp_emit_browsing_connection_closed(context->browsing_avrcp_callback, avrcp_connection->avrcp_browsing_cid);
                // free connection
                btstack_memory_avrcp_browsing_connection_free(avrcp_connection->browsing_connection);
                avrcp_connection->browsing_connection = NULL;
                break;
            }
            break;
        default:
            break;
    }
}

static int avrcp_browsing_controller_send_get_folder_items_cmd(uint16_t cid, avrcp_browsing_connection_t * connection){
    uint8_t command[100];
    int pos = 0; 
    // transport header
    // Transaction label | Packet_type | C/R | IPID (1 == invalid profile identifier)
    command[pos++] = (connection->transaction_label << 4) | (AVRCP_SINGLE_PACKET << 2) | (AVRCP_COMMAND_FRAME << 1) | 0;
    // Profile IDentifier (PID)
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL >> 8;
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL & 0x00FF;
    command[pos++] = AVRCP_PDU_ID_GET_FOLDER_ITEMS;

    uint32_t attribute_count = 0;
    uint32_t attributes_to_copy = 0;

    switch (connection->attr_bitmap){
        case AVRCP_MEDIA_ATTR_NONE:
            attribute_count = AVRCP_MEDIA_ATTR_NONE; // 0xFFFFFFFF
            break;
        case AVRCP_MEDIA_ATTR_ALL:
            attribute_count = AVRCP_MEDIA_ATTR_ALL;  // 0
            break;
        default:
            attribute_count = count_set_bits_uint32(connection->attr_bitmap & 0xff);
            attributes_to_copy = attribute_count;
            break;
    }
    
    big_endian_store_16(command, pos, 10 + attribute_count);
    pos += 2;
    command[pos++] = connection->scope;
    big_endian_store_32(command, pos, connection->start_item);
    pos += 4;
    big_endian_store_32(command, pos, connection->end_item);
    pos += 4;
    command[pos++] = attribute_count;
    
    int bit_position = 1;
    while (attributes_to_copy){
        if (connection->attr_bitmap & (1 << bit_position)){
            big_endian_store_32(command, pos, bit_position);
            pos += 4;
            attributes_to_copy--;
        }
        bit_position++;
    }
    
    return l2cap_send(cid, command, pos);
}

static int avrcp_browsing_controller_send_change_path_cmd(uint16_t cid, avrcp_browsing_connection_t * connection){
    uint8_t command[100];
    int pos = 0; 
    // transport header
    // Transaction label | Packet_type | C/R | IPID (1 == invalid profile identifier)
    command[pos++] = (connection->transaction_label << 4) | (AVRCP_SINGLE_PACKET << 2) | (AVRCP_COMMAND_FRAME << 1) | 0;
    // Profile IDentifier (PID)
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL >> 8;
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL & 0x00FF;
    command[pos++] = AVRCP_PDU_ID_CHANGE_PATH;

    big_endian_store_16(command, pos, 11);
    pos += 2;
    big_endian_store_16(command, pos, connection->browsed_player_uid_counter);
    pos += 2;
    command[pos++] = connection->direction;
    memcpy(command+pos, connection->folder_uid, 8);
    pos += 8;
    return l2cap_send(cid, command, pos);
}

static int avrcp_browsing_controller_send_set_browsed_player_cmd(uint16_t cid, avrcp_browsing_connection_t * connection){
    uint8_t command[100];
    int pos = 0; 
    // transport header
    // Transaction label | Packet_type | C/R | IPID (1 == invalid profile identifier)
    command[pos++] = (connection->transaction_label << 4) | (AVRCP_SINGLE_PACKET << 2) | (AVRCP_COMMAND_FRAME << 1) | 0;
    // Profile IDentifier (PID)
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL >> 8;
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL & 0x00FF;
    command[pos++] = AVRCP_PDU_ID_SET_BROWSED_PLAYER;

    big_endian_store_16(command, pos, 2);
    pos += 2;
    big_endian_store_16(command, pos, connection->browsed_player_id);
    pos += 2;
    return l2cap_send(cid, command, pos);
}

static int avrcp_browsing_controller_send_set_addressed_player_cmd(uint16_t cid, avrcp_browsing_connection_t * connection){
    uint8_t command[100];
    int pos = 0; 
    // transport header
    // Transaction label | Packet_type | C/R | IPID (1 == invalid profile identifier)
    command[pos++] = (connection->transaction_label << 4) | (AVRCP_SINGLE_PACKET << 2) | (AVRCP_COMMAND_FRAME << 1) | 0;
    // Profile IDentifier (PID)
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL >> 8;
    command[pos++] = BLUETOOTH_SERVICE_CLASS_AV_REMOTE_CONTROL & 0x00FF;
    command[pos++] = AVRCP_PDU_ID_SET_ADDRESSED_PLAYER;

    big_endian_store_16(command, pos, 2);
    pos += 2;
    big_endian_store_16(command, pos, connection->addressed_player_id);
    pos += 2;
    return l2cap_send(cid, command, pos);
}

static void avrcp_browsing_controller_handle_can_send_now(avrcp_browsing_connection_t * connection){
    switch (connection->state){
        case AVCTP_CONNECTION_OPENED:
            if (connection->set_browsed_player_id){
                connection->state = AVCTP_W2_RECEIVE_RESPONSE;
                connection->set_browsed_player_id = 0;
                avrcp_browsing_controller_send_set_browsed_player_cmd(connection->l2cap_browsing_cid, connection);
                break;
            }            

            if (connection->set_addressed_player_id){
                connection->state = AVCTP_W2_RECEIVE_RESPONSE;
                connection->set_addressed_player_id = 0;
                avrcp_browsing_controller_send_set_addressed_player_cmd(connection->l2cap_browsing_cid, connection);
                break;
            }

            if (connection->get_folder_item){
                connection->state = AVCTP_W2_RECEIVE_RESPONSE;
                connection->get_folder_item = 0;
                avrcp_browsing_controller_send_get_folder_items_cmd(connection->l2cap_browsing_cid, connection);
                break;
            }
            if (connection->change_path){
                connection->state = AVCTP_W2_RECEIVE_RESPONSE;
                connection->change_path = 0;
                avrcp_browsing_controller_send_change_path_cmd(connection->l2cap_browsing_cid, connection);
                break;
            }
            
        default:
            return;
    }
}

static void avrcp_browsing_controller_emit_done(btstack_packet_handler_t callback, uint16_t browsing_cid, uint8_t browsing_status, uint8_t bluetooth_status){
    if (!callback) return;
    uint8_t event[7];
    int pos = 0;
    event[pos++] = HCI_EVENT_AVRCP_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = AVRCP_SUBEVENT_BROWSING_MEDIA_ITEM_DONE;
    little_endian_store_16(event, pos, browsing_cid);
    pos += 2;
    event[pos++] = browsing_status;
    event[pos++] = bluetooth_status;
    (*callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static void avrcp_browsing_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    avrcp_browsing_connection_t * browsing_connection;
            
    switch (packet_type) {
        case L2CAP_DATA_PACKET:{
            browsing_connection = get_avrcp_browsing_connection_for_l2cap_cid(channel, &avrcp_controller_context);
            if (!browsing_connection) break;
            browsing_connection->state = AVCTP_CONNECTION_OPENED;

            // printf_hexdump(packet, size);
            int pos = 0;
            uint8_t transport_header = packet[pos++];
            // Transaction label | Packet_type | C/R | IPID (1 == invalid profile identifier)
            // uint8_t transaction_label = transport_header >> 4;
            avrcp_packet_type_t avctp_packet_type = (transport_header & 0x0F) >> 2;
            // uint8_t frame_type = (transport_header & 0x03) >> 1;
            // uint8_t ipid = transport_header & 0x01;
            pos += 2;
            browsing_connection->num_packets = 1;
            switch (avctp_packet_type){
                case AVRCP_SINGLE_PACKET:
                    break;
                case AVRCP_START_PACKET:
                    browsing_connection->num_packets = packet[pos++];
                    break;
                case AVRCP_CONTINUE_PACKET:
                case AVRCP_END_PACKET:
                    // browsing_connection->num_packets = packet[pos++];
                    break;
            }
    
            if (pos + 4 > size){
                avrcp_browsing_controller_emit_done(avrcp_controller_context.browsing_avrcp_callback, channel, AVRCP_BROWSING_ERROR_CODE_INVALID_COMMAND, ERROR_CODE_SUCCESS);
                return;  
            }
            
            avrcp_pdu_id_t pdu_id = packet[pos++];
            uint16_t length = big_endian_read_16(packet, pos);
            pos += 2;
            UNUSED(length);
            // if (browsing_connection->num_packets > 1)
            // if (browsing_connection->num_packets == 1 && (pos + length > size)){
            //     printf("pos + length > size, %d, %d, %d \n", pos, length, size);
            //     avrcp_browsing_controller_emit_done(avrcp_controller_context.browsing_avrcp_callback, channel, AVRCP_BROWSING_ERROR_CODE_INVALID_COMMAND, ERROR_CODE_SUCCESS);
            //     return;  
            // }
            
            uint8_t browsing_status = packet[pos++]; 
            if (browsing_status != AVRCP_BROWSING_ERROR_CODE_SUCCESS){
                printf("browsing_status %d\n", browsing_status);
                avrcp_browsing_controller_emit_done(avrcp_controller_context.browsing_avrcp_callback, channel, browsing_status, ERROR_CODE_SUCCESS);
                return;        
            }
            
            uint32_t i;
            switch(pdu_id){
                case AVRCP_PDU_ID_CHANGE_PATH:
                    printf("AVRCP_PDU_ID_CHANGE_PATH \n");
                    break;
                case AVRCP_PDU_ID_SET_ADDRESSED_PLAYER:
                    printf("AVRCP_PDU_ID_CHANGE_PATH \n");
                    break;
                case AVRCP_PDU_ID_SET_BROWSED_PLAYER:{
                    browsing_connection->browsed_player_uid_counter = big_endian_read_16(packet, pos);
                    pos += 2;
                    uint32_t num_items = big_endian_read_32(packet, pos);
                    pos += 4;
                    printf("AVRCP_PDU_ID_SET_BROWSED_PLAYER uuid counter 0x0%2x, num items %d\n", browsing_connection->browsed_player_uid_counter, num_items);
                    
                    for (i = 0; i < num_items; i++){
                        uint16_t browsable_item_length = 5 + big_endian_read_16(packet, pos+3);
                        printf(" pos %d, len %d\n", pos, browsable_item_length);
                        // reuse byte to put the new type AVRCP_BROWSING_MEDIA_ROOT_FOLDER
                        packet[pos-1] = AVRCP_BROWSING_MEDIA_ROOT_FOLDER;
                        (*avrcp_controller_context.browsing_avrcp_callback)(AVRCP_BROWSING_DATA_PACKET, channel, packet+pos, browsable_item_length+1);
                        pos += browsable_item_length;
                    }
                    break;
                }
                case AVRCP_PDU_ID_GET_FOLDER_ITEMS:{
                    // uint16_t uid_counter = big_endian_read_16(packet, pos);
                    pos += 2;
                    uint16_t num_items = big_endian_read_16(packet, pos);
                    pos += 2;
                    for (i = 0; i < num_items; i++){
                        uint16_t browsable_item_length = 3 + big_endian_read_16(packet, pos+1);
                        (*avrcp_controller_context.browsing_avrcp_callback)(AVRCP_BROWSING_DATA_PACKET, channel, packet+pos, browsable_item_length);
                        pos += browsable_item_length;
                    }               
                    break;
                }
                default:
                    break;
            }
            switch (avctp_packet_type){
                case AVRCP_SINGLE_PACKET:
                case AVRCP_END_PACKET:
                    avrcp_browsing_controller_emit_done(avrcp_controller_context.browsing_avrcp_callback, channel, browsing_status, ERROR_CODE_SUCCESS);
                    break;
                default:
                    break;
            }
            break;
        }
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)){
                case L2CAP_EVENT_CAN_SEND_NOW:
                    browsing_connection = get_avrcp_browsing_connection_for_l2cap_cid(channel, &avrcp_controller_context);
                    if (!browsing_connection) break;
                    avrcp_browsing_controller_handle_can_send_now(browsing_connection);
                    break;
            default:
                avrcp_browser_packet_handler(packet_type, channel, packet, size, &avrcp_controller_context);
                break;
        }
        default:
            break;
    }
}

void avrcp_browsing_controller_init(void){
    avrcp_controller_context.browsing_packet_handler = avrcp_browsing_controller_packet_handler;
    l2cap_register_service(&avrcp_browsing_controller_packet_handler, PSM_AVCTP_BROWSING, 0xffff, LEVEL_0);
}

void avrcp_browsing_controller_register_packet_handler(btstack_packet_handler_t callback){
    if (callback == NULL){
        log_error("avrcp_browsing_controller_register_packet_handler called with NULL callback");
        return;
    }
    avrcp_controller_context.browsing_avrcp_callback = callback;
}

uint8_t avrcp_browsing_controller_connect(bd_addr_t bd_addr, uint8_t * ertm_buffer, uint32_t size, l2cap_ertm_config_t * ertm_config, uint16_t * avrcp_browsing_cid){
    return avrcp_browsing_connect(bd_addr, &avrcp_controller_context, ertm_buffer, size, ertm_config, avrcp_browsing_cid);
}

uint8_t avrcp_browsing_controller_disconnect(uint16_t avrcp_browsing_cid){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_browsing_cid(avrcp_browsing_cid, &avrcp_controller_context);
    if (!avrcp_connection){
        log_error("avrcp_browsing_controller_disconnect: could not find a connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    if (avrcp_connection->browsing_connection->state != AVCTP_CONNECTION_OPENED) return ERROR_CODE_COMMAND_DISALLOWED;
    
    l2cap_disconnect(avrcp_connection->browsing_connection->l2cap_browsing_cid, 0);
    return ERROR_CODE_SUCCESS;
}

uint8_t avrcp_avrcp_browsing_configure_incoming_connection(uint16_t avrcp_browsing_cid, uint8_t * ertm_buffer, uint32_t size, l2cap_ertm_config_t * ertm_config){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_browsing_cid(avrcp_browsing_cid, &avrcp_controller_context);
    if (!avrcp_connection){
        log_error("avrcp_avrcp_browsing_decline_incoming_connection: could not find a connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    if (!avrcp_connection->browsing_connection){
        log_error("avrcp_avrcp_browsing_decline_incoming_connection: no browsing connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    } 

    if (avrcp_connection->browsing_connection->state != AVCTP_CONNECTION_W4_ERTM_CONFIGURATION){
        log_error("avrcp_avrcp_browsing_decline_incoming_connection: browsing connection in a wrong state.");
        return ERROR_CODE_COMMAND_DISALLOWED;
    } 

    avrcp_connection->browsing_connection->state = AVCTP_CONNECTION_W4_L2CAP_CONNECTED;
    avrcp_connection->browsing_connection->ertm_buffer = ertm_buffer;
    avrcp_connection->browsing_connection->ertm_buffer_size = size;
    memcpy(&avrcp_connection->browsing_connection->ertm_config, ertm_config, sizeof(l2cap_ertm_config_t));
    l2cap_accept_ertm_connection(avrcp_connection->browsing_connection->l2cap_browsing_cid, &avrcp_connection->browsing_connection->ertm_config, avrcp_connection->browsing_connection->ertm_buffer, avrcp_connection->browsing_connection->ertm_buffer_size);
    return ERROR_CODE_SUCCESS;
}

uint8_t avrcp_avrcp_browsing_decline_incoming_connection(uint16_t avrcp_browsing_cid){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_browsing_cid(avrcp_browsing_cid, &avrcp_controller_context);
    if (!avrcp_connection){
        log_error("avrcp_avrcp_browsing_decline_incoming_connection: could not find a connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    if (!avrcp_connection->browsing_connection) return ERROR_CODE_SUCCESS;
    if (avrcp_connection->browsing_connection->state > AVCTP_CONNECTION_W4_ERTM_CONFIGURATION) return ERROR_CODE_COMMAND_DISALLOWED;
    
    l2cap_decline_connection(avrcp_connection->browsing_connection->l2cap_browsing_cid);
    // free connection
    btstack_memory_avrcp_browsing_connection_free(avrcp_connection->browsing_connection);
    avrcp_connection->browsing_connection = NULL;
    return ERROR_CODE_SUCCESS;
}

/**
 * @brief Retrieve a listing of the contents of a folder.
 * @param scope    0-player list, 1-virtual file system, 2-search, 3-now playing  
 * @param start_item
 * @param end_item
 * @param attribute_count
 * @param attribute_list
 **/
static uint8_t avrcp_browsing_controller_get_folder_items(uint16_t avrcp_browsing_cid, uint8_t scope, uint32_t start_item, uint32_t end_item, uint32_t attr_bitmap){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_browsing_cid(avrcp_browsing_cid, &avrcp_controller_context);
    if (!avrcp_connection){
        log_error("avrcp_browsing_controller_disconnect: could not find a connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    avrcp_browsing_connection_t * connection = avrcp_connection->browsing_connection;
    if (connection->state != AVCTP_CONNECTION_OPENED) return ERROR_CODE_COMMAND_DISALLOWED;

    connection->get_folder_item = 1;
    connection->scope = scope;
    connection->start_item = start_item;
    connection->end_item = end_item;
    connection->attr_bitmap = attr_bitmap;

    avrcp_request_can_send_now(avrcp_connection, connection->l2cap_browsing_cid);
    return ERROR_CODE_SUCCESS;
}

uint8_t avrcp_browsing_controller_get_media_players(uint16_t avrcp_browsing_cid, uint32_t start_item, uint32_t end_item, uint32_t attr_bitmap){
    return avrcp_browsing_controller_get_folder_items(avrcp_browsing_cid, 0, start_item, end_item, attr_bitmap);
}

uint8_t avrcp_browsing_controller_browse_file_system(uint16_t avrcp_browsing_cid, uint32_t start_item, uint32_t end_item, uint32_t attr_bitmap){
    // return avrcp_browsing_controller_get_folder_items(avrcp_browsing_cid, 1, 0, 0xFFFFFFFF, attr_bitmap);
    return avrcp_browsing_controller_get_folder_items(avrcp_browsing_cid, 1, start_item, end_item, attr_bitmap);
}

uint8_t avrcp_browsing_controller_browse_media(uint16_t avrcp_browsing_cid, uint32_t start_item, uint32_t end_item, uint32_t attr_bitmap){
    // return avrcp_browsing_controller_get_folder_items(avrcp_browsing_cid, 2, 0, 0xFFFFFFFF, 0, NULL);
    return avrcp_browsing_controller_get_folder_items(avrcp_browsing_cid, 2, start_item, end_item, attr_bitmap);
}

uint8_t avrcp_browsing_controller_browse_now_playing_list(uint16_t avrcp_browsing_cid, uint32_t start_item, uint32_t end_item, uint32_t attr_bitmap){
    return avrcp_browsing_controller_get_folder_items(avrcp_browsing_cid, 3, start_item, end_item, attr_bitmap);
}


uint8_t avrcp_browsing_controller_set_browsed_player(uint16_t avrcp_browsing_cid, uint16_t browsed_player_id){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_browsing_cid(avrcp_browsing_cid, &avrcp_controller_context);
    if (!avrcp_connection){
        log_error("avrcp_browsing_controller_change_path: could not find a connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    
    avrcp_browsing_connection_t * connection = avrcp_connection->browsing_connection;
    if (connection->state != AVCTP_CONNECTION_OPENED){
        log_error("avrcp_browsing_controller_change_path: connection in wrong state.");
        return ERROR_CODE_COMMAND_DISALLOWED;
    } 

    connection->set_browsed_player_id = 1;
    connection->browsed_player_id = browsed_player_id;
    avrcp_request_can_send_now(avrcp_connection, connection->l2cap_browsing_cid);
    return ERROR_CODE_SUCCESS;
}

uint8_t avrcp_browsing_controller_set_addressed_player(uint16_t avrcp_browsing_cid, uint16_t addressed_player_id){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_browsing_cid(avrcp_browsing_cid, &avrcp_controller_context);
    if (!avrcp_connection){
        log_error("avrcp_browsing_controller_change_path: could not find a connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    
    avrcp_browsing_connection_t * connection = avrcp_connection->browsing_connection;
    if (!connection || connection->state != AVCTP_CONNECTION_OPENED){
        log_error("avrcp_browsing_controller_change_path: connection in wrong state.");
        return ERROR_CODE_COMMAND_DISALLOWED;
    } 

    connection->set_addressed_player_id = 1;
    connection->addressed_player_id = addressed_player_id;
    avrcp_request_can_send_now(avrcp_connection, connection->l2cap_browsing_cid);
    return ERROR_CODE_SUCCESS;
}

/**
 * @brief Retrieve a listing of the contents of a folder.
 * @param direction     0-folder up, 1-folder down    
 * @param folder_uid    8 bytes long
 **/
uint8_t avrcp_browsing_controller_change_path(uint16_t avrcp_browsing_cid, uint8_t direction, uint8_t * folder_uid){
    avrcp_connection_t * avrcp_connection = get_avrcp_connection_for_browsing_cid(avrcp_browsing_cid, &avrcp_controller_context);
    if (!avrcp_connection){
        log_error("avrcp_browsing_controller_change_path: could not find a connection.");
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    
    avrcp_browsing_connection_t * connection = avrcp_connection->browsing_connection;
    
    if (!connection || connection->state != AVCTP_CONNECTION_OPENED || !folder_uid){
        log_error("avrcp_browsing_controller_change_path: connection in wrong state.");
        return ERROR_CODE_COMMAND_DISALLOWED;
    } 

    if (!connection->browsed_player_id){
        log_error("avrcp_browsing_controller_change_path: no browsed player set.");
        return ERROR_CODE_COMMAND_DISALLOWED;
    }

    connection->change_path = 1;
    connection->direction = direction;

    memcpy(connection->folder_uid, folder_uid, 8);
    avrcp_request_can_send_now(avrcp_connection, connection->l2cap_browsing_cid);
    return ERROR_CODE_SUCCESS;
}

uint8_t avrcp_browsing_controller_go_up_one_level(uint16_t avrcp_browsing_cid){
    return avrcp_browsing_controller_change_path(avrcp_browsing_cid, 0, 0);
}

uint8_t avrcp_browsing_controller_go_down_one_level(uint16_t avrcp_browsing_cid, uint8_t * folder_uid){
    return avrcp_browsing_controller_change_path(avrcp_browsing_cid, 1, folder_uid);
}
