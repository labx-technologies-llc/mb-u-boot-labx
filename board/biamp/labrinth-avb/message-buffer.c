#include <linux/string.h>
#include <malloc.h>

#include "message-buffer.h"

/* MessageBuffer Routines */
void set_bytes(MessageBuffer_t msg, uint32_t offset, const uint8_t* data, uint32_t length)
{
  memcpy(&msg[offset], data, length); 
}

void set_uint8_t(MessageBuffer_t msg, uint32_t offset, uint8_t value)
{ 
  msg[offset] = value; 
}

void set_uint16_t(MessageBuffer_t msg, uint32_t offset, uint16_t value)
{ 
  /* Pack big-endian */
  msg[offset] = value >> 8; 
  msg[offset+1] = value; 
}

void set_uint32_t(MessageBuffer_t msg, uint32_t offset, uint32_t value)
{ 
  set_uint16_t(msg, offset, value >> 16); 
  set_uint16_t(msg, offset+2, value); 
}

uint8_t* get_bytes(MessageBuffer_t msg, uint32_t offset) 
{ 
  return &msg[offset]; 
}

uint8_t get_uint8_t(MessageBuffer_t msg, uint32_t offset) 
{ 
  return msg[offset]; 
}

uint16_t get_uint16_t(MessageBuffer_t msg, uint32_t offset) 
{ 
  return (msg[offset] << 8) | msg[offset+1]; 
}

uint32_t get_uint32_t(MessageBuffer_t msg, uint32_t offset) 
{ 
  return ( ((uint32_t)get_uint16_t(msg,offset) << 16) | (uint32_t)get_uint16_t(msg,offset+2) ); 
}
uint16_t sequence_t_uint8_t_marshal(RequestMessageBuffer_t request, uint32_t offset, sequence_t_uint8_t *data)
{
  uint16_t sequenceOffset = 0;

  sequenceOffset += uint32_t_marshal(request, offset, data->m_size);
  memcpy(&request[offset + sequenceOffset], data->m_data, data->m_size);
  sequenceOffset += data->m_size;
  return sequenceOffset;
}

uint16_t string_t_unmarshal(RequestMessageBuffer_t request, uint32_t offset, string_t *str)
{
  uint32_t size;
  memcpy(&size, &request[offset], 4);
  *str = (string_t) malloc(size);
  memset(*str, 'S', size);
  memcpy(*str, &request[offset + 4], size);

  /* The marshalled string should have a NULL terminator anyways, which *is* included
   * in the size, but make sure!
   */
  (*str)[size - 1] = '\0';
  return(4+size);
}

uint16_t sequence_t_uint8_t_unmarshal(RequestMessageBuffer_t request, uint32_t offset, sequence_t_uint8_t *data)
{
  uint16_t sequenceOffset=0;
  uint32_t sequenceLen=0;

  sequenceOffset += uint32_t_unmarshal(request, offset, &sequenceLen);
  data->m_size = sequenceLen;
  memcpy(data->m_data, &request[offset + sequenceOffset], sequenceLen);
  sequenceOffset += sequenceLen;
  return sequenceOffset;
}


uint32_t uint8_t_marshal(MessageBuffer_t msg, uint32_t offset, const uint8_t value)
{
  set_uint8_t(msg,offset,value);
  return 1; 
}

uint32_t uint8_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint8_t *value)
{
  *value = get_uint8_t(msg,offset); 
  return 1; 
}

uint32_t uint16_t_marshal(MessageBuffer_t msg, uint32_t offset, uint16_t value)
{ 
  set_uint16_t(msg,offset, value); 
  return 2; 
}

uint32_t uint16_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint16_t *value)
{ 
  *value = get_uint16_t(msg,offset); 
  return 2; 
}

uint32_t uint32_t_marshal(MessageBuffer_t msg, uint32_t offset, uint32_t value)
{ 
  set_uint32_t(msg,offset, value); 
  return 4; 
}

uint32_t uint32_t_unmarshal(MessageBuffer_t msg, uint32_t offset, uint32_t *value)
{ 
  *value = get_uint32_t(msg,offset); 
  return 4; 
}

uint32_t bool_marshal(MessageBuffer_t msg, uint32_t offset, uint8_t value)
{ 
  set_uint32_t(msg,offset, (value) ? 0xFFFFFFFF : 0x00000000); 
  return 4; 
}
    

/* RequestMessageBuffer_t  routines */
void setLength_req(RequestMessageBuffer_t msg,uint16_t length)
{ 
  set_uint16_t(msg,0, length); 
}

void setClassCode_req(RequestMessageBuffer_t msg,uint16_t classCode)           
{ 
  set_uint16_t(msg,4, classCode); 
}

void setInstanceNumber_req(RequestMessageBuffer_t msg,uint16_t instanceNumber) 
{ 
  set_uint16_t(msg,6, instanceNumber); 
}

void setServiceCode_req(RequestMessageBuffer_t msg,uint16_t serviceCode)       
{ 
  set_uint16_t(msg,8, serviceCode); 
}

void setAttributeCode_req(RequestMessageBuffer_t msg,uint16_t attributeCode)   
{ 
  set_uint16_t(msg,10, attributeCode); 
}

uint16_t getPayloadOffset_req(RequestMessageBuffer_t msg)   
{ 
  return 12; 
}

uint16_t getLength_req(RequestMessageBuffer_t msg)          
{ 
  return get_uint16_t(msg,0); 
}

uint16_t getClassCode_req(RequestMessageBuffer_t msg)       
{ 
  return get_uint16_t(msg,4); 
}

uint16_t getInstanceNumber_req(RequestMessageBuffer_t msg)  
{ 
  return get_uint16_t(msg,6); 
}

uint16_t getServiceCode_req(RequestMessageBuffer_t msg)     
{ 
  return get_uint16_t(msg,8); 
}

uint16_t getAttributeCode_req(RequestMessageBuffer_t msg)   
{ 
  return get_uint16_t(msg,10); 
}

/* ResponseMessageBuffer_t  routines */

uint16_t getLength_resp(ResponseMessageBuffer_t msg)          
{ 
  return get_uint16_t(msg,0); 
}

void setLength_resp(ResponseMessageBuffer_t msg,uint16_t length)                 
{ 
  set_uint16_t(msg,0, length); 
}

void setStatusCode_resp(ResponseMessageBuffer_t msg,uint16_t statusCode)         
{ 
  set_uint16_t(msg, 2, statusCode); 
}

uint16_t getPayloadOffset_resp(ResponseMessageBuffer_t msg) 
{ 
  return(4); 
}

uint16_t getStatusCode_resp(ResponseMessageBuffer_t msg) 
{ 
  return get_uint16_t(msg, 2); 
}


