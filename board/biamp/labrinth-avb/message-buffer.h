#ifndef __MESSAGE_BUFFER_H__
#define __MESSAGE_BUFFER_H__

#include <linux/types.h>

#define MAX_MSG_BUF_SIZE 1024

typedef unsigned char RequestMessageBuffer_t[MAX_MSG_BUF_SIZE];
typedef unsigned char ResponseMessageBuffer_t[MAX_MSG_BUF_SIZE];
typedef unsigned char MessageBuffer_t[MAX_MSG_BUF_SIZE];

typedef char * string_t;

typedef struct 
{
  uint32_t m_size;
  uint8_t m_data[MAX_MSG_BUF_SIZE];
} sequence_t_uint8_t;

// Common constants
typedef enum 
{
    k_CC_FirmwareUpdate       = 1
} ClassCode;

typedef enum 
{
    k_SC_getAttribute = 1,
    k_SC_setAttribute = 2,

} CommonServiceCode;

typedef enum 
{
    k_SC_startFirmwareUpdate= 1000,
    k_SC_sendDataPacket,
} FirmwareUpdateServiceCode;

extern void     setClassCode_req(RequestMessageBuffer_t msg, uint16_t classCode);
extern void     setInstanceNumber_req(RequestMessageBuffer_t msg, uint16_t instanceNumber);
extern void     setServiceCode_req(RequestMessageBuffer_t msg, uint16_t serviceCode);
extern void     setAttributeCode_req(RequestMessageBuffer_t msg, uint16_t attributeCode);
extern uint16_t getPayloadOffset_req(RequestMessageBuffer_t msg);
extern void     setLength_req(RequestMessageBuffer_t msg, uint16_t length);
extern uint16_t getServiceCode_req(RequestMessageBuffer_t msg);
extern uint16_t getAttributeCode_req(RequestMessageBuffer_t msg);

extern void     setLength_resp(RequestMessageBuffer_t msg, uint16_t length);
extern uint16_t getStatusCode_resp(RequestMessageBuffer_t msg); 
extern uint16_t getPayloadOffset_resp(RequestMessageBuffer_t msg);

extern uint16_t sequence_t_uint8_t_marshal(RequestMessageBuffer_t request, uint32_t offset, sequence_t_uint8_t *data);
extern uint16_t sequence_t_uint8_t_unmarshal(RequestMessageBuffer_t request, uint32_t offset, sequence_t_uint8_t *data);
extern uint16_t string_t_unmarshal(RequestMessageBuffer_t request, uint32_t offset, string_t *str);
extern uint32_t uint8_t_marshal(MessageBuffer_t msg, uint32_t offset, uint8_t value);
extern uint32_t uint8_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint8_t *value);
extern uint32_t uint16_t_marshal(MessageBuffer_t msg, uint32_t offset, uint16_t value);
extern uint32_t uint16_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint16_t *value);
extern uint32_t uint32_t_marshal(MessageBuffer_t msg, uint32_t offset, uint32_t value);
extern uint32_t uint32_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint32_t *value);
extern uint32_t bool_marshal(MessageBuffer_t msg, uint32_t offset, uint8_t value);
extern void     SendMessage(MessageBuffer_t request, MessageBuffer_t response);
#endif
