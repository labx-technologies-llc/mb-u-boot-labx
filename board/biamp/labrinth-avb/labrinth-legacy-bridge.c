#include "AvbDefs.h"
#include "labrinth-legacy-bridge.h"
#include "BackplaneBridge_unmarshal.h"
#include "spi-mailbox.h"
#include "BackplaneBridge.h"
#include "xparameters.h"
#include "linux/types.h"
#include <command.h>
#include <common.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0 
#endif

/* Default and maximum number of MAC match units */
#define DEFAULT_MAC_MATCH_UNITS  (4)
#define MAX_MAC_MATCH_UNITS     (32)

#define MAC_MATCH_NONE 0
#define MAC_MATCH_ALL 1

static const u8 MAC_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 MAC_ZERO[6]      = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#define NUM_SRL16E_CONFIG_WORDS 8
#define NUM_SRL16E_INSTANCES    12

typedef struct {
  uint32_t whichAvbPort;
  uint32_t whichFilter;
  uint32_t enabled;
  uint8_t  macAddress[MAC_ADDRESS_BYTES];
} MacFilterConfig;

typedef struct {
  uint32_t rxPortSelection;
  uint32_t txPortsEnable[NUM_AVB_BRIDGE_PORTS];
} BridgePortsConfig;

/* 
 * Enumerated type for AVB port selection
 */
typedef enum {
  BRIDGE_AVB_PORT_0 = 0,
  BRIDGE_AVB_PORT_1
} PortSelection;

typedef enum { 
  LOADING_MORE_WORDS, 
  LOADING_LAST_WORD 
} LoadingMode;

typedef enum { 
  SELECT_NONE, 
  SELECT_SINGLE, 
  SELECT_ALL 
} SelectionMode;

/* Busy loops until the match unit configuration logic is idle.  The hardware goes 
 * idle very quickly and deterministically after a configuration word is written, 
 * so this should not consume very much time at all.
 */
void wait_match_config(uint32_t whichPort) {
  uint32_t statusWord;
  uint32_t timeout = 10000;
  do {
    statusWord = BRIDGE_READ_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_CTRL_STAT_REG));
    if (timeout-- == 0) {
      printf("depacketizer: wait_match_config timeout!\n");
      break;
    }
  } while(statusWord & FILTER_LOAD_ACTIVE);
}

/* Sets the loading mode for any selected match units.  This revolves around
 * automatically disabling the match units' outputs while they're being
 * configured so they don't fire false matches, and re-enabling them as their
 * last configuration word is loaded.
 */
void set_matcher_loading_mode(uint32_t whichPort,
                                     LoadingMode loadingMode) {
  uint32_t controlWord = BRIDGE_READ_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_CTRL_STAT_REG));

  if(loadingMode == LOADING_MORE_WORDS) {
    /* Clear the "last word" bit to suppress false matches while the units are
     * only partially cleared out
     */
    controlWord &= ~FILTER_LOAD_LAST;
  } else {
    /* Loading the final word, flag the match unit(s) to enable after the
     * next configuration word is loaded.
     */
    controlWord |= FILTER_LOAD_LAST;
  }
  //printk("CONTROL WORD %08X\n", controlWord);
  BRIDGE_WRITE_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_CTRL_STAT_REG), controlWord);
}

/* Loads truth tables into a match unit using the newest, "unified" match
 * architecture.  This is SRL16E based (not cascaded) due to the efficient
 * packing of these primitives into Xilinx LUT6-based architectures.
 */
void load_unified_matcher(uint32_t whichPort,
                          const uint8_t matchMac[6]) {
  int32_t wordIndex;
  int32_t lutIndex;
  uint32_t configWord = 0x00000000;
  uint32_t matchChunk;

  /* In this architecture, all of the SRL16Es are loaded in parallel, with each
   * configuration word supplying two bits to each.  Only one of the two bits can
   * ever be set, so there is just an explicit check for one.
   */
  for(wordIndex = (NUM_SRL16E_CONFIG_WORDS - 1); wordIndex >= 0; wordIndex--) {
    for(lutIndex = (NUM_SRL16E_INSTANCES - 1); lutIndex >= 0; lutIndex--) {
      matchChunk = ((matchMac[5-(lutIndex/2)] >> ((lutIndex&1) << 2)) & 0x0F);
      configWord <<= 2;
      if(matchChunk == (wordIndex << 1)) configWord |= 0x01;
      if(matchChunk == ((wordIndex << 1) + 1)) configWord |= 0x02;
    }
    /* 12 nybbles are packed to the MSB */
    configWord <<= 8;

    /* Two bits of truth table have been determined for each SRL16E, load the
     * word and wait for the configuration to occur.  Be sure to flag the last
     * word to automatically re-enable the match unit(s) as the last word completes.
     */
    if(wordIndex == 0) {
      set_matcher_loading_mode(whichPort, LOADING_LAST_WORD);
    }
    printf("MAC LOAD %08X\n", configWord);
    BRIDGE_WRITE_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_LOAD_REG), configWord);
    wait_match_config(whichPort);
  }
}

void select_matchers(uint32_t whichPort,
                     SelectionMode selectionMode,
                     uint32_t matchUnit) {

  switch(selectionMode) {
  case SELECT_NONE:
    /* De-select all the match units */
    printf("MAC SELECT %08X\n", 0);
    BRIDGE_WRITE_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_SELECT_REG),
                     FILTER_SELECT_NONE);
    break;

  case SELECT_SINGLE:
    /* Select a single unit */
    printf("MAC SELECT %08X\n", 1 << matchUnit);
    BRIDGE_WRITE_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_SELECT_REG), (1 << matchUnit));
    break;

  default:
    /* Select all match units at once */
    printf("MAC SELECT %08X\n", 0xFFFFFFFF);
    BRIDGE_WRITE_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_SELECT_REG),
                     FILTER_SELECT_ALL);
    break;
  }
}

/* Clears any selected match units, preventing them from matching any packets */
void clear_selected_matchers(uint32_t whichPort) {
  uint32_t wordIndex;

  /* Ensure the unit(s) disable as the first word is load to prevent erronous
   * matches as the units become partially-cleared
   */
  set_matcher_loading_mode(whichPort, LOADING_MORE_WORDS);

  for(wordIndex = 0; wordIndex < NUM_SRL16E_CONFIG_WORDS; wordIndex++) {
    /* Assert the "last word" flag on the last word required to complete the clearing
     * of the selected unit(s).
     */
    if(wordIndex == (NUM_SRL16E_CONFIG_WORDS - 1)) {
      set_matcher_loading_mode(whichPort, LOADING_LAST_WORD);
    }
    //printk("MAC LOAD %08X\n", 0);
    BRIDGE_WRITE_REG(BRIDGE_PORT_REG_ADDRESS(BRIDGE_BASE, whichPort, FILTER_LOAD_REG),
              FILTER_LOAD_CLEAR);
  }
}

/*
 * Resets the packet bridge to a known state - permitting pass-through
 * of data from the backplane to the AVB network, but permitting no
 * traffic in the other direction.
 */
void reset_legacy_bridge(void) {
  printf("Resetting legacy bridge\n");

  /* Clear out all of the Rx filter match units for both ports */
  select_matchers(AVB_PORT_0, SELECT_ALL, 0);
  clear_selected_matchers(AVB_PORT_0);
  select_matchers(AVB_PORT_0, SELECT_NONE, 0);
  select_matchers(AVB_PORT_1, SELECT_ALL, 0);
  clear_selected_matchers(AVB_PORT_1);
  select_matchers(AVB_PORT_1, SELECT_NONE, 0);

  /* Disable transmission on both ports */
  BRIDGE_WRITE_REG(BRIDGE_REG_ADDRESS(BRIDGE_BASE, BRIDGE_CTRL_REG), BRIDGE_TX_EN_NONE);

  /* Hit the backplane MAC's Tx and Rx reset registers */
  BRIDGE_WRITE_REG(BP_MAC_REG_ADDRESS(BRIDGE_BASE, MAC_RX_CONFIG_REG),
            MAC_RX_RESET);
  BRIDGE_WRITE_REG(BP_MAC_REG_ADDRESS(BRIDGE_BASE, MAC_TX_CONFIG_REG),
            MAC_TX_RESET);
}

void configureBridgePorts(bool bridgeActive,
                          PortSelection whichPort) {
  // Send a port configuration control command; Rx MAC filters
  // will be cleared if neither Tx port is selected as active
  BridgePortsConfig portsConfig;
  portsConfig.rxPortSelection = ((whichPort == BRIDGE_AVB_PORT_0) ?
                                 RX_PORT_0_SELECT : RX_PORT_1_SELECT); 
  portsConfig.txPortsEnable[0] = ((bridgeActive & (whichPort == BRIDGE_AVB_PORT_0)) ?
                                 TX_PORT_ENABLED : TX_PORT_DISABLED);
  portsConfig.txPortsEnable[1] = ((bridgeActive & (whichPort == BRIDGE_AVB_PORT_1)) ?
                                 TX_PORT_ENABLED : TX_PORT_DISABLED);
  
  /* If neither Tx port is enabled, disabled everything by resettting the
   * entire bridge, which also clears all of the Rx MAC filters for both ports
   * in addition to disabling Tx on both ports.
   */
  if((portsConfig.txPortsEnable[0] == TX_PORT_DISABLED) &
     (portsConfig.txPortsEnable[1] == TX_PORT_DISABLED)) {
    reset_legacy_bridge();
  } else {
    uint32_t bridgeConfigWord = BRIDGE_TX_EN_NONE;
    
    /* Actively bridging, configure appropriately */
    if(portsConfig.rxPortSelection == RX_PORT_1_SELECT) {
      printf("Bridging backplane legacy Rx to port 1\n");
      bridgeConfigWord |= BRIDGE_RX_PORT_1;
    } else {
      printf("Bridging backplane legacy Rx to port 0\n");
    }

    if(portsConfig.txPortsEnable[0] == TX_PORT_ENABLED) {
      printf("Bridging backplane legacy Tx to port 0\n");
      bridgeConfigWord |= BRIDGE_TX_EN_PORT_0;
    }

    if(portsConfig.txPortsEnable[1] == TX_PORT_ENABLED) {
      printf("Bridging backplane legacy Tx to port 1\n");
      bridgeConfigWord |= BRIDGE_TX_EN_PORT_1;
    }
    BRIDGE_WRITE_REG(BRIDGE_REG_ADDRESS(BRIDGE_BASE, BRIDGE_CTRL_REG), bridgeConfigWord);
  }
}

void configure_mac_filter(uint32_t whichPort,
                                 uint32_t unitNum,
                                 const u8 mac[6],
                                 uint32_t mode) {
  /* Only allow programming up to the supported number of MAC match units */
  if (unitNum >= NUM_MATCH_UNITS) return;

  /* Ascertain that the configuration logic is ready, then select the matcher */
  wait_match_config(whichPort);
  select_matchers(whichPort, SELECT_SINGLE, unitNum);

  if (mode == MAC_MATCH_NONE) {
    printf("Config port %d, match unit %d disabled\n",
               whichPort, unitNum);

    clear_selected_matchers(whichPort);
  } else {
    printf("Config port %d, match unit %d address: %02X:%02X:%02X:%02X:%02X:%02X\n",
               whichPort, unitNum,
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Set the loading mode to disable as we load the first word */
    set_matcher_loading_mode(whichPort, LOADING_MORE_WORDS);

    /* Calculate matching truth tables for the LUTs and load them */
    load_unified_matcher(whichPort, mac);
  }

  /* De-select the match unit */
  select_matchers(whichPort, SELECT_NONE, 0);
}

AvbDefs__ErrorCode configureBridge(bool bridgeEnabled,
				   BackplaneBridge__LegacyBridgePort whichPort) {

  AvbDefs__ErrorCode returnValue;

  // Translate the call into the native method
  PortSelection selectedPort =
  ((whichPort == e_AVB_PORT_1) ?
    BRIDGE_AVB_PORT_0 : BRIDGE_AVB_PORT_1);
  configureBridgePorts(bridgeEnabled, selectedPort);

  returnValue = e_EC_SUCCESS;

  return(returnValue);
}

AvbDefs__ErrorCode enableMatchUnit(BackplaneBridge__LegacyBridgePort whichPort, 
                                   uint32_t whichUnit, 
                                   AvbDefs__MacAddress *matchAddress) {
  AvbDefs__ErrorCode returnValue;
  uint32_t byteIndex;

  //Enable the selected match unit
  MacFilterConfig filterConfig;
  filterConfig.whichAvbPort = ((whichPort == e_AVB_PORT_0) ?
                               BRIDGE_AVB_PORT_0 :
                               BRIDGE_AVB_PORT_1);
  filterConfig.whichFilter = whichUnit;
  for(byteIndex = 0; byteIndex < MAC_ADDRESS_BYTES; byteIndex++) {
    filterConfig.macAddress[byteIndex] = matchAddress->mac[byteIndex];
  }
  
  /* Validate the parameters */
  if((filterConfig.whichAvbPort >= NUM_AVB_BRIDGE_PORTS) |
     (filterConfig.whichFilter >= NUM_MATCH_UNITS)) {
    returnValue = e_EC_NOT_EXECUTED;
    return(returnValue);
  }

  configure_mac_filter(filterConfig.whichAvbPort,
                       filterConfig.whichFilter,
                       filterConfig.macAddress,
                       (filterConfig.enabled ? MAC_MATCH_ALL : MAC_MATCH_NONE));

  returnValue = e_EC_SUCCESS;

  return(returnValue);
}

AvbDefs__ErrorCode disableMatchUnit(BackplaneBridge__LegacyBridgePort whichPort, 
                                    uint32_t whichUnit) {
  
  AvbDefs__ErrorCode returnValue;

  //Disable the selected match unit
  MacFilterConfig filterConfig;
  filterConfig.whichAvbPort = ((whichPort == e_AVB_PORT_0) ?
                            BRIDGE_AVB_PORT_0 :
                            BRIDGE_AVB_PORT_1);
  filterConfig.whichFilter = whichUnit;
  filterConfig.enabled     = DISABLE_MAC_FILTER;
  
  /* Validate the parameters */
  if((filterConfig.whichAvbPort >= NUM_AVB_BRIDGE_PORTS) |
     (filterConfig.whichFilter >= NUM_MATCH_UNITS)) {
    returnValue = e_EC_NOT_EXECUTED;
    return(returnValue);
  }

  configure_mac_filter(filterConfig.whichAvbPort,
                       filterConfig.whichFilter,
                       filterConfig.macAddress,
                       (filterConfig.enabled ? MAC_MATCH_ALL : MAC_MATCH_NONE));

  returnValue = e_EC_SUCCESS;

  return(returnValue);
}

AvbDefs__ErrorCode get_numMatchUnits(uint32_t *matchUnits) {

  AvbDefs__ErrorCode returnValue;

  *matchUnits = NUM_MATCH_UNITS;

  returnValue = e_EC_SUCCESS;

  return(returnValue);
}
