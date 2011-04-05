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

// First class-specific service or attribute codes to be used for
// any class.  Codes below this value are reserved for any generic
// facilities.
#define MIN_SERVICE_CODE    (0x1000)
#define MIN_ATTRIBUTE_CODE  (0x8000)

// Define a hard class code for firmware update.  This maps beyond the
// Labrinth-specific IDL class codes used within Linux.
typedef enum {
  k_CC_FirmwareUpdate = 256
} ClassCode;

typedef enum {
  k_SC_getAttribute = 1,
  k_SC_setAttribute = 2,
} CommonServiceCode;

typedef enum {
  k_SC_startFirmwareUpdate = (MIN_SERVICE_CODE    ),
  k_SC_sendDataPacket      = (MIN_SERVICE_CODE + 1),
  k_SC_sendCommand         = (MIN_SERVICE_CODE + 2),
} FirmwareUpdateServiceCode;

/* Request buffer methods */
extern void     setClassCode_req(RequestMessageBuffer_t msg, uint16_t classCode);
extern void     setInstanceNumber_req(RequestMessageBuffer_t msg, uint16_t instanceNumber);
extern void     setServiceCode_req(RequestMessageBuffer_t msg, uint16_t serviceCode);
extern void     setAttributeCode_req(RequestMessageBuffer_t msg, uint16_t attributeCode);
extern uint16_t getPayloadOffset_req(RequestMessageBuffer_t msg);
extern void     setLength_req(RequestMessageBuffer_t msg, uint16_t length);
extern uint16_t getServiceCode_req(RequestMessageBuffer_t msg);
extern uint16_t getAttributeCode_req(RequestMessageBuffer_t msg);

/* Response buffer methods */
extern uint16_t getLength_resp(ResponseMessageBuffer_t msg);
extern void     setLength_resp(ResponseMessageBuffer_t msg, uint16_t length);
extern uint16_t getStatusCode_resp(ResponseMessageBuffer_t msg); 
extern uint16_t getPayloadOffset_resp(ResponseMessageBuffer_t msg);

extern uint16_t sequence_t_uint8_t_marshal(ResponseMessageBuffer_t request, uint32_t offset, sequence_t_uint8_t *data);
extern uint16_t sequence_t_uint8_t_unmarshal(ResponseMessageBuffer_t request, uint32_t offset, sequence_t_uint8_t *data);
extern uint16_t string_t_unmarshal(ResponseMessageBuffer_t request, uint32_t offset, string_t *str);

/* Marshalling utility methods */
extern uint32_t uint8_t_marshal(MessageBuffer_t msg, uint32_t offset, uint8_t value);
extern uint32_t uint8_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint8_t *value);
extern uint32_t uint16_t_marshal(MessageBuffer_t msg, uint32_t offset, uint16_t value);
extern uint32_t uint16_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint16_t *value);
extern uint32_t uint32_t_marshal(MessageBuffer_t msg, uint32_t offset, uint32_t value);
extern uint32_t uint32_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint32_t *value);
extern uint32_t bool_marshal(MessageBuffer_t msg, uint32_t offset, uint8_t value);

#endif
