#include "mac_helper.h"


bool equal_mac(mac_addr_t ls, mac_addr_t rs){
    for (size_t i = 0; i < MAC_ADDR_SIZE; i++)
    {
        if(ls.addr[i] != rs.addr[i]){
            return false;
        }
    }
    return true;
}